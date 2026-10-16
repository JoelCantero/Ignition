#!/usr/bin/env python3
"""Record and compare reproducible test baselines (milestone H0).

  tools/test_baseline.py record --build DIR -o tests/baselines/NAME.json
  tools/test_baseline.py compare --build DIR --baseline tests/baselines/NAME.json

`record` reads a finished `ctest --output-junit ctest-junit.xml` run in DIR and the JSON reports the suites
write there, and stores provenance (commit, clean tree, compiler, CMake options, binary and game-data hashes)
plus every test outcome classified as:

  execution   the test could not produce a verdict (crash, timeout, harness error, NaN/exit code in regress);
  historical  differences of tests/regress.py against tests/expected.json, a historical snapshot that is
              never regenerated to hide them;
  regression  any other failing test.

The `regress` case outputs are also stored as the current reference, so `compare` can report new failures and
behaviour changes without rewriting tests/expected.json. Game data is hashed, never copied.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

HISTORICAL = "regress"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def cache(build):
    values = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        match = re.match(r"([A-Za-z0-9_]+):[A-Z]+=(.*)", line)
        if match:
            values[match[1]] = match[2]
    return values


def compiler(build):
    for path in sorted(build.glob("CMakeFiles/*/CMakeCCompiler.cmake")):
        text = path.read_text(encoding="utf-8")
        ident = re.search(r'set\(CMAKE_C_COMPILER_ID "([^"]*)"\)', text)
        version = re.search(r'set\(CMAKE_C_COMPILER_VERSION "([^"]*)"\)', text)
        if ident and version:
            return f"{ident[1]} {version[1]}"
    raise ValueError("C compiler identification not found in the build directory")


def git(source, *args):
    return subprocess.run(["git", "-C", str(source), *args], check=True, capture_output=True, text=True).stdout


def data_manifest(data):
    files = sorted(p for p in data.rglob("*") if p.is_file())
    if not files:
        raise ValueError(f"empty game data directory: {data}")
    rows = [[p.relative_to(data).as_posix(), sha256(p)] for p in files]
    return {"files": len(rows), "sha256": hashlib.sha256(json.dumps(rows).encode()).hexdigest()}


def junit(build):
    root = ET.parse(build / "ctest-junit.xml").getroot()
    tests = {}
    for case in root.iter("testcase"):
        failure = case.find("failure")
        status = case.get("status", "run")
        output = case.findtext("system-out") or ""
        tests[case.get("name")] = {
            "status": "skipped" if status == "notrun" else "fail" if failure is not None or status == "fail" else "pass",
            "seconds": round(float(case.get("time") or 0), 2),
            "timeout": "Timeout" in (failure.get("message", "") if failure is not None else "") or
                       "***Timeout" in output,
            "output_tail": output.strip().splitlines()[-3:] if failure is not None else [],
        }
    if not tests:
        raise ValueError("ctest-junit.xml has no test cases")
    return tests


def reports(build):
    found = {}
    for path in sorted(build.glob("*report*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            found[path.name] = {"unreadable": str(error)}
            continue
        if isinstance(data, dict):
            found[path.name] = {k: data[k] for k in ("status", "error", "passed", "total", "differing_cases")
                                if k in data}
    return found


def classify(tests, report_summaries, regress):
    classes = {}
    for name, test in tests.items():
        if test["status"] != "fail":
            continue
        summary = report_summaries.get(name.replace("_", "-") + "-report.json", {})
        harness_error = "error" in summary or "unreadable" in summary
        if name == HISTORICAL and regress:
            runtime = regress.get("differing_cases", {}).get("runtime", 0)
            classes[name] = "execution" if runtime or "error" in regress else "historical"
        elif test["timeout"] or harness_error:
            classes[name] = "execution"
        else:
            classes[name] = "regression"
    return classes


def record(args):
    build = args.build.resolve()
    values = cache(build)
    source = Path(values["CMAKE_HOME_DIRECTORY"])
    dirty = git(source, "status", "--porcelain", "--untracked-files=no").strip()
    if dirty and not args.allow_dirty:
        raise ValueError("source tree has uncommitted changes; record a committed revision (or --allow-dirty)")
    tests = junit(build)
    summaries = reports(build)
    regress_path = build / "regression-report.json"
    regress = json.loads(regress_path.read_text(encoding="utf-8")) if regress_path.exists() else None
    classes = classify(tests, summaries, regress)
    data = Path(values["GAME_DIR"])
    baseline = {
        "schema": 1,
        "label": args.label,
        "provenance": {
            "commit": git(source, "rev-parse", "HEAD").strip(),
            "tree_clean": not dirty,
            "build_type": values.get("CMAKE_BUILD_TYPE"),
            "options": {k: values[k] for k in ("IGN_VERIFY_ORIGINAL",) if k in values},
            "compiler": compiler(build),
            "platform": platform.platform(),
            "python": values.get("Python3_EXECUTABLE"),
            "ctest_arguments": args.ctest_arguments,
            "binary_sha256": sha256(build / "ignition"),
            "original_sha256": sha256(data / "MAINDOS.EXE"),
            "game_data": data_manifest(data),
        },
        "summary": {
            "tests": len(tests),
            "passed": sum(t["status"] == "pass" for t in tests.values()),
            "skipped": sum(t["status"] == "skipped" for t in tests.values()),
            "failed": {kind: sorted(n for n, c in classes.items() if c == kind)
                       for kind in ("execution", "historical", "regression")},
        },
        "tests": {name: {**test, **({"class": classes[name]} if name in classes else {})}
                  for name, test in sorted(tests.items())},
        "reports": summaries,
        "regress": None if regress is None else {
            "historical_reference": "tests/expected.json",
            "historical_reference_sha256": regress.get("expected_sha256"),
            "passed": regress.get("passed"), "total": regress.get("total"),
            "differing_cases": regress.get("differing_cases"),
            "current": {name: case["got"] for name, case in sorted(regress.get("cases", {}).items())},
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(baseline, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    s = baseline["summary"]
    print(f"{args.output}: {s['passed']}/{s['tests']} passed; execution={len(s['failed']['execution'])} "
          f"historical={len(s['failed']['historical'])} regression={len(s['failed']['regression'])}")
    return 0


def compare(args):
    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    build = args.build.resolve()
    tests = junit(build)
    findings = []
    for name, old in baseline["tests"].items():
        new = tests.get(name)
        if new is None:
            findings.append(f"missing test {name}")
        elif old["status"] == "pass" and new["status"] != "pass":
            findings.append(f"new failure: {name}")
    for name in sorted(set(tests) - set(baseline["tests"])):
        if tests[name]["status"] != "pass":
            findings.append(f"new test failing: {name}")
    regress_path = build / "regression-report.json"
    if baseline.get("regress") and regress_path.exists():
        current = json.loads(regress_path.read_text(encoding="utf-8")).get("cases", {})
        for name, old in baseline["regress"]["current"].items():
            got = current.get(name, {}).get("got")
            if got is None:
                findings.append(f"regress case not run: {name}")
            elif got != old:
                changed = [k for k in ("rc", "nan", "states", "image") if got.get(k) != old.get(k)]
                findings.append(f"regress behaviour change in {name}: {', '.join(changed)}")
    for finding in findings:
        print(finding)
    print(f"{len(findings)} differences against {args.baseline}")
    return 1 if findings else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    rec = sub.add_parser("record")
    rec.add_argument("--build", type=Path, required=True)
    rec.add_argument("-o", "--output", type=Path, required=True)
    rec.add_argument("--label", default="baseline")
    rec.add_argument("--ctest-arguments", default="-j4 --timeout 3600 --output-junit ctest-junit.xml")
    rec.add_argument("--allow-dirty", action="store_true")
    cmp = sub.add_parser("compare")
    cmp.add_argument("--build", type=Path, required=True)
    cmp.add_argument("--baseline", type=Path, required=True)
    args = parser.parse_args()
    try:
        return record(args) if args.command == "record" else compare(args)
    except (OSError, ValueError, KeyError, ET.ParseError, subprocess.CalledProcessError) as error:
        print(f"test baseline failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
