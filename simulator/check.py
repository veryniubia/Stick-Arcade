"""Exercise the complete application through virtual hardware, without a physical StickS3."""
import csv
from pathlib import Path
import subprocess
import sys
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / '.pio/sticks3-simulator'
EXE = BUILD / 'sticks3-simulator.exe'
OUT = BUILD / 'checks'


def tap(actions, frame, key):
    actions += [(frame, key, 1), (frame + 2, key, 0)]


def run(name, actions, frames=340, device=1, realtime=False):
    out = OUT / name
    out.mkdir(parents=True, exist_ok=True)
    script = out / 'input.txt'
    script.write_text(''.join(f'{f} {k} {v}\n' for f, k, v in sorted(actions)))
    cmd = [str(EXE), '--headless', '--seed', '122', '--frames', str(frames), '--script', str(script),
           '--output', str(out), '--device', str(device)]
    if realtime:
        cmd.append('--realtime')
    subprocess.run(cmd, check=True, timeout=40)
    with (out / 'input-trace.csv').open() as f:
        rows = list(csv.DictReader(f))
    with (out / f'events-{device}.csv').open() as f:
        events = list(csv.DictReader(f))
    return out, rows, events


def menu(index):
    actions = []
    tap(actions, 4, 'a')  # Title, including its entry animation.
    for n in range(index):
        tap(actions, 40 + n * 12, 'b')
    return actions


def main():
    if '--no-build' not in sys.argv:
        subprocess.run([sys.executable, str(ROOT / 'simulator/run.py'), '--build-only'], check=True)
    OUT.mkdir(parents=True, exist_ok=True)
    images = []
    for index, name in enumerate(['throwhalla', 'big-bump', 'moonduel', 'middiff']):
        actions = menu(index)
        tap(actions, 100, 'a')  # Enter from the real launcher.
        tap(actions, 115, 'b')  # Solo.
        if index == 3:
            tap(actions, 195, 'b')  # First B calibrates in lobby; after sampling, B starts solo.
            actions += [(205, 'joyX', 100), (205, 'joyY', 100), (215, 'stick', 1),
                        (218, 'stick', 0), (240, 'joyX', 0), (240, 'joyY', 0),
                        (205, 'yaw', 15), (230, 'yaw', 0), (235, 'pitch', -15)]
        else:
            actions += [(170, 'tilt', 25), (220, 'tilt', -20), (250, 'tilt', 0)]
        actions += [(200, 'a', 1), (230, 'a', 0)]
        tap(actions, 240, 'b')
        actions += [(180, 'shot', 'playing.ppm'), (270, 'shot', 'action.ppm'),
                    (275, 'shake', 1), (295, 'shot', 'exit.ppm')]
        tap(actions, 305, 'a')
        out, rows, events = run(name, actions)
        tags = [e['event'] for e in events]
        assert 'lobby' in tags and 'solo' in tags, (name, events)
        assert 'exit_prompt' in tags and 'exit_confirm' in tags and 'play_end' in tags, (name, events)
        assert any(e['event'] == 'radio' and e['value'] == '1' for e in events), (name, events)
        assert rows[215]['a'] == '1' and rows[231]['a'] == '0', name
        assert rows[215]['aCount'] == rows[225]['aCount'], 'Held key must not repeat presses'
        if index == 1:
            assert (rows[200]['width'], rows[200]['height']) == ('135', '240')
        if index == 3:
            assert int(rows[210]['joyX']) > 90 and int(rows[210]['joyY']) > 90
            assert int(rows[220]['stickCount']) > 0
            assert abs(float(rows[220]['yaw']) - 35) < .1, rows[220]
            assert abs(float(rows[231]['yaw'])) < .1, rows[231]
            assert float(rows[260]['pitch']) > 5, rows[260]
        else:
            field = 'driveTilt' if index == 1 else 'tilt'
            assert float(rows[200][field]) > 20, (name, rows[200])
        assert (rows[-1]['width'], rows[-1]['height']) == ('240', '135')
        image = Image.open(out / 'action.ppm')
        assert len(image.getcolors(240 * 240) or []) >= 12, name
        image.save(out / 'action.png')
        if index == 2:
            expected = (out / 'action.ppm').read_bytes()
            run(name, actions)
            assert (out / 'action.ppm').read_bytes() == expected, 'Seeded input replay changed'
        images.append((name, image.copy()))
        print(f'PASS {name}: launcher, solo, hardware input, shake confirmation, return')

    # A virtual device pose is an angle, not a turn command. Hold each pose
    # long enough to expose continued rotation; test return after both stops.
    actions = menu(3)
    tap(actions, 100, 'a')
    tap(actions, 115, 'b')
    tap(actions, 195, 'b')
    actions += [(205, 'yaw', -15), (300, 'yaw', 0), (340, 'yaw', 15),
                (440, 'yaw', 45), (480, 'yaw', 30), (520, 'yaw', 15),
                (560, 'yaw', 0), (600, 'yaw', -45), (640, 'yaw', -30),
                (680, 'yaw', -15), (720, 'yaw', 0)]
    _, rows, _ = run('middiff-pose', actions, 770)
    for frame, expected in [(206, -35), (290, -35), (310, 0), (350, 35),
                            (430, 35), (450, 70), (490, 70), (530, 35),
                            (570, 0), (610, -70), (650, -70), (690, -35), (760, 0)]:
        assert abs(float(rows[frame]['yaw']) - expected) < .1, (frame, rows[frame])
    print('PASS MidDiff pose: hold left/right, return to centre, recover from both stops')

    # Settings changes persist separately for each virtual device.
    actions = menu(4)
    tap(actions, 100, 'a')
    tap(actions, 110, 'a')
    actions += [(130, 'shot', 'settings.ppm')]
    out, _, _ = run('settings', actions, 140)
    saved = out / 'settings-1.bin'
    assert saved.exists()
    before = saved.read_bytes()
    run('settings', menu(4) + [(100, 'shot', 'launcher.ppm')], 120)
    assert saved.read_bytes() == before
    images.append(('settings', Image.open(out / 'settings.ppm').copy()))
    # Missing joystick must prevent starting MidDiff.
    actions = menu(3) + [(0, 'connected', 0)]
    tap(actions, 100, 'a')
    _, _, events = run('no-joystick', actions, 150)
    assert not any(e['event'] == 'lobby' for e in events), events
    print('PASS settings persistence and missing joystick gate')

    # Launch two full applications on the same game. Real-time mode preserves heartbeat timing.
    procs = []
    for device in (1, 2):
        out = OUT / f'link-{device}'
        out.mkdir(exist_ok=True)
        actions = menu(2)
        tap(actions, 100, 'a')
        actions += [(410, 'shot', 'online.ppm')]
        script = out / 'input.txt'
        script.write_text(''.join(f'{f} {k} {v}\n' for f, k, v in sorted(actions)))
        procs.append(subprocess.Popen([str(EXE), '--headless', '--realtime', '--seed', str(120 + device), '--frames', '430',
                                      '--device', str(device), '--script', str(script), '--output', str(out)]))
    try:
        for p in procs:
            assert p.wait(timeout=35) == 0
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
    roles = set()
    for device in (1, 2):
        out = OUT / f'link-{device}'
        with (out / f'events-{device}.csv').open() as f:
            events = list(csv.DictReader(f))
        playing = [e for e in events if e['event'] == 'online_status' and e['value'] == '3']
        assert playing, (device, events)
        roles.add(playing[-1]['detail'])
        images.append((f'online device {device}', Image.open(out / 'online.ppm').copy()))
    assert roles == {'1', '2'}, roles
    print('PASS two virtual devices: pairing, countdown, host/client gameplay')
    subprocess.run([str(EXE), '--frames', '20', '--output', str(OUT / 'window')], check=True, timeout=15)
    print('PASS Windows window/control creation, painting and clean exit')
    sheet = Image.new('RGB', (3 * 500, 3 * 520), '#141827')
    draw = ImageDraw.Draw(sheet)
    for i, (label, im) in enumerate(images):
        im.thumbnail((480, 480), Image.Resampling.NEAREST)
        im = im.resize((im.width * min(480 // im.width, 480 // im.height),
                        im.height * min(480 // im.width, 480 // im.height)), Image.Resampling.NEAREST)
        x, y = (i % 3) * 500 + 10, (i // 3) * 520 + 28
        draw.text((x, y - 18), label, fill='white')
        sheet.paste(im, (x, y))
    sheet.save(OUT / 'contact-sheet.png')
    print(OUT / 'contact-sheet.png')


if __name__ == '__main__':
    main()
