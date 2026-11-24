"""Exercise a connected DEBUG_REMOTE build and capture its actual framebuffer.

Enter MoonDuel solo first, then run: python test_host/samuraiduel_check.py COM6 out/moonduel
Debug fixtures only prepare starting states; every action/collision is the real game loop.
"""
import json
from pathlib import Path
import re
import struct
import sys
import time

from PIL import Image
import serial


def decode(raw):
    assert len(raw) == 134, len(raw)
    seed, phase, winner, round_winner, solo, sword, s0, s1 = struct.unpack_from('<IBbbBBII', raw)
    elapsed, round_time, timer = struct.unpack_from('<fHH', raw, 17)
    result = dict(seed=seed, phase=phase, winner=winner, round_winner=round_winner,
                  solo=bool(solo), sword=sword, score=[s0, s1], elapsed=elapsed,
                  round_time=round_time/1000, timer=timer/1000, fighters=[], bullets=[])
    for i in range(2):
        x, y, vx, vy, cooldown, action, recoil, bits, shot, parry, down_time = struct.unpack_from('<hhhhHHBBBBH', raw, 31+i*18)
        result['fighters'].append(dict(x=x/64, y=y/64, vx=vx/64, vy=vy/64,
            cooldown=cooldown/1000, action=action/1000, recoil=recoil/250,
            facing=1 if bits & 1 else -1, grounded=bool(bits & 2), thrust=bool(bits & 8),
            down=bool(bits & 16), shot=shot/250, parry=parry/250, down_time=down_time/1000))
    for i in range(12):
        bits, x, y = struct.unpack_from('<Bhh', raw, 67+i*5)
        result['bullets'].append(dict(x=x/32, y=y/32, active=bool(bits & 1),
            reflected=bool(bits & 2), direction=1 if bits & 4 else -1, owner=(bits >> 3) & 1))
    result['boost'] = list(raw[-2:])
    return result


def main():
    port, destination = sys.argv[1:3]
    out = Path(destination); out.mkdir(parents=True, exist_ok=True)
    s = serial.Serial(); s.port=port; s.baudrate=115200; s.timeout=.005
    s.dtr=False; s.rts=False; s.open()
    checks, states, logs = [], {}, []

    def send(keys, delay=0):
        s.write(keys.encode()); s.flush()
        if delay: time.sleep(delay)

    def state(label=None):
        s.reset_input_buffer(); send('d')
        data=b''; deadline=time.monotonic()+1
        while time.monotonic()<deadline:
            data+=s.read(4096)
            match=re.search(rb'STATE ([0-9a-f]+)\r?\n', data)
            if match:
                value=decode(bytes.fromhex(match[1].decode()))
                if label: states[label]=value
                return value
        raise AssertionError(('no state', data[-200:]))

    def check(label, condition):
        assert condition, label
        checks.append(label); print('PASS',label,flush=True)

    def screenshot(label):
        s.reset_input_buffer(); send('s'); data=b''; deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            data+=s.read(8192); start=data.find(b'SHOT')
            if start>=0 and len(data)>=start+4+240*135*2: break
        else: raise AssertionError('no framebuffer')
        raw=data[start+4:start+4+240*135*2]
        rgb=bytearray()
        for value, in struct.iter_unpack('>H',raw):
            rgb.extend((((value>>11)&31)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))
        frame=Image.frombytes('RGB',(240,135),bytes(rgb))
        frame.save(out/f'{label}-native.png')
        frame.resize((960,540),Image.Resampling.NEAREST).save(out/f'{label}.png')
        print('SCREENSHOT',label,flush=True)

    try:
        send('q=1',.12); screenshot('idle')
        send('5',.07); check('real burger pickup',state('pickup')['boost'][0]==2)
        send('A',.13); screenshot('thrust')
        st=state('thrust'); check('forward thrust active',st['fighters'][0]['thrust'] and st['fighters'][0]['x']>90)

        send('2',.08); send('5',.06); send('A',.18); screenshot('gunfire')
        st=state('gunfire');check('muzzle effect follows actual shot',st['fighters'][0]['shot']>0)
        send('2',.06);send('5',.06);send('A')
        count=0
        deadline=time.monotonic()+.53
        while time.monotonic()<deadline:
            st=state();count=max(count,sum(b['active'] for b in st['bullets']))
            check_neutral=st['fighters'][1]['action']==0
            assert check_neutral,'solo opponent gained input'
            time.sleep(.012)
        states['burst']=st
        check('three identical shots on device',count==3)
        check('solo opponent stays neutral',check_neutral)

        send('6A'); reflected=0;deadline=time.monotonic()+.62
        while time.monotonic()<deadline:
            st=state();reflected=max(reflected,sum(b['active'] and b['reflected'] for b in st['bullets']))
            time.sleep(.008)
        states['parries']=st;check('one thrust reflects all three on device',reflected==3)

        send('3',.52);screenshot('double-ko');st=state('double_ko')
        check('both fighters down and scored',st['score']==[1,1] and all(f['down'] for f in st['fighters']))
        send('A]j',.15);st=state('dead_input')
        check('fallen fighters cannot attack or jump',all(f['action']==0 and f['grounded'] for f in st['fighters']))
        send('=',1.5);st=state('reset')
        check('round resets after death display',st['phase']==0 and not any(f['down'] for f in st['fighters']))

        send('4',.15);st=state('provisional_draw')
        check('match point remains provisional',st['score']==[5,5] and st['phase']==1)
        time.sleep(1.55);screenshot('draw');st=state('draw')
        check('five-five finishes as draw',st['phase']==2 and st['winner']==-1)
        send('1',.08);send('5',.06)
        # Capture steady play statistics, excluding framebuffer transmission stalls.
        s.reset_input_buffer();deadline=time.monotonic()+2.3
        while time.monotonic()<deadline:
            logs.append(s.read(4096).decode(errors='replace'))
        text=''.join(logs);(out/'performance.log').write_text(text,encoding='utf-8')
        samples=[int(v) for v in re.findall(r'PERF scr=2 fps=(\d+)',text)]
        check('steady gameplay reaches 59 fps',bool(samples) and max(samples)>=59)
        send('1') # Leave the device in a normal unboosted practice scene.
    finally:
        s.close()
        (out/'checks.json').write_text(json.dumps(dict(checks=checks,states=states),indent=2),encoding='utf-8')


if __name__ == '__main__': main()
