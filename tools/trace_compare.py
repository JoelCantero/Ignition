#!/usr/bin/env python3
"""Compare two per-tick traces and report the first divergence.

Both files use the `key=value` lines of `IGN_TRACE=1 ./build/ignition ... 2>&1 | grep '^t='` (fields t, x, z, kmh,
and optionally h, v). The reference trace of the original game is captured in DOSBox-X (see docs/dosbox-trace.md)
and written in the same format.

  tools/trace_compare.py port.txt original.txt [--tol x=2 z=2 kmh=1]
"""
import argparse
import math
import re
import sys

NUMBER = re.compile(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?")


def load_text(text):
    rows = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if not line.startswith("t="):
            continue
        row, seen = {}, set()
        for token in line.split():
            if "=" not in token:
                raise ValueError("line %d: malformed field %r" % (lineno, token))
            key, value = token.split("=", 1)
            if not re.fullmatch(r"\w+", key) or key in seen:
                raise ValueError("line %d: invalid or duplicate field %r" % (lineno, key))
            seen.add(key)
            # Legacy traces print drive=force/limit and turbo=mode/charge.
            parts = value.split("/")
            parsed = []
            for part in parts:
                try:
                    number = float(int(part, 16)) if part.lower().startswith(("0x", "-0x")) else float(part)
                except (ValueError, OverflowError) as exc:
                    raise ValueError("line %d: invalid number for %s: %r" % (lineno, key, part)) from exc
                if not math.isfinite(number):
                    raise ValueError("line %d: non-finite %s" % (lineno, key))
                if not NUMBER.fullmatch(part) and not re.fullmatch(r"-?0[xX][0-9a-fA-F]+", part):
                    raise ValueError("line %d: malformed number for %s" % (lineno, key))
                parsed.append(number)
            if len(parsed) == 1:
                values = {key: parsed[0]}
            elif len(parsed) == 2 and key in ("drive", "turbo"):
                values = {key: parsed[0], "drive_limit" if key == "drive" else "turbo_charge": parsed[1]}
            else:
                raise ValueError("line %d: unsupported compound field %s" % (lineno, key))
            if row.keys() & values.keys():
                raise ValueError("line %d: overlapping fields for %s" % (lineno, key))
            row.update(values)
        if "t" not in row or row["t"] < 0:
            raise ValueError("line %d: invalid timestamp" % lineno)
        if rows and row["t"] <= rows[-1]["t"]:
            raise ValueError("line %d: timestamps must increase" % lineno)
        if "tick" in row and (row["tick"] < 0 or not row["tick"].is_integer()):
            raise ValueError("line %d: invalid tick" % lineno)
        rows.append(row)
    if not rows:
        raise ValueError("empty trace")
    return rows


def load(path):
    with open(path) as f:
        return load_text(f.read())


def compare_rows(a, b, tol, all_fields=False):
    if not a or not b:
        raise ValueError("empty trace")
    if (not tol and not all_fields) or any(not math.isfinite(v) or v < 0 for v in tol.values()):
        raise ValueError("tolerances must be finite and nonnegative")
    worst = {}
    for i, (ra, rb) in enumerate(zip(a, b)):
        if ra["t"] != rb["t"] or ra.get("tick") != rb.get("tick"):
            return {"row": i, "field": "time/tick", "port": [ra["t"], ra.get("tick")],
                    "original": [rb["t"], rb.get("tick")]}, worst
        if all_fields and ra.keys() != rb.keys():
            return {"row": i, "field": "schema", "port": sorted(ra), "original": sorted(rb)}, worst
        fields = list(ra) + [k for k in tol if k not in ra] if all_fields else tol.keys()
        for k in fields:
            if k not in ra or k not in rb:
                return {"row": i, "field": k, "port": ra.get(k), "original": rb.get(k)}, worst
            lim = tol.get(k, 0.0)
            d = abs(ra[k] - rb[k])
            worst[k] = max(worst.get(k, 0.0), d)
            if d > lim:
                return {"row": i, "field": k, "port": ra[k], "original": rb[k], "tolerance": lim}, worst
    if len(a) != len(b):
        return {"row": min(len(a), len(b)), "field": "length", "port": len(a), "original": len(b)}, worst
    return None, worst


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("original")
    ap.add_argument("--tol", nargs="+", default=["x=2", "z=2", "kmh=1"], help="field=tolerance pairs")
    ap.add_argument("--all-fields", action="store_true", help="compare the complete schema; unspecified tolerances are zero")
    args = ap.parse_args()
    try:
        tol = {k: float(v) for k, v in (t.split("=") for t in args.tol)}
        if not tol or any(not math.isfinite(v) or v < 0 for v in tol.values()):
            raise ValueError("tolerances must be finite and nonnegative")
        a, b = load(args.port), load(args.original)
        diff, worst = compare_rows(a, b, tol, args.all_fields)
    except (ValueError, OSError) as exc:
        print("trace comparison failed: %s" % exc, file=sys.stderr)
        return 1
    if diff:
        print("first divergence at row %(row)d: %(field)s port=%(port)s original=%(original)s" % diff)
        return 1
    print("traces agree over %d rows; worst differences: %s" % (
        min(len(a), len(b)), ", ".join("%s=%.3g" % kv for kv in worst.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
