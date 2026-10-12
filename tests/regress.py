#!/usr/bin/env python3
"""Deterministic headless regression tests for the port.

Runs `ignition LEVEL --sim SEC auto --rivals N --shot IMG` per track and compares section, phase, lap, NaN
presence and the framebuffer hash with tests/expected.json. Everything is deterministic (fixed 1/72 s ticks, no
wall clock), so a mismatch means behaviour changed.

  tests/regress.py                run all cases (exit 1 on any mismatch)
  tests/regress.py --update       rewrite tests/expected.json from the current build
  tests/regress.py --no-images    skip framebuffer hashes (they depend on the compiler's float rounding)
  tests/regress.py -k USA         only cases whose name contains USA
"""
import argparse
import concurrent.futures as cf
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXPECTED = os.path.join(ROOT, "tests", "expected.json")
TRACKS = ["USA", "CANADA", "AUSTRIA", "BRAZIL", "CARIB", "ICELAND", "JAPAN"]
CASES = {}
for t in TRACKS:
    CASES["%s solo 40s" % t] = [t, "--sim", "40", "auto", "--rivals", "0"]
    CASES["%s 3 rivals 40s" % t] = [t, "--sim", "40", "auto", "--rivals", "3"]
    CASES["%s turbo 8s" % t] = [t, "--sim", "8", "turbo", "--rivals", "0"]
CASES["USA viewer car 9"] = ["--model", "9", "0.6", "30", "0.15", "0", "USA"]
CASES["USA grid camera"] = ["USA", "--sim", "0.3", "0", "--rivals", "3", "--cam", "900", "20", "2700", "0.0", "0.25"]

STATE = re.compile(r"section (-?\d+)(?:/(\d+))? phase (\d+) lap (\d+)")


def run_case(binary, name, args):
    with tempfile.TemporaryDirectory() as tmp:
        shot = os.path.join(tmp, "x.ppm")
        cmd = [binary] + (["--shot", shot] + args if args[0] == "--model" else args + ["--shot", shot])
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        out = p.stdout + p.stderr
        res = {"rc": p.returncode, "nan": bool(re.search(r"\bnan\b|-nan", out, re.I)),
               "states": [[int(v or 0) for v in m] for m in STATE.findall(out)]}
        res["image"] = hashlib.md5(open(shot, "rb").read()).hexdigest() if os.path.exists(shot) else None
        return name, res


def compare(got, want, images, section_tol):
    errs = []
    if got["rc"] != want["rc"]:
        errs.append("exit code %s != %s" % (got["rc"], want["rc"]))
    if got["nan"]:
        errs.append("NaN in output")
    if len(got["states"]) != len(want["states"]):
        errs.append("%d cars reported, expected %d" % (len(got["states"]), len(want["states"])))
    for i, (g, w) in enumerate(zip(got["states"], want["states"])):
        if abs(g[0] - w[0]) > section_tol or g[1:] != w[1:]:
            errs.append("car %d: section/count/phase/lap %s, expected %s" % (i, g, w))
    if images and got["image"] != want["image"]:
        errs.append("framebuffer hash %s, expected %s" % (got["image"], want["image"]))
    return errs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "ignition"))
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--no-images", action="store_true")
    ap.add_argument("--section-tol", type=int, default=0, help="allowed section difference (default exact)")
    ap.add_argument("-k", default="")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 2)
    args = ap.parse_args()

    cases = {n: a for n, a in CASES.items() if args.k in n}
    with cf.ThreadPoolExecutor(args.j) as ex:
        results = dict(ex.map(lambda kv: run_case(args.binary, *kv), cases.items()))

    if args.update:
        old = json.load(open(EXPECTED)) if os.path.exists(EXPECTED) else {}
        old.update(results)
        with open(EXPECTED, "w") as f:
            json.dump(old, f, indent=1, sort_keys=True)
            f.write("\n")
        print("updated %d cases in %s" % (len(results), os.path.relpath(EXPECTED, ROOT)))
        return 0

    expected = json.load(open(EXPECTED))
    failed = 0
    for name in cases:
        errs = (compare(results[name], expected[name], not args.no_images, args.section_tol)
                if name in expected else ["no expected result (run --update)"])
        print("%-4s %s" % ("FAIL" if errs else "ok", name))
        for e in errs:
            print("       " + e)
        failed += bool(errs)
    print("%d/%d passed" % (len(cases) - failed, len(cases)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
