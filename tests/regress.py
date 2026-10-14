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
CASES["USA grid camera"] = ["USA", "--sim", "0.3", "0", "--rivals", "3", "--cam", "900", "-165", "2700", "0.0", "0.25"]

STATE = re.compile(r"section (-?\d+)(?:/(\d+))? phase (\d+) lap (\d+)")


def run_case(binary, name, args, *, env=None, trace=False):
    with tempfile.TemporaryDirectory() as tmp:
        shot = os.path.join(tmp, "x.ppm")
        cmd = [binary] + (["--shot", shot] + args if args[0] == "--model" else args + ["--shot", shot])
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=600, env=env)
        out = p.stdout + p.stderr
        res = {"rc": p.returncode, "nan": bool(re.search(r"\bnan\b|-nan", out, re.I)),
               "states": [[int(v or 0) for v in m] for m in STATE.findall(out)]}
        res["image"] = hashlib.md5(open(shot, "rb").read()).hexdigest() if os.path.exists(shot) else None
        if trace:
            res["trace"] = "\n".join(line for line in out.splitlines() if line.startswith("t=")) + "\n"
            res["output"] = out
        return name, res


def differences(got, want, section_tol):
    errs = {"runtime": [], "state": [], "image": []}
    if got["rc"] != 0 or got["rc"] != want["rc"]:
        errs["runtime"].append("exit code %s, expected %s (requires zero)" % (got["rc"], want["rc"]))
    if got["nan"]:
        errs["runtime"].append("NaN in output")
    if got["image"] is None:
        errs["runtime"].append("missing framebuffer")
    if len(got["states"]) != len(want["states"]):
        errs["state"].append("%d cars reported, expected %d" % (len(got["states"]), len(want["states"])))
    for i, (g, w) in enumerate(zip(got["states"], want["states"])):
        if abs(g[0] - w[0]) > section_tol or g[1:] != w[1:]:
            errs["state"].append("car %d: section/count/phase/lap %s, expected %s" % (i, g, w))
    if got["image"] != want["image"]:
        errs["image"].append("framebuffer hash %s, expected %s" % (got["image"], want["image"]))
    return errs


def compare(got, want, images, section_tol):
    grouped = differences(got, want, section_tol)
    return grouped["runtime"] + grouped["state"] + (grouped["image"] if images else [])


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def controlled_environment(data, trace=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith("IGN_") and k != "IGNITION_DATA"}
    env["IGNITION_DATA"] = os.path.abspath(data)
    if trace:
        env["IGN_TRACE"] = trace
    return env


def write_report(path, report):
    with open(path, "w") as f:
        json.dump(report, f, indent=2, sort_keys=True, allow_nan=False)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "ignition"))
    ap.add_argument("--data", default=os.getenv("IGNITION_DATA", "/Users/Shared/Epic Games/Ignition/ignition"))
    ap.add_argument("--report", help="write categorized results and binary/baseline hashes to JSON")
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--no-images", action="store_true")
    ap.add_argument("--section-tol", type=int, default=0, help="allowed section difference (default exact)")
    ap.add_argument("-k", default="")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 2)
    args = ap.parse_args()
    if args.section_tol < 0 or args.j < 1:
        ap.error("section tolerance must be nonnegative and jobs must be positive")
    cases = {n: a for n, a in CASES.items() if args.k in n}
    if not cases:
        ap.error("no matching cases")
    env = controlled_environment(args.data)
    report = {"status": "running", "binary": os.path.abspath(args.binary), "cases": {},
              "data_directory": env["IGNITION_DATA"], "images": not args.no_images,
              "section_tolerance": args.section_tol}
    if args.report:
        write_report(args.report, report)
    try:
        report["binary_sha256"] = sha256(args.binary)
        report["expected_sha256"] = sha256(EXPECTED)
        with cf.ThreadPoolExecutor(args.j) as ex:
            results = dict(ex.map(lambda kv: run_case(args.binary, *kv, env=env), cases.items()))
        expected = json.load(open(EXPECTED))
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        report.update(status="fail", error=str(exc))
        if args.report:
            write_report(args.report, report)
        print("regression failed: %s" % exc, file=sys.stderr)
        return 1

    if args.update:
        invalid = [n for n, r in results.items() if r["rc"] or r["nan"] or r["image"] is None
                   or (not n.startswith("USA viewer") and not r["states"])]
        if invalid:
            report.update(status="fail", error="cannot update failed captures: " + ", ".join(invalid))
            if args.report:
                write_report(args.report, report)
            print(report["error"], file=sys.stderr)
            return 1
        expected.update(results)
        write_report(EXPECTED, expected)
        report["status"] = "updated"
        if args.report:
            report["cases"] = results
            write_report(args.report, report)
        print("updated %d cases in %s" % (len(results), os.path.relpath(EXPECTED, ROOT)))
        return 0

    failed = 0
    categories = {k: 0 for k in ("runtime", "state", "image")}
    for name in cases:
        grouped = (differences(results[name], expected[name], args.section_tol) if name in expected else
                   {"runtime": ["no expected result"], "state": [], "image": []})
        errs = grouped["runtime"] + grouped["state"] + (grouped["image"] if not args.no_images else [])
        print("%-4s %s" % ("FAIL" if errs else "ok", name))
        for kind, messages in grouped.items():
            categories[kind] += bool(messages)
            if kind == "image" and args.no_images:
                continue
            for e in messages:
                print("       [%s] %s" % (kind, e))
        report["cases"][name] = {"got": results[name], "expected": expected.get(name), "differences": grouped}
        failed += bool(errs)
    report.update(status="fail" if failed else "pass", passed=len(cases) - failed, total=len(cases),
                  differing_cases=categories)
    if args.report:
        write_report(args.report, report)
    print("%d/%d passed; differing cases: runtime=%d state=%d image=%d%s" %
          (report["passed"], len(cases), categories["runtime"], categories["state"], categories["image"],
           " (images excluded)" if args.no_images else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
