"""Real USB/ESP-NOW powerup checks; preserve states, assertions and framebuffer PNGs."""
import hashlib
import json
import re
import sys
import time
from pathlib import Path

import serial
from net_check import grab
from throwhalla_check import decode


def main():
    ports = sys.argv[1:3]
    out = Path(sys.argv[3]); out.mkdir(parents=True, exist_ok=True)
    sers = [serial.Serial(p, 115200, timeout=.002) for p in ports]
    bufs = [b'', b'']; logs = []; latest = {}; states = {}; saved = {}; checks = []; perf = {}
    parse_states = True

    def drain(seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            for i, ser in enumerate(sers):
                bufs[i] += ser.read(ser.in_waiting or 1)
                while b'\n' in bufs[i]:
                    line, bufs[i] = bufs[i].split(b'\n', 1)
                    text = line.decode(errors='replace').strip()
                    if text.startswith(('NET ', 'PERF ', 'RADIO ', 'STATE ')):
                        logs.append(f'[{ports[i]}] {text}')
                    if text.startswith('NET '): latest[i] = text
                    if text.startswith('PERF '): perf[i] = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', text)}
                    if text.startswith('STATE ') and parse_states: states[i] = decode(bytes.fromhex(text[6:]))

    def send(keys, who=None):
        for i in (range(2) if who is None else [who]): sers[i].write(keys.encode())

    def snapshot(label):
        states.clear(); send('d'); drain(.09)
        assert len(states) == 2, (label, states, latest)
        saved[label] = json.loads(json.dumps(states))
        return dict(states)

    def check(label, ok):
        assert ok, (label, states, latest)
        checks.append(label); print('PASS', label, flush=True)

    def shots(label):
        for i, ser in enumerate(sers):
            grab(ser, str(out / f'{label}_{ports[i]}.png'))
        bufs[0] = bufs[1] = b''
        drain(.08)

    def wait_play():
        latest.clear()
        for _ in range(20):
            drain(.5)
            if all('st=PLAY' in latest.get(i, '') for i in range(2)): return
        raise AssertionError(latest)

    try:
        for ser in sers: ser.dtr = False; ser.rts = False
        send('q=k'); send('v'); wait_play()
        host = next(i for i in range(2) if 'role=1 ' in latest[i])
        print('HOST', ports[host], flush=True)
        client = 1 - host
        check('two devices paired over ESP-NOW', True)
        send('0', host); drain(.15)
        st = snapshot('gallery')
        check('airborne icons synchronize on both displays', st[host]['powerups'] == st[client]['powerups'])
        shots('gallery')

        # All four weapon types contact every modifier through the ordinary physics loop.
        for weapon in range(1, 5):
            for key, name in [('5', 'burger'), ('6', 'split'), ('7', 'lightning'), ('8', 'ice')]:
                send(str(weapon) + key, host); drain(.22)
                st = snapshot(f'weapon_{weapon}_{name}')
                for i in range(2):
                    ps = st[i]['proj']
                    check(f'{ports[i]} weapon {weapon} {name} contact',
                          len(ps) == (2 if key == '6' else 1) and
                          all(p['type'] == weapon and p['owner'] == 0 for p in ps) and
                          all(item['type'] == 0 for item in st[i]['powerups']))
                    if key == '5': check(f'{ports[i]} weapon {weapon} size', all(abs(p['scale'] - 1.6) < .01 for p in ps))
                    if key in ('7', '8'):
                        factor = 1.5 if key == '7' else .6
                        check(f'{ports[i]} weapon {weapon} speed', all(abs(p['speed'] - factor) < .01 for p in ps))
                        if weapon == 1: check(f'{ports[i]} knife actual velocity {name}', abs(ps[0]['vx'] - 40*factor) < .2)
                if weapon == 1: shots(name)

        send('19', host); drain(.18); st = snapshot('stacked')
        check('split inherits burger and lightning on both devices',
              all(len(v['proj']) == 2 and all(abs(p['scale'] - 1.6) < .01 and abs(p['speed'] - 1.5) < .01 for p in v['proj']) for v in st.values()))
        shots('stacked')
        send('S', host); drain(.4); st = snapshot('split_bomb_hits')
        check('both split bombs independently score with unchanged damage',
              all(v['players'][0]['score'] == 2 and v['players'][1]['serial'] == 2 for v in st.values()))
        shots('split_bomb_hits'); drain(.6); st = snapshot('no_repeated_blast_damage')
        check('persistent split blasts do not repeatedly score', all(v['players'][0]['score'] == 2 for v in st.values()))

        send('l'); send('19', host); drain(.3); st = snapshot('packet_loss')
        check('stacked split state converges with 20 percent packet loss',
              all(len(v['proj']) == 2 and all(abs(p['scale'] - 1.6) < .01 for p in v['proj']) for v in st.values()))
        send('k')
        # Keep the effect workload alive across several complete performance windows.
        for _ in range(42): send('19', host); drain(.16)
        saved['effect_perf'] = dict(perf)
        check('both devices sustain 60 fps with stacked effects', all(perf.get(i, {}).get('fps', 0) >= 59 for i in range(2)))

        send('g'); drain(.3)
        counts = set(); duplicate = False
        for attempt in range(35):
            send('g'); drain(.1); st = snapshot(f'random_round_{attempt}')
            for v in st.values():
                kinds = [p['type'] for p in v['powerups'] if p['type']]
                counts.add(len(kinds)); duplicate |= len(kinds) != len(set(kinds))
        check('real round starts generate all counts 0 through 3', counts == {0, 1, 2, 3})
        check('real random spawns allow duplicate types', duplicate)

        # Verify the remaining second game is reachable after removing Paddle.
        parse_states = False
        send('z'); wait_play(); shots('big_bump')
        check('Big Bump remains reachable and pairs as game two', True)
        send('m'); drain(.2); shots('launcher')
        parse_states = True
        send('v'); wait_play(); snapshot('final_normal_game'); shots('final_normal_game')
        check('left both devices in a fresh normal Throwhalla match', True)
    finally:
        (out / 'serial.log').write_text('\n'.join(logs), encoding='utf-8')
        (out / 'states.json').write_text(json.dumps(saved, indent=2), encoding='utf-8')
        (out / 'checks.json').write_text(json.dumps(checks, indent=2), encoding='utf-8')
        firmware = Path('.pio/build/debug-all/firmware.bin')
        (out / 'summary.json').write_text(json.dumps(dict(ports=ports, checks=len(checks), perf=perf,
            firmware_sha256=hashlib.sha256(firmware.read_bytes()).hexdigest()), indent=2), encoding='utf-8')
        for ser in sers:
            ser.write(b'qkn')  # release synthetic controls before returning devices to the user
            ser.close()
    print('ALL POWERUP HARDWARE CHECKS PASSED', flush=True)


if __name__ == '__main__': main()
