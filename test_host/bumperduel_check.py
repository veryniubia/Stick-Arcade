"""Real ESP-NOW and framebuffer checks for the inflatable sumo rules (protocol 5)."""
import json
import math
import re
from pathlib import Path
import struct
import sys
import time
import serial
from net_check import grab

def decode(raw):
    assert len(raw)==84,len(raw)
    seed,phase,winner,flags,elapsed,s0,s1,flash=struct.unpack_from('<IBbBfBBB',raw)
    result=dict(seed=seed,phase=phase,winner=winner,solo=bool(flags&1),swapped=bool(flags&2),elapsed=elapsed,score=[s0,s1],flash=flash/100,cars=[])
    for i in range(2):
        x,y,vx,vy,angle,protect,cooldown,pulse,recoil,control,struck=struct.unpack_from('<hhhhhBHBBBB',raw,14+i*24)
        burger,shoes,shield,slide=struct.unpack_from('<HHHB',raw,31+i*24)
        result['cars'].append(dict(burger=burger/1000,shoes=shoes/1000,shield=shield/1000,slide=slide/250,x=x/64,y=y/64,vx=vx/64,vy=vy/64,angle=angle/8192,protect=protect/100,cooldown=cooldown/1000,pulse=pulse/250,recoil=recoil/250,control=control,struck=bool(struck)))
    result['items']=[]
    for i in range(2):
        x,y,kind,warning,ttl=struct.unpack_from('<hhBBH',raw,68+i*8)
        result['items'].append(dict(x=x/64,y=y/64,type=kind,warning=warning/250,ttl=ttl/1000))
    return result

def main():
    ports=sys.argv[1:3];out=Path(sys.argv[3]);out.mkdir(parents=True,exist_ok=True)
    sers=[serial.Serial(p,115200,timeout=.002) for p in ports]
    bufs=[b'',b''];logs=[];latest={};states={};saved={};checks=[];perf={}
    def drain(seconds):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            for i,s in enumerate(sers):
                bufs[i]+=s.read(s.in_waiting or 1)
                while b'\n' in bufs[i]:
                    line,bufs[i]=bufs[i].split(b'\n',1);text=line.decode(errors='replace').strip()
                    if text.startswith(('NET ','PERF ','RADIO ','STATE ')):logs.append(f'[{ports[i]}] {text}')
                    if text.startswith('NET '):latest[i]=text
                    if text.startswith('PERF '):perf[i]={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',text)}
                    if text.startswith('STATE '):states[i]=decode(bytes.fromhex(text[6:]))
    def send(keys,who=None):
        for i in (range(2) if who is None else [who]):sers[i].write(keys.encode())
    def snapshot(label,who=None):
        states.clear();send('d',who);drain(.1)
        required=2 if who is None else 1
        deadline=time.monotonic()+.5
        while len(states)<required and time.monotonic()<deadline:drain(.01)
        assert len(states)==required,(label,latest,states)
        result=json.loads(json.dumps(states));saved[label]=result
        return dict(states)
    def check(label,ok):
        assert ok,(label,states,latest)
        checks.append(label);print('PASS',label,flush=True)
    def shots(label,who=None):
        for i in (range(2) if who is None else [who]):grab(sers[i],str(out/f'{label}_{ports[i]}.png'))
        bufs[0]=bufs[1]=b'';drain(1)
    def wait_play():
        for _ in range(12):
            drain(.6)
            if all('st=PLAY' in latest.get(i,'') for i in range(2)):return
        raise AssertionError(('pairing',latest))
    try:
        for s in sers:s.dtr=False;s.rts=False
        drain(1);send('mq=k');drain(.2);send('z');drain(2);shots('countdown');wait_play();drain(1)
        host=next(i for i in range(2) if 'role=1 ' in latest[i]);client=1-host
        saved['roles']=dict(host=ports[host],client=ports[client]);print('HOST',ports[host],flush=True)
        start=snapshot('start');check('pairing and same seed',start[host]['seed']==start[client]['seed']);shots('arena')
        drain(4.2);st=snapshot('random_items')
        check('random items spawn and synchronize',any(item['type'] for item in st[host]['items']) and all((a['type'],a['x'],a['y'])==(b['type'],b['x'],b['y']) and abs(a['ttl']-b['ttl'])<.1 for a,b in zip(st[host]['items'],st[client]['items'])))
        shots('items')
        send(']',client);drain(.16);st=snapshot('angle');c=st[host]['cars'][1]
        base=(math.pi/2 if st[host]['swapped'] else -math.pi/2)
        expected=math.atan2(math.sin(base+25*math.pi/45),math.cos(base+25*math.pi/45))
        check('client absolute tilt reaches authority',abs(c['angle']-expected)<.01)
        drain(.5);st=snapshot('angle_held');check('holding tilt holds heading',abs(st[host]['cars'][1]['angle']-expected)<.01)
        for key,angle in [('u',90),('b',180)]:
            send(key,client);drain(.16);st=snapshot('full_angle_'+key)
            expected=math.atan2(math.sin(base+angle*math.pi/45),math.cos(base+angle*math.pi/45))
            check('full angle '+key+' is periodic rather than clamped',abs(math.atan2(math.sin(st[host]['cars'][1]['angle']-expected),math.cos(st[host]['cars'][1]['angle']-expected)))<.01)
        send('=j',client);drain(.16);st=snapshot('client_burst');check('client B triggers pulse and cooldown',st[host]['cars'][1]['pulse']>0 and st[host]['cars'][1]['cooldown']>2.5)
        send('j',client);drain(.25);st=snapshot('cooldown_reject');check('cooldown rejects second pulse',st[host]['cars'][1]['cooldown']<2.7)
        send('q=');send('h',host);drain(.1);st=snapshot('passive');check('body contact gives no score',st[host]['score']==[0,0])
        send('t',host);drain(.14);st=snapshot('burst');check('burst knocks radially without moving inflated attacker',st[host]['cars'][1]['vx']>75 and st[host]['cars'][0]['vx']<5 and abs(st[host]['cars'][0]['x']-57)<.03);shots('burst')
        send('q=');send('p',host);drain(.14);st=snapshot('mutual');check('simultaneous bursts push both',st[host]['cars'][0]['vx']< -75 and st[host]['cars'][1]['vx']>75)
        send('c',host);drain(.15);st=snapshot('injury');check('spike gives opponent point and protection',st[host]['score']==[1,0] and st[host]['cars'][1]['protect']>0)
        check('spike rebounds inward',st[host]['cars'][1]['vx']< -40);drain(.2);st=snapshot('injury_sync');check('injury score converges',st[client]['score']==[1,0]);shots('injury')
        send('q=');send('5',host);drain(.12);st=snapshot('burger')
        check('hamburger picked up on both devices',all(v['cars'][0]['burger']>5.5 for v in st.values()))
        send('j',host);drain(.16);shots('hamburger',host)
        send('6',host);drain(.12);send('e',host);drain(.2);st=snapshot('shoes')
        check('shoes increase real forward speed',st[host]['cars'][0]['shoes']>5 and math.hypot(st[host]['cars'][0]['vx'],st[host]['cars'][0]['vy'])>65)
        send('q');send('7',host);drain(.12);st=snapshot('shield')
        check('shield pickup synchronized',all(v['cars'][0]['shield']>5.5 for v in st.values()))
        score=st[host]['score'];send('e',host);drain(.8);send('q',host);st=snapshot('shield_block')
        check('shield prevents one real spike point',st[host]['cars'][0]['shield']==0 and st[host]['score']==score)
        send('8',host);drain(.035);st=snapshot('banana')
        check('banana launches in facing direction',st[host]['cars'][0]['slide']>0 and st[host]['cars'][0]['vy']>100)
        drain(.2);st=snapshot('banana_sync');check('banana pickup converges',st[client]['items'][0]['type']==0)
        send('9',host);drain(.05);shots('max_buffs',host)
        # Refresh the real fixture during several full PERF windows to hold both bodies
        # at maximum hamburger inflation with all icons/auras and two visible map items.
        for _ in range(55):send('9',host);drain(.12)
        saved['max_buffs_perf']=dict(perf)
        check('60 fps with both maximum bodies and all buffs',all(perf[i].get('fps',0)>=59 for i in range(2)))
        send('h',host);send('e',client);drain(.18);st=snapshot('move');moving=st[host]['cars'][1];check('A moves forward',math.hypot(moving['vx'],moving['vy'])>40)
        send('q',client);drain(.25);st=snapshot('stop');check('release rapidly slows',math.hypot(st[host]['cars'][1]['vx'],st[host]['cars'][1]['vy'])<8)
        send('l');send('j',client);drain(.3);st=snapshot('loss_burst');check('B edge survives packet loss',st[host]['cars'][1]['cooldown']>2.3)
        drain(2);check('20 percent loss still plays',all('st=PLAY' in latest.get(i,'') for i in range(2)));send('kq=')
        send('L')
        # NET diagnostics are periodic; wait for both reports rather than asserting
        # on a potentially stale PLAY line just after the link timeout.
        for _ in range(7):
            drain(.35)
            if all('st=LOST' in latest.get(i,'') for i in range(2)):break
        st=snapshot('lost');check('link pauses both',all('st=LOST' in latest.get(i,'') for i in range(2)))
        elapsed=st[host]['elapsed'];drain(.3);st=snapshot('paused');check('authority frozen during link loss',st[host]['elapsed']==elapsed);send('k');wait_play()
        send('w',host);drain(.3);st=snapshot('win');check('five points wins and synchronizes',all(v['phase']==2 and v['winner']==0 and v['score']==[5,0] for v in st.values()));shots('win')
        swapped=st[host]['swapped'];send('a',host);drain(.3);check('one AGAIN waits',all(v['phase']==2 for v in snapshot('one_again').values()))
        send('a',client);drain(4.5);wait_play();st=snapshot('rematch');check('rematch swaps starts and clears scores',st[host]['swapped']!=swapped and st[host]['score']==[0,0] and st[host]['seed']==st[client]['seed'])
        send('y',host);drain(.3);check('same tick spike damage draws at five',all(v['phase']==2 and v['winner']==-1 and v['score']==[5,5] for v in snapshot('draw').values()));shots('draw')
        send('a');drain(4.5);wait_play();drain(8);shots('playing')
        send('x',client);drain(.2);send('a',client);drain(1.3);check('exit notifies peer',any('st=LEFT' in x for x in logs[-30:]));send('m');drain(.4);shots('launcher')
        send('Z',host);drain(.5);st=snapshot('solo_start',host);bot=st[host]['cars'][1];check('solo entered',st[host]['solo']);drain(3)
        st=snapshot('solo_idle',host);check('solo opponent has no input and stays still',all(st[host]['cars'][1][key]==bot[key] for key in ['x','y','angle']) and st[host]['cars'][1]['control']==0)
        send('t',host);drain(.16);st=snapshot('solo_pushed',host);check('idle opponent can be knocked away',st[host]['cars'][1]['vx']>75);shots('solo',host)
        send('w',host);drain(.3);send('a',host);drain(.4);st=snapshot('solo_again',host);check('solo AGAIN resets match',st[host]['score']==[0,0] and st[host]['phase']==0)
        send('w',host);drain(.3);send('z',client);drain(1.5);send('a',host);drain(6);wait_play();check('solo can join waiting peer',all('stage=2' in latest.get(i,'') for i in range(2)))
        send('q=k');send('h',host);drain(.3);shots('final_arena');drain(6)
        for _ in range(6):
            send('m');drain(.15);send('Z');drain(.2)
        send('m');drain(.2);send('z');drain(5);wait_play();drain(6)
        saved['final_perf']=dict(perf)
        check('DMA retained after repeated orientation switches',all(perf[i].get('dma')==1 for i in range(2)))
        check('60 fps retained after repeated orientation switches',all(perf[i].get('fps',0)>=59 for i in range(2)))
        print('ALL INFLATABLE SUMO HARDWARE CHECKS PASSED',flush=True)
    finally:
        (out/'serial.log').write_text('\n'.join(logs),encoding='utf-8');(out/'states.json').write_text(json.dumps(saved,indent=2));(out/'checks.json').write_text(json.dumps(checks,indent=2))
        for s in sers:
            s.write(b'qkn')
            s.close()
if __name__=='__main__':main()
