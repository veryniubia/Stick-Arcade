"""Compile the real game view against a tiny host canvas; export PNGs and a contact sheet.

Requires the existing PlatformIO M5GFX dependency (font only), Pillow, and C++17.
Run: python test_host/preview_samuraiduel.py
Outputs: .pio/samuraiduel-preview/ (no device required).
"""
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]

def main():
    os.chdir(ROOT)
    out = ROOT / '.pio' / 'samuraiduel-preview'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / ('preview.exe' if os.name == 'nt' else 'preview')
    includes = ['test_host/view_support', 'src', '.pio/libdeps/m5stack-sticks3/M5GFX/src']
    sources = ['test_host/samuraiduel_preview.cpp', 'src/games/samuraiduel/samuraiduel.cpp',
               'src/games/samuraiduel/samuraiduel_view.cpp', 'src/ui/feedback.cpp']
    installs = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Microsoft Visual Studio'
    vcvars = next(installs.glob('*/*/VC/Auxiliary/Build/vcvars64.bat'), None) if os.name == 'nt' else None
    if vcvars or shutil.which('cl'):
        cmd = ['cl', '/nologo', '/std:c++17', '/EHsc', '/O2', *['/I'+p for p in includes],
               *sources, '/Fo:'+str(out)+os.sep, '/Fe:'+str(exe)]
        batch = out / 'build.cmd'
        lines = ['@echo off']
        if vcvars: lines.append(f'call "{vcvars}" >nul')
        lines.append(subprocess.list2cmdline(cmd))
        batch.write_text('\n'.join(lines)+'\n')
        subprocess.run(['cmd', '/d', '/c', str(batch)], check=True)
    else:
        subprocess.run(['g++', '-std=c++17', '-O2', *['-I'+p for p in includes], *sources, '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(out)], check=True)
    names = ['arena', 'platforms', 'parry', 'air_shot', 'solo', 'online_ping', 'point', 'match_end', 'lobby', 'card']
    sheet = Image.new('RGB', (3*500, 4*300), '#101728')
    draw = ImageDraw.Draw(sheet)
    for index, name in enumerate(names):
        frame = Image.open(out / (name+'.ppm'))
        frame.save(out / (name+'.png'))
        tile = frame.resize((480,270), Image.Resampling.NEAREST)
        x, y = (index%3)*500+10, (index//3)*300+23
        sheet.paste(tile,(x,y))
        draw.text((x,y-17),name,fill='#e7e6c2')
    sheet.save(out / 'contact-sheet.png')
    swings = Image.new('RGB', (4*224, 2*148), '#101728')
    labels = ImageDraw.Draw(swings)
    phases = ['Windup', 'Sweep', 'Crescent', 'Recovery']
    for row, (side, x) in enumerate([('right', 67), ('left', 173)]):
        for col, phase in enumerate(phases):
            frame = Image.open(out / f'slash_{side}_{col}.ppm')
            crop = frame.crop((x-24, 75, x+24, 108)).resize((192, 132), Image.Resampling.NEAREST)
            swings.paste(crop, (col*224+16, row*148+16))
            labels.text((col*224+16, row*148+1), f'{side.title()} / {phase}', fill='#e7e6c2')
    swings.save(out / 'slash-animation.png')
    Image.open(out/'arena.png').resize((960,540),Image.Resampling.NEAREST).save(out/'arena-4x.png')
    print(out / 'contact-sheet.png')

if __name__ == '__main__': main()
