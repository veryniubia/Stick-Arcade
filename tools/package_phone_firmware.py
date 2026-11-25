"""Package the four PlatformIO flash segments, leaving the NVS sectors untouched."""
import hashlib
import os
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT/".pio/build/m5stack-sticks3"
CORE = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home()/".platformio"))
PACKAGES = Path(os.environ.get("PLATFORMIO_PACKAGES_DIR", CORE/"packages"))
OUT = ROOT/"out/phone-link"
SEGMENTS = [(0,BUILD/"bootloader.bin"), (0x8000,BUILD/"partitions.bin"),
            (0xe000,PACKAGES/"framework-arduinoespressif32/tools/partitions/boot_app0.bin"),
            (0x10000,BUILD/"firmware.bin")]


def main():
    OUT.mkdir(exist_ok=True,parents=True)
    final = OUT/"stick-arcade-sticks3.zip"
    temp = final.with_suffix(".tmp")
    with zipfile.ZipFile(temp,"w",zipfile.ZIP_DEFLATED) as z:
        hashes = []
        for offset,path in SEGMENTS:
            data = path.read_bytes()
            if not data or (offset<0xe000 and offset+len(data)>0x9000):
                raise SystemExit(f"Invalid flash segment: {path}")
            z.writestr(path.name,data)
            hashes.append(f"{hashlib.sha256(data).hexdigest()}  {path.name}")
        z.writestr("sha256.txt","\n".join(hashes)+"\n")
        z.writestr("flash_args.txt","\n".join(f"0x{offset:x} {p.name}" for offset,p in SEGMENTS)+"\n")
        z.writestr("README.txt",
                   "Stick Arcade - ESP32-S3 StickS3, 8 MB. Extract all four .bin files.\n"
                   "Run from this directory with esptool 4.9 (replace COM9 with your StickS3 port):\n"
                   "python -m esptool --chip esp32s3 --port COM9 write_flash @flash_args.txt\n"
                   "Use the four addresses in flash_args.txt. NVS is at 0x9000..0xdfff.\n")
    temp.replace(final)
    print(final)


if __name__=="__main__":
    main()
