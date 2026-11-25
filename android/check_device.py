"""UI smoke test on an explicitly selected Android device. Starts at the title screen."""
import argparse
import re
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path

PKG = "dev.stickarcade.player"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--adb", default="adb")
    p.add_argument("--serial", required=True)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    adb = [args.adb, "-s", args.serial]
    out = args.output or Path(__file__).resolve().parents[1]/"out"/"phone-link-check"
    out.mkdir(parents=True, exist_ok=True)

    def shell(*parts):
        return subprocess.check_output([*adb, "shell", *map(str,parts)], encoding="utf-8").strip()

    def ui():
        for _ in range(4):
            xml = subprocess.check_output([*adb,"exec-out","uiautomator","dump","/dev/tty"],encoding="utf-8")
            if "<hierarchy" in xml:
                return ET.fromstring(xml[xml.index("<hierarchy"):xml.index("</hierarchy>")+12])
            time.sleep(.5)
        raise RuntimeError(xml)

    def widgets():
        nodes = list(ui().iter("node"))
        pad = next(n for n in nodes if n.get("content-desc") == "Game controls" and n.get("package") == PKG)
        bounds = list(map(int, re.findall(r"\d+", pad.get("bounds"))))
        status = next(n.get("text") for n in nodes if "RTT" in n.get("text",""))
        return bounds, status

    def tap(bounds, fraction):
        l,t,r,b = bounds
        shell("input","tap",int(l+(r-l)*fraction),int((b+t)/2))
        time.sleep(.28)

    def state(expected):
        for _ in range(4):
            bounds, status = widgets()
            if expected in status:
                print(status, flush=True)
                return bounds
        raise AssertionError((expected, status))

    def shot(name):
        (out/(name+".png")).write_bytes(subprocess.check_output([*adb,"exec-out","screencap","-p"]))

    shell("am","force-stop",PKG)
    shell("am","start","-W","-n",PKG+"/.Player")
    bounds = state("Menu")
    tap(bounds,.66)
    time.sleep(1)
    labels = ("THROWHALLA","BIG BUMP","MoonDuel","MidDiff")
    for i,name in enumerate(("Throwhalla","Big-Bump","MoonDuel","MidDiff")):
        if i: tap(bounds,.34)  # launcher remembers the last card
        tap(bounds,.66)
        bounds = state("Lobby")
        b = .64 if i==3 else .34
        if i==3:
            tap(bounds,b)
            time.sleep(2)
        tap(bounds,b)
        bounds = state(labels[i]+" Solo")
        assert (bounds[3] > bounds[2]) == (i == 1), (name, bounds)
        shot(name)
        tap(bounds,.86 if i==3 else .66)
        shell("input","keyevent","KEYCODE_HOME")
        # Wait for the vendor launcher's home transition before bringing the task back.
        time.sleep(2)
        shell("am","start","-W","-n",PKG+"/.Player")
        bounds = state("Menu")
        print("PASS",name,"solo + background/resume",flush=True)


if __name__ == "__main__":
    main()
