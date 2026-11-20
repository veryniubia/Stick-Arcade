"""Reproducible two-device DEBUG_REMOTE checks; writes serial logs and screenshots."""
import json
from pathlib import Path
import struct
import sys
import time

import serial
from net_check import grab


def decode(raw):
    state = {"phase": raw[0], "winner": struct.unpack_from("b", raw, 1)[0], "players": [], "proj": []}
    for i in range(2):
        v = struct.unpack_from("<hhhBBBBBBBB", raw, 8 + 14 * i)
        state["players"].append(dict(score=v[6], sources=v[7], serial=v[8], flash=v[4], flags=v[10]))
    for i in range(raw[36]):
        v = struct.unpack_from("<BBhhhhhBB", raw, 37 + i * 14)
        state["proj"].append(dict(owner=v[1] >> 4, x=v[2] / 16, vx=v[4] / 8))
    return state


def main():
    ports = sys.argv[1:3]
    out = Path(sys.argv[3])
    out.mkdir(parents=True, exist_ok=True)
    sers = [serial.Serial(p, 115200, timeout=0.02) for p in ports]
    log = []
    states = {}
    latest = {}
    bufs = [b"", b""]

    def drain(seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            for i, ser in enumerate(sers):
                bufs[i] += ser.read(4096)
                while b"\n" in bufs[i]:
                    line, bufs[i] = bufs[i].split(b"\n", 1)
                    text = line.decode(errors="replace").strip()
                    if text.startswith(("NET", "PERF", "RADIO", "STATE")):
                        log.append(f"[{ports[i]}] {text}")
                    if text.startswith("NET"):
                        latest[i] = text
                    if text.startswith("STATE "):
                        states[i] = decode(bytes.fromhex(text[6:]))

    def snapshot(label):
        states.clear()
        for ser in sers:
            ser.write(b"d")
        drain(0.12)
        assert len(states) == 2, states
        saved[label] = dict(states)
        return dict(states)

    def shots(label):
        for i, ser in enumerate(sers):
            grab(ser, str(out / f"{label}_{i}.png"))

    saved = {}
    try:
        for ser in sers:
            ser.dtr = False
            ser.rts = False
        drain(1)
        for ser in sers:
            ser.write(b"v")
        drain(9)
        assert all("st=PLAY" in latest.get(i, "") for i in range(2)), latest
        host = next(i for i in range(2) if "role=1 " in latest[i])
        print("Host:", ports[host], flush=True)
        sers[host].write(b"c")
        drain(0.06)
        collision = snapshot("collision")
        for st in collision.values():
            assert len(st["proj"]) == 2, st
            by_owner = {p["owner"]: p for p in st["proj"]}
            assert by_owner[0]["vx"] < 0 < by_owner[1]["vx"], st
        shots("collision")
        drain(1.5)
        sers[host].write(b"h")
        drain(0.06)
        first = snapshot("first_hit")
        for st in first.values():
            assert st["phase"] == 0 and st["players"][1]["score"] == 1, st
            assert st["players"][0]["sources"] == 1 and st["players"][0]["flash"] > 0, st
        shots("first_hit")
        sers[host].write(b"H")
        drain(0.06)
        second = snapshot("second_hit")
        for st in second.values():
            assert st["phase"] == 0 and st["players"][1]["score"] == 2, st
            assert st["players"][0]["sources"] == 3 and st["players"][0]["serial"] == 2, st
        shots("second_hit")
        drain(0.8)
        for st in snapshot("no_repeat").values():
            assert st["players"][1]["score"] == 2, st
        sers[host].write(b"t")
        drain(0.06)
        for st in snapshot("tie_hit").values():
            assert [p["score"] for p in st["players"]] == [5, 5], st
        shots("tie_hit")
        drain(1.3)
        for st in snapshot("draw").values():
            assert st["phase"] == 2 and st["winner"] == -1, st
        shots("draw")
        for ser in sers:
            ser.write(b"a")
        drain(6)
        for st in snapshot("rematch").values():
            assert st["phase"] == 0 and [p["score"] for p in st["players"]] == [0, 0], st
        # Normal charge/release and jumping must still work over the actual radio.
        for ser in sers:
            ser.write(b"1fj")
        drain(3)
        shots("normal_play")
        sers[host].write(b"w")
        drain(0.06)
        for st in snapshot("winning_hit").values():
            assert st["phase"] == 1 and [p["score"] for p in st["players"]] == [5, 0], st
        drain(1.3)
        for st in snapshot("win").values():
            assert st["phase"] == 2 and st["winner"] == 0, st
        shots("win")
        # Solo feedback: self-owned and dummy-targeted blasts use the normal damage path.
        for ser in sers:
            ser.write(b"g")
        drain(0.5)
        for ser in sers:
            ser.write(b"c")
        drain(1.5)
        for ser in sers:
            ser.write(b"h")
        drain(0.06)
        solo = snapshot("solo_self")
        assert all(st["players"][0]["sources"] == 1 for st in solo.values()), solo
        shots("solo_self")
        for ser in sers:
            ser.write(b"w")
        drain(0.06)
        for st in snapshot("solo_hit").values():
            assert st["phase"] == 0 and st["players"][0]["score"] == 5, st
            assert st["players"][1]["sources"] == 1, st
        shots("solo_hit")
        for ser in sers:
            ser.write(b"m")
        drain(0.3)
        print("ALL HARDWARE CHECKS PASSED", flush=True)
    finally:
        (out / "serial.log").write_text("\n".join(log), encoding="utf-8")
        (out / "states.json").write_text(json.dumps(saved, indent=2), encoding="utf-8")
        for ser in sers:
            ser.close()


if __name__ == "__main__":
    main()
