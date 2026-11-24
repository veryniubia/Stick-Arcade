"""Compile the real MidDiff view against a tiny host canvas; export PNGs and a contact sheet.

Requires the existing PlatformIO M5GFX dependency (font only), Pillow, and C++17.
Run: python test_host/preview_middiff.py
Outputs: .pio/middiff-preview/ (no device required).
"""
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
NAMES = ['burger_pickup', 'burger_held', 'burger_throw', 'burger_flying', 'burger_blast', 'burger_smoke', 'start', 'far', 'scope', 'bolt', 'gate_closed', 'gate_open', 'gate_warn', 'gate_lifting', 'barrels', 'left', 'right', 'up', 'down',
         'team1_view', 'team1_down', 'foe_at_me', 'foe_left', 'foe_fires', 'foe_hurt', 'foe_hop', 'foe_falling', 'foe_down',
         'foe_protected', 'foe_other_team', 'shot', 'hurt', 'dead', 'respawn_flash', 'protected', 'score', 'match_end',
         'solo', 'lobby', 'card0', 'card1', 'card2']


def main():
    os.chdir(ROOT)
    out = ROOT / '.pio' / 'middiff-preview'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / ('preview.exe' if os.name == 'nt' else 'preview')
    includes = ['test_host/view_support', 'src', '.pio/libdeps/m5stack-sticks3/M5GFX/src']
    sources = ['test_host/middiff_preview.cpp', 'src/games/middiff/middiff.cpp',
               'src/games/middiff/middiff_view.cpp', 'src/ui/feedback.cpp']
    installs = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Microsoft Visual Studio'
    vcvars = next(installs.glob('*/*/VC/Auxiliary/Build/vcvars64.bat'), None) if os.name == 'nt' else None
    if vcvars or shutil.which('cl'):
        cmd = ['cl', '/nologo', '/std:c++17', '/EHsc', '/O2', *['/I' + p for p in includes],
               *sources, '/Fo:' + str(out) + os.sep, '/Fe:' + str(exe)]
        batch = out / 'build.cmd'
        lines = ['@echo off']
        if vcvars:
            lines.append(f'call "{vcvars}" >nul')
        lines.append(subprocess.list2cmdline(cmd))
        batch.write_text('\n'.join(lines) + '\n')
        subprocess.run(['cmd', '/d', '/c', str(batch)], check=True)
    else:
        subprocess.run(['g++', '-std=c++17', '-O2', *['-I' + p for p in includes], *sources, '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(out)], check=True)
    cols, rows = 3, (len(NAMES) + 2) // 3
    sheet = Image.new('RGB', (cols * 500, rows * 300), '#101728')
    draw = ImageDraw.Draw(sheet)
    for index, name in enumerate(NAMES):
        frame = Image.open(out / (name + '.ppm'))
        frame.save(out / (name + '.png'))
        tile = frame.resize((480, 270), Image.Resampling.NEAREST)
        x, y = (index % cols) * 500 + 10, (index // cols) * 300 + 23
        sheet.paste(tile, (x, y))
        draw.text((x, y - 17), name, fill='#e7e6c2')
    sheet.save(out / 'contact-sheet.png')
    # A compact review board and a nearest-neighbour enlarged animation from actual gameplay frames.
    stages = [('burger_held', 'HELD'), ('burger_throw', 'RELEASE'), ('burger_flying', 'TUMBLE'),
              ('burger_blast', 'EXPLOSION'), ('burger_smoke', 'SMOKE & FRAGMENTS'), ('burger_pickup', 'PICKUP')]
    board = Image.new('RGB', (1000, 912), '#181c20')
    pen = ImageDraw.Draw(board)
    for i, (name, label) in enumerate(stages):
        x, y = 10 + (i % 2) * 500, 28 + (i // 2) * 304
        board.paste(Image.open(out / (name + '.png')).resize((480,270),Image.Resampling.NEAREST),(x,y))
        pen.text((x,y-18),label,fill='#e6cf9c')
    board.save(out / 'burger-preview.png')
    frames = [Image.open(out / f'burger_anim_{i:03d}.ppm').resize((720,405),Image.Resampling.NEAREST) for i in range(105)]
    frames[0].save(out / 'burger-action.gif', save_all=True, append_images=frames[1:], duration=33, loop=0)
    print(out / 'contact-sheet.png')


if __name__ == '__main__':
    main()
