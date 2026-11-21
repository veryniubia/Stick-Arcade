"""Dev helper: drive two DEBUG_REMOTE devices over serial for online play checks.

usage: python net_check.py COM8 COM9 step [step ...]
Steps: v / V (open game 0 / game 1 on both devices: the waiting screen), g / G (same and go solo),
       p<seconds>:<regex> (fire and jump on both until every NET line matches <regex>),
       <index|*>:<keys> sends keys (0:a presses A on the first device, 0:x shakes it, which opens EXIT?),
       w<seconds> waits and prints NET/PERF/RADIO lines,
       @<prefix> saves screenshots of both devices. Re-opening a port resets the board, so keep one run per scenario.
"""
import re
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
        print("no screenshot", path)
        return
    import re
    sizes = re.findall(rb"SIZE (\d+) (\d+)\r?\n", buf[:i])
    width, height = map(int, sizes[-1]) if sizes else (W, H)
    img = Image.new("RGB", (width, height))
    px = img.load()
    for y in range(height):
        for x in range(width):
            v = (raw[(y * width + x) * 2] << 8) | raw[(y * width + x) * 2 + 1]
            px[x, y] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    img.resize((width * 3, height * 3), Image.NEAREST).save(path)
    print("saved", path)


latest = {}


def drain(sers, seconds):
    end = time.time() + seconds
    bufs = [b"" for _ in sers]
    while time.time() < end:
        for i, s in enumerate(sers):
            bufs[i] += s.read(4096)
            while b"\n" in bufs[i]:
                line, bufs[i] = bufs[i].split(b"\n", 1)
                text = line.decode(errors="replace").strip()
                if text.startswith(("NET", "FPS", "PERF", "RADIO")):
                    print(f"[{i}] {text}")
                if text.startswith("NET"):
                    latest[i] = text


def play_until(sers, seconds, needle):
    """Fire and jump on every device until all of them report `needle` in their NET line."""
    end = time.time() + seconds
    while time.time() < end:
        for s in sers:
            s.write(b"fj")
        drain(sers, 1.5)
        if all(re.search(needle, latest.get(i, "")) for i in range(len(sers))):
            return True
    print("timeout waiting for", needle)
    return False


def main():
    ports, steps = sys.argv[1:3], sys.argv[3:]
    sers = []
    for p in ports:
        s = serial.Serial(p, 115200, timeout=0.02)
        s.dtr = False
        s.rts = False
        sers.append(s)
    time.sleep(0.3)
    try:
        for step in steps:
            if step in ("v", "V", "g", "G"):
                for s in sers:
                    s.write(step.encode())
            elif step.startswith("p"):  # p<seconds>:<text>
                secs, _, needle = step[1:].partition(":")
                play_until(sers, float(secs), needle)
            elif step.startswith("w"):
                drain(sers, float(step[1:]))
            elif step.startswith("@"):
                for i, s in enumerate(sers):
                    grab(s, f"{step[1:]}_{i}.png")
            else:
                idx, _, keys = step.partition(":")
                targets = sers if idx == "*" else [sers[int(idx)]]
                for s in targets:
                    s.write(keys.encode())
    finally:
        for s in sers:
            s.write(b'qkn')
            s.close()


if __name__ == "__main__":
    main()
