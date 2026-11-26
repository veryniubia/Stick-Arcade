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
        tap(actions, 34 + n * 10, 'b')
    return actions


def check_wheel():
    # Exercise the whole path from gravity and stored settings to the drawn kart.
    def drive(name, tilt, offset=0, pitch=0):
        name = 'wheel-' + name
        out = OUT / name
        out.mkdir(parents=True, exist_ok=True)
        data = bytearray((1, 3, 3, 2)) + int(offset * 10).to_bytes(2, 'little', signed=True) + bytes(1)
        crc = 0xa5
        for value in data:
            crc ^= value
            for _ in range(8):
                crc = ((crc << 1) ^ (7 if crc & 128 else 0)) & 255
        (out / 'settings-1.bin').write_bytes(data + bytes([crc]))
        actions = menu(5)
        tap(actions, 100, 'a')
        actions += [(115, 'b', 1), (122, 'pitch', pitch), (135, 'shot', 'overview.ppm'),
                    (180, 'shot','overview-held.ppm'), (235,'shot','focus.ppm'),
                    (278,'shot','three.ppm'), (338,'shot','two.ppm'), (398,'shot','one.ppm'),
                    (458, 'shot', 'ready.ppm'), (465, 'a', 1), (512, 'b', 0),
                    (514, 'tilt', tilt), (528, 'shot', 'wheel.ppm'), (532, 'tilt', 0)]
        _, rows, events = run(name, actions, 544)
        assert any(e['event'] == 'solo' for e in events)
        assert abs(float(rows[510]['wheelTilt'])) < .1
        assert abs(float(rows[527]['wheelTilt']) - tilt) < .1
        assert all(abs(float(row['wheelTilt'])) < .001 for row in rows[532:])
        im = Image.open(out / 'wheel.ppm')
        ready = Image.open(out / 'ready.ppm')
        overview = Image.open(out / 'overview.ppm')
        assert im.size == ready.size == overview.size == (135,240)
        assert overview.crop((0,55,135,225)).tobytes() != ready.crop((0,55,135,225)).tobytes()
        assert overview.tobytes() == Image.open(out/'overview-held.ppm').tobytes()
        beats = [Image.open(out/(n+'.ppm')) for n in ('three','two','one')]
        frames = [im.crop((0,55,135,205)).tobytes() for im in [overview,Image.open(out/'focus.ppm'),*beats]]
        assert len(set(frames[:3])) == 3 and len(set(frames[2:])) == 1
        assert len({im.crop((55,25,80,55)).tobytes() for im in beats+[ready]}) == 4
        orange = im.getpixel((39,9))
        # The DRIFT indicator must already be lit before A, with the lobby B still held.
        assert orange in [c for _,c in ready.crop((53,225,87,240)).getcolors()]
        assert orange not in [c for _,c in im.crop((53,225,87,240)).getcolors()]
        assert ready.crop((95,212,129,229)).tobytes() != im.crop((95,212,129,229)).tobytes()
        assert ready.crop((99,230,125,240)).tobytes() != im.crop((99,230,125,240)).tobytes()
        # With the car pointing forward, a short bank should turn the nose
        # relative to the lagging camera. Compare front and rear bodywork.
        pixels = [(x,y) for y in range(137,187) for x in range(41,94) if im.getpixel((x,y)) == orange]
        middle = sum(y for x,y in pixels)/len(pixels)
        front = [x for x,y in pixels if y<middle]
        rear = [x for x,y in pixels if y>middle]
        return im.tobytes(), sum(front)/len(front)-sum(rear)/len(rear)

    straight, center = drive('level', 0)
    assert drive('tabletop', 0, offset=12, pitch=90)[0] == straight, 'flat with saved aim trim'
    assert drive('tremor', 4.5, offset=-8)[0] == straight, 'small tilt on the straight'
    assert drive('right', 20)[1] > center + 1, 'right turn from an east-facing start'
    assert drive('left', -20)[1] < center - 1, 'left turn from an east-facing start'
    print('PASS HwyDrift wheel: level, saved aim trim, neutral band, left and right')


def check_springring():
    def replay(name, angle):
        actions = menu(4) + [(0, 'connected', 0)]
        tap(actions, 100, 'a')
        tap(actions, 115, 'b')
        actions += [(165, 'shot', 'ready.ppm'), (170, 'tilt', angle),
                    (192, 'shot', 'reach.ppm'), (200, 'a', 1),
                    (207, 'shot', 'launch.ppm'), (215, 'shot', 'air.ppm'),
                    (218, 'a', 0), (240, 'shot', 'score.ppm'),
                    (250, 'shot', 'hold.ppm'), (300, 'shot', 'reset.ppm')]
        out, rows, events = run('springring-' + name, actions, 301)
        assert any(e['event'] == 'lobby' and e['value'] == '8' for e in events)
        assert rows[208]['a'] == '1' and rows[220]['a'] == '0'
        return {f.stem: Image.open(f).convert('RGB') for f in out.glob('*.ppm')}

    flat = replay('level', 0)
    right = replay('right', 12)
    left = replay('left', -12)
    def boot_x(frame):
        # The cream boot sole is the lowest bright row beneath the blue hip.
        sole_color = flat['ready'].getpixel((103, 110))
        soles = [(x, y) for y in range(92, 111) for x in range(84, 117)
                 if frame.getpixel((x, y)) == sole_color and not (107 <= x <= 133 and y == 107)]
        lowest = max(y for x, y in soles)
        xs = [x for x, y in soles if y == lowest]
        return sum(xs) / len(xs)
    assert boot_x(left['reach']) < boot_x(flat['reach']) < boot_x(right['reach'])
    board = (120, 107)
    assert right['launch'].getpixel(board) != flat['launch'].getpixel(board)
    assert flat['ready'].crop((50, 25, 190, 110)) != flat['air'].crop((50, 25, 190, 110))
    assert flat['score'] == flat['hold'], 'impact pause must retain the scoring pose'
    assert flat['ready'].crop((0, 21, 240, 135)) == flat['reset'].crop((0, 21, 240, 135))
    assert flat['ready'].crop((50, 5, 86, 16)) != flat['reset'].crop((50, 5, 86, 16))
    print('PASS Spring Ring: level A, left/right boot placement, board flash, flight, score, pause and reset')



def main():
    if '--no-build' not in sys.argv:
        subprocess.run([sys.executable, str(ROOT / 'simulator/run.py'), '--build-only'], check=True)
    OUT.mkdir(parents=True, exist_ok=True)
    check_wheel()
    check_springring()
    images = []
    for index, name in enumerate(['throwhalla', 'big-bump', 'moonduel', 'springring', 'hwydrift', 'middiff']):
        actions = menu(index + 1)
        tap(actions, 100, 'a')  # Enter from the real launcher.
        tap(actions, 115, 'b')  # Solo.
        if index == 5:
            tap(actions, 195, 'b')  # First B calibrates in lobby; after sampling, B starts solo.
            actions += [(205, 'joyX', 100), (205, 'joyY', 100), (215, 'stick', 1),
                        (218, 'stick', 0), (240, 'joyX', 0), (240, 'joyY', 0),
                        (205, 'yaw', 15), (230, 'yaw', 0), (235, 'pitch', -15)]
        elif index == 4:
            actions += [(0, 'connected', 0), (195, 'tilt', -20), (231, 'tilt', 0)]
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
        if index in (1,4):
            assert (rows[200]['width'], rows[200]['height']) == ('135', '240')
        if index == 5:
            assert int(rows[210]['joyX']) > 90 and int(rows[210]['joyY']) > 90
            assert int(rows[220]['stickCount']) > 0
            assert abs(float(rows[220]['yaw']) - 35) < .1, rows[220]
            assert abs(float(rows[231]['yaw'])) < .1, rows[231]
            assert float(rows[260]['pitch']) > 5, rows[260]
        elif index == 4:
            assert float(rows[215]['wheelTilt']) < -17 and int(rows[215]['joyX']) == 0
            assert any(e['event'] == 'lobby' and e['value'] == '7' for e in events)
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
    actions = menu(6)
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
    actions = menu(7)
    tap(actions, 116, 'a')
    tap(actions, 126, 'a')
    actions += [(130, 'shot', 'settings.ppm')]
    out, _, _ = run('settings', actions, 140)
    saved = out / 'settings-1.bin'
    assert saved.exists()
    before = saved.read_bytes()
    run('settings', menu(7) + [(124, 'shot', 'launcher.ppm')], 130)
    assert saved.read_bytes() == before
    images.append(('settings', Image.open(out / 'settings.ppm').copy()))
    # Missing joystick must prevent starting MidDiff.
    actions = menu(6) + [(0, 'connected', 0)]
    tap(actions, 100, 'a')
    _, _, events = run('no-joystick', actions, 150)
    assert not any(e['event'] == 'lobby' for e in events), events
    print('PASS settings persistence and missing joystick gate')
    actions = menu(5) + [(0, 'connected', 0)]
    tap(actions, 110, 'a')
    tap(actions, 125, 'b')
    _, _, events = run('hwydrift-no-joystick', actions, 155)
    assert any(e['event'] == 'lobby' and e['value'] == '7' for e in events), events
    assert any(e['event'] == 'solo' for e in events), events

    actions = menu(0) + [(0, 'connected', 0)]
    tap(actions, 100, 'a')
    tap(actions, 104, 'b')  # B arrives before the joystick probe finishes.
    _, _, events = run('random-solo', actions, 200)
    assert any(e['event'] == 'solo' for e in events), events
    assert {e['value'] for e in events if e['event'] == 'lobby'} & {'1', '4', '5', '7', '8'}, events
    print('PASS RANDOM solo without joystick')

    check_link('link', 3, images)
    check_link('random-link', 0, images)
    check_link('random-middiff', 0, None, both_sticks=True)
    check_link('springring-link', 4, None)
    check_link('hwydrift-link', 5, None)
    subprocess.run([str(EXE), '--frames', '20', '--output', str(OUT / 'window')], check=True, timeout=15)
    print('PASS Windows window/control creation, painting and clean exit')
    sheet = Image.new('RGB', (3 * 500, ((len(images) + 2) // 3) * 520), '#141827')
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


def check_link(name, index, images, both_sticks=False):
    # Real-time mode preserves heartbeat timing for the two applications.
    procs = []
    for device in (1, 2):
        out = OUT / f'{name}-{device}'
        out.mkdir(exist_ok=True)
        actions = menu(index)
        if index == 0 and device == 1 and not both_sticks:
            actions += [(0, 'connected', 0)]
        tap(actions, 100 if device == 1 else 130, 'a')
        if both_sticks:
            tap(actions, 175 if device == 1 else 240, 'b')
        if name == 'hwydrift-link':
            actions += [(0, 'connected', 0), (390 if device == 1 else 415, 'a', 1),
                        (480, 'tilt', 20 if device == 1 else -20),
                        (484, 'b', 1), (500, 'tilt', 0), (504, 'b', 0),
                        (510, 'shot', 'race.ppm')]
        actions += [(600, 'shot', 'online.ppm')]
        script = out / 'input.txt'
        script.write_text(''.join(f'{f} {k} {v}\n' for f, k, v in sorted(actions)))
        # With six eligible games this seed selects MidDiff for the calibration check.
        seed = 121 if device == 1 else (125 if both_sticks else 128)
        procs.append(subprocess.Popen([str(EXE), '--headless', '--realtime', '--seed', str(seed), '--frames', '620',
                                      '--device', str(device), '--script', str(script), '--output', str(out)]))
    try:
        for p in procs:
            assert p.wait(timeout=35) == 0
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
    roles = set()
    games = set()
    for device in (1, 2):
        out = OUT / f'{name}-{device}'
        with (out / f'events-{device}.csv').open() as f:
            events = list(csv.DictReader(f))
        playing = [e for e in events if e['event'] == 'online_status' and e['value'] == '3']
        assert playing, (device, events)
        roles.add(playing[-1]['detail'])
        games.update(e['value'] for e in events if e['event'] == 'lobby')
        if images is not None:
            images.append((f'{name} device {device}', Image.open(out / 'online.ppm').copy()))
    assert roles == {'1', '2'}, roles
    expected = {'8'} if index == 4 else {'7'} if index == 5 else {'6'} if both_sticks else {'1', '4', '5', '7', '8'}
    assert len(games) == 1 and games <= expected, games
    if name == 'hwydrift-link':
        views = [Image.open(OUT / f'{name}-{device}' / 'race.ppm') for device in (1, 2)]
        assert views[0].getpixel((39,9)) != views[1].getpixel((39,9)), 'Local kart colour must identify each driver'
    print(f'PASS {name}: two virtual devices, countdown, same game')


if __name__ == '__main__':
    main()
