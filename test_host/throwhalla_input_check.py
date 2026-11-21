"""Check live IMU handoff and capture rune/obstacle layering on one real board."""
import json
import re
import sys
import time
from pathlib import Path

import serial
from net_check import grab
from throwhalla_check import decode


def main():
    port = sys.argv[1]
    out = Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
    ser = serial.Serial(port, 115200, timeout=.002)
    ser.dtr = False; ser.rts = False
    buf = b''; log = []; diag = {}; state = {}; saved = {}; checks = []

    def drain(seconds):
        nonlocal buf, diag, state
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            buf += ser.read(ser.in_waiting or 1)
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                text = line.decode(errors='replace').strip()
                if text.startswith(('IMU ', 'STATE ', 'NET ', 'PERF ')): log.append(text)
                if text.startswith('IMU '): diag = {k: float(v) for k, v in re.findall(r'(\w+)=(-?[\d.]+)', text)}
                if text.startswith('STATE '): state = decode(bytes.fromhex(text[6:]))

    def query(label, keys='I'):
        nonlocal diag, state
        diag = {}
        if 'd' in keys: state = {}
        ser.write(keys.encode()); drain(.12)
        deadline = time.monotonic() + 1.5
        while (not diag or ('d' in keys and not state)) and time.monotonic() < deadline:
            drain(.03)
        assert diag and ('d' not in keys or state), (label, log[-8:])
        saved[label] = dict(imu=dict(diag), state=state)
        return diag

    def check(label, ok):
        assert ok, (label, diag, state)
        checks.append(label); print('PASS', port, label, flush=True)

    def shot(label):
        nonlocal buf
        grab(ser, str(out / f'{label}_{port}.png')); buf = b''

    try:
        ser.write(b'qkng'); drain(.4)
        query('startup')
        check('IMU enabled with no synthetic aim', diag['ok'] == 1 and diag['override'] == 0)
        query('forced_neutral', '=I')
        check('reproduced the old forced neutral failure', diag['override'] == 1 and diag['aim'] == 0)
        query('exit_releases_aim', 'mI')
        check('leaving a game restores IMU', diag['override'] == 0)
        ser.write(b'=g'); drain(.4); query('entry_releases_aim')
        check('entering a game restores IMU', diag['override'] == 0)
        for i in range(8):
            query(f'live_sensor_{i}', 'Id')
            expected = max(-30, min(180, diag['aim']))
            check(f'game aim follows live IMU sample {i}', abs(state['players'][0]['aim'] - expected) < 2)
            drain(.08)
        ser.write(b'0'); drain(.6); shot('icons_no_ring')
        ser.write(b'O'); drain(.3); shot('obstacles_over_icons')
        ser.write(b'g'); drain(.4); query('final_live', 'Id')
        check('handoff has live IMU and a normal arena', diag['override'] == 0 and state['world'] == 7)
    finally:
        ser.write(b'qkn'); ser.close()
        (out / f'{port}.json').write_text(json.dumps(dict(checks=checks, samples=saved), indent=2))
        (out / f'{port}.log').write_text('\n'.join(log))
    print('ALL IMU HANDOFF CHECKS PASSED', port, flush=True)


if __name__ == '__main__': main()
