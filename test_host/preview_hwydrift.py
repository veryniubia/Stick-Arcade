"""Compile the real HwyDrift simulation and view against a tiny host canvas; export PNGs and a contact sheet.

Requires the existing PlatformIO M5GFX dependency (font only), Pillow, and C++17.
Run: python test_host/preview_hwydrift.py
Outputs: .pio/hwydrift-preview/ (no device required).
"""
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]


def main():
    os.chdir(ROOT)
    out = ROOT / '.pio' / 'hwydrift-preview'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / ('preview.exe' if os.name == 'nt' else 'preview')
    includes = ['test_host/view_support', 'src', '.pio/libdeps/m5stack-sticks3/M5GFX/src']
    sources = ['test_host/hwydrift_preview.cpp', 'src/games/hwydrift/hwydrift.cpp',
               'src/games/hwydrift/hwydrift_view.cpp', 'src/games/hwydrift/course.cpp']
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
    for ppm in out.glob('*.ppm'):
        Image.open(ppm).save(ppm.with_suffix('.png'))
    runs = sorted(out.glob('run_*.ppm'), key=lambda p: int(p.stem.split('_')[1]))
    wanted = runs[:30]
    sheet = Image.new('RGB', (10 * 280, 3 * 500), '#101728')
    draw = ImageDraw.Draw(sheet)
    for index, path in enumerate(wanted):
        tile = Image.open(path).resize((270, 480), Image.Resampling.NEAREST)
        x, y = (index % 10) * 280 + 5, (index // 10) * 500 + 16
        sheet.paste(tile, (x, y))
        draw.text((x, y - 14), path.stem, fill='#e7e6c2')
    sheet.save(out / 'contact-sheet.png')
    for name in ('card_1200', 'card_1700', 'lobby'):
        Image.open(out / f'{name}.ppm').resize((960, 540), Image.Resampling.NEAREST).save(out / f'{name}-4x.png')
    print(out)


if __name__ == '__main__':
    main()
