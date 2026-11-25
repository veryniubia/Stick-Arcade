"""Build and verify a locally signed arm64 Android APK with installed SDK tools."""
import concurrent.futures
import os
from pathlib import Path
import re

import subprocess
import tempfile
import zipfile

ANDROID = Path(__file__).resolve().parent
ROOT = ANDROID.parent
OUT = ROOT / "out" / "phone-link"
API = 26


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def native(ndk, work):
    hosts = list((ndk / "toolchains/llvm/prebuilt").glob("*"))
    toolbin = hosts[0] / "bin"
    ext = ".exe" if os.name == "nt" else ""
    clang = toolbin / ("clang++" + ext)
    flags = ["--target=aarch64-linux-android26", "-std=c++17", "-O2", "-fPIC",
             "-ffunction-sections", "-fdata-sections", "-fvisibility=hidden",
             "-DMIDDIFF_HOST_CANVAS", "-Wall", "-Werror=return-type"]
    includes = [ROOT/"src", ROOT/"simulator/include", ROOT/"simulator", ANDROID/"font"]
    flags += ["-I" + str(p) for p in includes]
    exclude = {ROOT/"src/core/display.cpp", ROOT/"src/net/net_espnow.cpp", ROOT/"src/net/arcade_radio.cpp"}
    sources = sorted(set((ROOT/"src").rglob("*.cpp")) - exclude)
    sources += [ROOT/"simulator/display.cpp", *sorted((ANDROID/"native").glob("*.cpp"))]

    def compile_one(source):
        obj = work / ("_".join(source.relative_to(ROOT).parts) + ".o")
        run(clang, *flags, "-c", source, "-o", obj)
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=min(os.cpu_count() or 1, 8)) as pool:
        objects = list(pool.map(compile_one, sources))
    lib = work / "libstickarcade.so"
    run(clang, flags[0], "-shared", "-static-libstdc++", "-Wl,-z,max-page-size=16384",
        "-Wl,--gc-sections,--no-undefined,-soname,libstickarcade.so", *objects,
        "-landroid", "-laaudio", "-ljnigraphics", "-o", lib)
    run(toolbin / ("llvm-strip" + ext), "--strip-unneeded", lib)
    return lib


def build():
    sdk = Path(os.environ["ANDROID_HOME"])
    ndk = Path(os.environ["ANDROID_NDK_HOME"])
    jdk = Path(os.environ["JAVA_HOME"]) / "bin"
    tools = sdk / "build-tools/35.0.1"
    jar = sdk / "platforms/android-35/android.jar"
    key = Path(os.environ.get("STICK_KEYSTORE", Path.home()/".android/debug.keystore"))
    if not key.is_file():
        raise SystemExit("Signing key missing. See android/README.md before generating a new identity.")
    # Default only for the conventional local Android debug key.
    password = "env:STICK_STORE_PASS" if "STICK_STORE_PASS" in os.environ else "pass:android"
    keypass = "env:STICK_KEY_PASS" if "STICK_KEY_PASS" in os.environ else password
    alias = os.environ.get("STICK_KEY_ALIAS", "androiddebugkey")
    OUT.mkdir(exist_ok=True, parents=True)
    with tempfile.TemporaryDirectory(dir=OUT) as temp:
        work = Path(temp)
        lib = native(ndk,work)
        classes = work / "classes"
        classes.mkdir()
        run(jdk/"javac", "-encoding","UTF-8", "--release","8","-classpath",jar,
            "-d",classes,*sorted((ANDROID/"java").rglob("*.java")))
        bytecode = work/"app.jar"
        run(jdk/"jar", "cf",bytecode,"-C",classes,".")
        run(jdk/"java","-cp",tools/"lib/d8.jar","com.android.tools.r8.D8","--release",
            "--min-api",API,"--lib",jar,"--output",work,bytecode)
        unsigned = work/"unsigned.apk"
        run(tools/"aapt2","link","-I",jar,"--manifest",ANDROID/"AndroidManifest.xml","-o",unsigned)
        with zipfile.ZipFile(unsigned,"a") as apk:
            apk.write(work/"classes.dex","classes.dex",zipfile.ZIP_DEFLATED)
            apk.write(lib,"lib/arm64-v8a/libstickarcade.so",zipfile.ZIP_STORED)
            apk.write(ANDROID/"font/lgfx/Fonts/glcdfont.h","assets/NOTICE-font.txt",zipfile.ZIP_DEFLATED)
        aligned = work/"aligned.apk"
        run(tools/"zipalign","-P","16","-f","4",unsigned,aligned)
        signed = work/"player.apk"
        signer = [jdk/"java","-jar",tools/"lib/apksigner.jar"]
        run(*signer,"sign","--ks",key,"--ks-key-alias",alias,"--ks-pass",password,
            "--key-pass",keypass,"--out",signed,aligned)
        report = subprocess.check_output([*signer,"verify","--verbose","--print-certs",signed],
                                         text=True, encoding="utf-8", errors="replace")
        print(report)
        digest = re.search(r"Signer #1 certificate SHA-256 digest: ([0-9a-f]+)", report)
        expected = (ANDROID/"signing-cert.sha256").read_text().strip()
        if not digest or digest.group(1) != expected:
            raise SystemExit("Signing identity differs from the installed player. Restore its original keystore.")
        run(tools/"zipalign","-c","-P","16","4",signed)
        dest = OUT/"stick-arcade-player.apk"
        os.replace(signed,dest)
        print(dest)


if __name__ == "__main__":
    build()
