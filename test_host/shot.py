"""Dev helper: drive a DEBUG_REMOTE build over serial and save screenshots.

usage: python shot.py COM9 out_prefix step [step ...]
Steps are sent in order. A step is either
  <key>[:<seconds>]   send the key then wait. Keys: v/V open game 0/1 (waiting screen), g/G open and go solo,
                      a/o press A/B, x shake twice (opens EXIT?), m launcher, f charge, j jump, i/1-4 Throwhalla items
  @<name>             take a screenshot saved as <out_prefix>_<name>.png
"""
import sys
import time

import serial
from PIL import Image

W, H = 240, 135


def grab(ser, path):
    ser.reset_input_buffer()
    ser.write(b"s")
    deadline = time.time() + 5
    buf = b""
    while time.time() < deadline:
        buf += ser.read(4096)
        i = buf.find(b"SHOT")
        if i >= 0 and len(buf) - i - 4 >= W * H * 2:
            raw = buf[i + 4 : i + 4 + W * H * 2]
            break
    else:
        raise SystemExit("no screenshot received")
    import re
    sizes = re.findall(rb"SIZE (\d+) (\d+)\r?\n", buf[:i])
    width, height = map(int, sizes[-1]) if sizes else (W, H)
    img = Image.new("RGB", (width, height))
    px = img.load()
    for y in range(height):
        for x in range(width):
            v = (raw[(y * width + x) * 2] << 8) | raw[(y * width + x) * 2 + 1]
            px[x, y] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    img.resize((width * 4, height * 4), Image.NEAREST).save(path)
    print("saved", path)


def main():
    port, prefix, steps = sys.argv[1], sys.argv[2], sys.argv[3:]
    ser = serial.Serial(port, 115200, timeout=0.03)
    ser.dtr = False
    ser.rts = False
    time.sleep(0.3)
    for step in steps:
        if step.startswith("@"):
            grab(ser, f"{prefix}_{step[1:]}.png")
            continue
        key, _, delay = step.partition(":")
        ser.write(key.encode())
        time.sleep(float(delay) if delay else 0.2)


if __name__ == "__main__":
    main()
