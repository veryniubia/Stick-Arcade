"""Build and run every host test with g++ or an installed MSVC (Windows).

Usage: python test_host/run_tests.py [--compiler g++|cl]
MSVC is preferred on Windows when found, including outside a developer prompt.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TESTS = {
    'game_test': ['src/games/throwhalla/game.cpp'],
    'core_test': ['src/core/game_instance.cpp',
                  'src/games/throwhalla/throwhalla.cpp', 'src/games/throwhalla/game.cpp'],
    'bumperduel_test': ['src/games/bumperduel/bumperduel.cpp'],
    'samuraiduel_test': ['src/games/samuraiduel/samuraiduel.cpp'],
    'net_test': ['src/net/net_session.cpp', 'src/core/game_instance.cpp',
                 'src/games/throwhalla/throwhalla.cpp', 'src/games/throwhalla/game.cpp',
                 'src/games/bumperduel/bumperduel.cpp',
                 'src/games/samuraiduel/samuraiduel.cpp'],
}


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--compiler', choices=['g++', 'cl'])
    args = parser.parse_args()
    os.chdir(ROOT)
    build = ROOT / '.pio' / 'host-tests'; build.mkdir(parents=True, exist_ok=True)
    vcvars = None
    if os.name == 'nt' and args.compiler != 'g++':
        installations = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Microsoft Visual Studio'
        vcvars = next(installations.glob('*/*/VC/Auxiliary/Build/vcvars64.bat'), None)
    compiler = args.compiler or ('cl' if vcvars or shutil.which('cl') else 'g++')
    lines = ['@echo off']
    if compiler == 'cl' and vcvars: lines.append(f'call "{vcvars}" >nul')
    for name, sources in TESTS.items():
        exe = build / (name + ('.exe' if os.name == 'nt' else ''))
        inputs = [f'test_host/{name}.cpp', *sources]
        if compiler == 'cl':
            cmd = ['cl', '/nologo', '/std:c++17', '/EHsc', '/O2', '/I', 'src', *inputs,
                   '/Fo:' + str(build) + os.sep, '/Fe:' + str(exe)]
            lines += [subprocess.list2cmdline(cmd), 'if errorlevel 1 exit /b 1', f'"{exe}"', 'if errorlevel 1 exit /b 1']
        else:
            subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-I', 'src', *inputs, '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
    if compiler == 'cl':
        batch = build / 'run.cmd'; batch.write_text('\n'.join(lines)+'\n')
        subprocess.run(['cmd', '/d', '/c', str(batch)], check=True)


if __name__ == '__main__': main()
