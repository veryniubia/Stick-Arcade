"""Run the production BSD socket transport integration test on an Android test emulator."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--adb", default="adb")
    p.add_argument("--serial", required=True)
    args = p.parse_args()
    adb = [args.adb, "-s", args.serial]
    abi = subprocess.check_output([*adb,"shell","getprop","ro.product.cpu.abi"],text=True).strip()
    target = {"x86_64":"x86_64", "arm64-v8a":"aarch64"}[abi]
    tools = next((Path(os.environ["ANDROID_NDK_HOME"])/"toolchains/llvm/prebuilt").iterdir())/"bin"
    exe = ROOT/"out/phone-udp-test"
    sources = ["test_host/phone_udp_test.cpp","src/core/game_instance.cpp","src/net/net_session.cpp",
               "src/games/throwhalla/game.cpp","src/games/throwhalla/throwhalla.cpp",
               "src/games/bumperduel/bumperduel.cpp","src/games/samuraiduel/samuraiduel.cpp",
               "src/games/middiff/middiff.cpp"]
    exe.parent.mkdir(exist_ok=True)
    subprocess.run([tools/"clang++",f"--target={target}-linux-android26","-std=c++17","-O2",
                    "-static-libstdc++","-Isrc",*sources,"-o",exe],check=True,cwd=ROOT)
    remote = "/data/local/tmp/stick-arcade-phone-udp-test"
    subprocess.run([*adb,"push",exe,remote],check=True)
    subprocess.run([*adb,"shell","chmod","700",remote],check=True)
    subprocess.run([*adb,"shell",remote],check=True)


if __name__=="__main__":
    main()
