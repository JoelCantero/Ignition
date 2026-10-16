#!/usr/bin/env python3
"""Offline checks of tools/test_baseline.py classification and comparison (synthetic build directory)."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("test_baseline", ROOT / "tools/test_baseline.py")
baseline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baseline)

JUNIT = """<?xml version="1.0" encoding="UTF-8"?>
<testsuite tests="5">
  <testcase name="gearbox" status="run" time="0.1"/>
  <testcase name="regress" status="fail" time="9"><failure message="Failed"/></testcase>
  <testcase name="car_original" status="fail" time="2"><failure message="Failed"/></testcase>
  <testcase name="pos_original" status="fail" time="1"><failure message="Failed"/></testcase>
  <testcase name="slow" status="fail" time="60"><failure message="Timeout"/></testcase>
</testsuite>
"""


class BaselineTests(unittest.TestCase):
    def build(self, folder, regress_runtime=0):
        build = Path(folder)
        (build / "ctest-junit.xml").write_text(JUNIT)
        (build / "car-original-report.json").write_text(json.dumps({"status": "fail", "error": "harness"}))
        (build / "pos-original-report.json").write_text(json.dumps({"status": "fail"}))
        (build / "regression-report.json").write_text(json.dumps({
            "status": "fail", "passed": 0, "total": 1, "expected_sha256": "x",
            "differing_cases": {"runtime": regress_runtime, "state": 1, "image": 1},
            "cases": {"USA solo 40s": {"got": {"rc": 0, "nan": False, "states": [[1, 0, 0, 0]], "image": "a"}}}}))
        return build

    def test_failures_are_classified(self):
        with tempfile.TemporaryDirectory() as folder:
            build = self.build(folder)
            tests = baseline.junit(build)
            regress = json.loads((build / "regression-report.json").read_text())
            classes = baseline.classify(tests, baseline.reports(build), regress)
            self.assertEqual(classes, {"regress": "historical", "car_original": "execution",
                                       "pos_original": "regression", "slow": "execution"})
            self.assertNotIn("gearbox", classes)
            regress["differing_cases"]["runtime"] = 1
            self.assertEqual(baseline.classify(tests, baseline.reports(build), regress)["regress"], "execution")

    def test_compare_reports_new_failures_and_behaviour_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            build = self.build(folder)
            reference = {"tests": {"gearbox": {"status": "pass"}, "pos_original": {"status": "pass"},
                                   "regress": {"status": "fail"}},
                         "regress": {"current": {"USA solo 40s": {"rc": 0, "nan": False, "states": [[1, 0, 0, 0]],
                                                                  "image": "b"}}}}
            path = build / "reference.json"
            path.write_text(json.dumps(reference))
            args = type("Args", (), {"build": build, "baseline": path})()
            self.assertEqual(baseline.compare(args), 1)
            reference["tests"]["pos_original"]["status"] = "fail"
            reference["regress"]["current"]["USA solo 40s"]["image"] = "a"
            path.write_text(json.dumps(reference))
            # car_original/slow are new tests that fail: still reported.
            self.assertEqual(baseline.compare(args), 1)
            (build / "ctest-junit.xml").write_text(JUNIT.replace('status="fail" time="2"><failure message="Failed"/>',
                                                                  'status="run" time="2">').replace(
                '<testcase name="slow" status="fail" time="60"><failure message="Timeout"/></testcase>', ""))
            self.assertEqual(baseline.compare(args), 0)

    def test_published_h0_baseline_has_complete_provenance(self):
        record = json.loads((ROOT / "tests/baselines/h0-6f5bee0.json").read_text())
        provenance = record["provenance"]
        self.assertTrue(provenance["tree_clean"])
        self.assertRegex(provenance["commit"], r"^[0-9a-f]{40}$")
        for key in ("binary_sha256", "original_sha256", "compiler", "build_type", "ctest_arguments"):
            self.assertTrue(provenance[key], key)
        self.assertEqual(provenance["original_sha256"],
                         json.loads((ROOT / "docs/functions.json").read_text())["executable"]["sha256"])
        summary = record["summary"]
        failed = [n for n, t in record["tests"].items() if t["status"] == "fail"]
        self.assertEqual(sorted(failed), sorted(sum(summary["failed"].values(), [])))
        self.assertEqual(summary["failed"]["historical"], ["regress"])


if __name__ == "__main__":
    unittest.main()
