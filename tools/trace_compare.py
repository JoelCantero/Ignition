#!/usr/bin/env python3
"""Compare two per-tick traces and report the first divergence.

Both files use the `key=value` lines of `IGN_TRACE=1 ./build/ignition ... 2>&1 | grep '^t='` (fields t, x, z, kmh,
and optionally h, v). The reference trace of the original game is captured in DOSBox-X (see docs/dosbox-trace.md)
and written in the same format.

  tools/trace_compare.py port.txt original.txt [--tol x=2 z=2 kmh=1]
"""
import argparse
import re
import sys

KV = re.compile(r"(\w+)=(-?[\d.]+(?:e-?\d+)?)")


def load(path):
    rows = []
    with open(path) as f:
        for line in f:
            if line.startswith("t="):
                rows.append({k: float(v) for k, v in KV.findall(line)})
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("original")
    ap.add_argument("--tol", nargs="*", default=["x=2", "z=2", "kmh=1"], help="field=tolerance pairs")
    args = ap.parse_args()
    tol = {k: float(v) for k, v in (t.split("=") for t in args.tol)}
    a, b = load(args.port), load(args.original)
    if not a or not b:
        sys.exit("empty trace")
    worst = {k: 0.0 for k in tol}
    for i, (ra, rb) in enumerate(zip(a, b)):
        for k, lim in tol.items():
            if k in ra and k in rb:
                d = abs(ra[k] - rb[k])
                worst[k] = max(worst[k], d)
                if d > lim:
                    print("first divergence at row %d (t=%.3f): %s port=%g original=%g (tol %g)" % (
                        i, ra.get("t", 0), k, ra[k], rb[k], lim))
                    return 1
    print("traces agree over %d rows; worst differences: %s" % (
        min(len(a), len(b)), ", ".join("%s=%.3g" % kv for kv in worst.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
