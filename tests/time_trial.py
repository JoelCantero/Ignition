#!/usr/bin/env python3
"""Headless time trial: record the ghost, load it again and only replace it when beaten.

Runs `ignition USA --time-trial --sim 150 auto` (the autopilot finishes in about 142 s) with IGN_SAVE_DIR set to
a temporary folder and unbeatable track records (to isolate ghost persistence), and checks that:
  1. the first run (no ghost) writes GHOSTS/GOLDRUSH.GST and IGN_DOS.BTZ in the user folder only;
  2. the second run loads the ghost, does not beat it and leaves both files unchanged;
  3. after making the saved ghost slower (header time), a third run replaces it with the new time;
  4. the BTZ written by the port is still a valid image (tools/btz.py roundtrip) and the game folder is untouched;
  5. an elimination race that ends (USA, 3 rivals) only reshuffles the first 4 grid slots (0x2cf60); skipped if
     the AI does not bring the race to its end.
"""
import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def digest(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest() if os.path.exists(path) else None


def tree_digest(folder):
    out = {}
    for base, _, files in os.walk(folder):
        for f in files:
            p = os.path.join(base, f)
            st = os.stat(p)
            out[os.path.relpath(p, folder)] = (st.st_size, st.st_mtime_ns)
    return out


def run(binary, data, save, shot, args=("--time-trial", "--sim", "150", "auto")):
    env = dict(os.environ, IGN_SAVE_DIR=save, IGNITION_DATA=data)
    p = subprocess.run([binary, "USA", *args, "--shot", shot],
                       env=env, capture_output=True, text=True, timeout=600)
    if p.returncode:
        raise SystemExit("ignition failed (%d): %s" % (p.returncode, p.stderr))
    return p.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--data", required=True)
    a = ap.parse_args()
    if not os.path.isdir(os.path.join(a.data, "LEVELS")):
        print("skip: no game data in %s" % a.data)
        return 0
    game_before = tree_digest(a.data)
    tmp = tempfile.mkdtemp(prefix="ign-time-trial-")
    failures = []

    def check(cond, msg):
        if not cond:
            failures.append(msg)
            print("FAIL", msg)

    try:
        save = os.path.join(tmp, "user") + os.sep
        os.makedirs(save)
        shot = os.path.join(tmp, "x.ppm")
        gst = os.path.join(save, "GHOSTS", "GOLDRUSH.GST")
        btz = os.path.join(save, "IGN_DOS.BTZ")
        seed = bytearray(open(os.path.join(a.data, "IGN_DOS.BTZ"), "rb").read())
        # An equal ghost time may still enter the race/lap top five; exclude that separate save path.
        for table in (0x14, 0x50):
            for slot in range(5):
                struct.pack_into("<d", seed, 0x1BF + 0x78 + table + slot * 8, 0.0)
        with open(btz, "wb") as f:
            f.write(seed)

        out1 = run(a.binary, a.data, save, shot)
        check("time trial: no ghost, record 30000.00 s" in out1, "first run starts without a ghost")
        m = re.search(r"time trial: ([0-9.]+) s against 30000.00 s, ghost (\S+)", out1)
        check(m is not None and m.group(2) == gst, "first run saves the ghost")
        check(os.path.getsize(gst) == 0x49D4C if os.path.exists(gst) else False, "ghost size 302412")
        check(os.path.exists(btz) and os.path.getsize(btz) == 0x597, "BTZ written in the user folder")
        if failures:
            return 1
        race = float(m.group(1))
        car, track, time_cs = struct.unpack_from("<hhh", open(gst, "rb").read(), 0)
        check(track == 1 and time_cs == int(race * 100 + 1e-6), "ghost header: USA and race time")
        h_gst, h_btz = digest(gst), digest(btz)

        out2 = run(a.binary, a.data, save, shot)
        check("time trial: ghost, record %.2f s" % (time_cs / 100.0) in out2, "second run loads the ghost")
        check("ghost not saved" in out2, "an equal time does not beat the ghost")
        check(digest(gst) == h_gst, "ghost unchanged when not beaten")
        check(digest(btz) == h_btz, "BTZ unchanged when no record is set")

        data = bytearray(open(gst, "rb").read())
        struct.pack_into("<h", data, 4, time_cs + 500)
        open(gst, "wb").write(data)
        out3 = run(a.binary, a.data, save, shot)
        check("record %.2f s" % ((time_cs + 500) / 100.0) in out3, "third run loads the slower ghost")
        check("ghost " + gst in out3, "a faster run replaces the ghost")
        check(struct.unpack_from("<h", open(gst, "rb").read(), 4)[0] == time_cs, "replaced ghost has the new time")

        rt = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "btz.py"), "roundtrip", btz],
                            capture_output=True, text=True)
        check(rt.returncode == 0, "saved BTZ parses and rebuilds: " + rt.stdout.strip())

        save2 = os.path.join(tmp, "elim") + os.sep
        os.makedirs(save2)
        with open(os.path.join(save2, "IGN_DOS.BTZ"), "wb") as f:
            f.write(seed)
        out4 = run(a.binary, a.data, save2, shot, ("--elimination", "--rivals", "3", "--sim", "600", "auto"))
        if "race: 4/4 finished" not in out4:
            print("skip elimination race end: not every car is out")
        else:
            orig = seed
            new = open(os.path.join(save2, "IGN_DOS.BTZ"), "rb").read()
            changed = [i for i in range(0x597) if orig[i] != new[i]]
            check(all(0x133 <= i < 0x143 for i in changed), "elimination end only touches grid slots 0-3")
            check(sorted(struct.unpack_from("<4i", new, 0x133)) == sorted(struct.unpack_from("<4i", orig, 0x133)),
                  "grid shuffle is a permutation")
        check(tree_digest(a.data) == game_before, "game folder untouched")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        return 1
    print("time_trial: ok (race %.2f s)" % race)
    return 0


if __name__ == "__main__":
    sys.exit(main())
