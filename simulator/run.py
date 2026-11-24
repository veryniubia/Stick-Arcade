"""Build the whole Stick Arcade application against the Windows StickS3 adapters."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / '.pio' / 'sticks3-simulator'


def build():
    if os.name != 'nt':
        raise SystemExit('This desktop backend currently requires Windows.')
    BUILD.mkdir(parents=True, exist_ok=True)
    font = ROOT / '.pio/libdeps/m5stack-sticks3/M5GFX/src'
    if not (font / 'lgfx/Fonts/glcdfont.h').exists():
        raise SystemExit('M5GFX font missing. Build the firmware once with PlatformIO first.')
    installs = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Microsoft Visual Studio'
    vcvars = next(installs.glob('*/*/VC/Auxiliary/Build/vcvars64.bat'), None)
    if not vcvars and not shutil.which('cl'):
        raise SystemExit('Install Visual Studio Build Tools with Desktop development with C++.')
    excluded = {'core/display.cpp', 'net/net_espnow.cpp'}
    sources = [str(p) for p in (ROOT / 'src').rglob('*.cpp')
               if p.relative_to(ROOT / 'src').as_posix() not in excluded]
    sources += [str(p) for p in (ROOT / 'simulator').glob('*.cpp')]
    exe = BUILD / 'sticks3-simulator.exe'
    cmd = ['cl', '/nologo', '/std:c++17', '/EHsc', '/O2', '/utf-8', '/DWIN32_LEAN_AND_MEAN',
           '/DNOMINMAX', '/DMIDDIFF_HOST_CANVAS', '/DSTICKS3_SIMULATOR',
           *['/I' + str(p) for p in [ROOT / 'simulator/include', ROOT / 'simulator', ROOT / 'src', font]],
           *sources, '/Fo:' + str(BUILD) + os.sep, '/Fe:' + str(exe),
           '/link', 'user32.lib', 'gdi32.lib', 'comctl32.lib', 'winmm.lib', 'ws2_32.lib']
    lines = ['@echo off']
    if vcvars:
        lines.append(f'call "{vcvars}" >nul')
    lines += [subprocess.list2cmdline(cmd), 'if errorlevel 1 exit /b 1']
    batch = BUILD / 'build.cmd'
    batch.write_text('\n'.join(lines) + '\n')
    subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=ROOT, check=True)
    return exe


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--no-build', action='store_true')
    args, remaining = parser.parse_known_args()
    exe = BUILD / 'sticks3-simulator.exe' if args.no_build else build()
    if not args.build_only:
        subprocess.run([str(exe), *remaining], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
