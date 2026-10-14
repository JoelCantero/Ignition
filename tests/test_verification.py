#!/usr/bin/env python3
"""Synthetic verifier controls; optional --binary/--data enables headless CLI checks."""
import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import regress
import solo_replay
from trace_compare import compare_rows, load_text


def trace(ticks):
    lines = []
    for tick in range(ticks + 1):
        row = dict.fromkeys(sorted(solo_replay.FIELDS), 0)
        row.update(t=tick / 72.0, tick=tick, trace_schema=1)
        lines.append("t=%.17g " % row.pop("t") + " ".join("%s=%.17g" % kv for kv in row.items()))
    return "\n".join(lines)


class TraceTests(unittest.TestCase):
    def test_numeric_formats(self):
        row = load_text("stats ignored\nt=0 x=-1.2e-03 z=+2E+4 kmh=.5 flags=0x10")[0]
        self.assertEqual([row[k] for k in ("x", "z", "kmh", "flags")], [-0.0012, 20000, 0.5, 16])

    def test_legacy_compounds_are_compared(self):
        a = load_text("t=0 drive=3/4 turbo=1/100")
        b = load_text("t=0 drive=3/5 turbo=1/100")
        diff, _ = compare_rows(a, b, {}, all_fields=True)
        self.assertEqual(diff["field"], "drive_limit")
        self.assertEqual(a[0]["turbo_charge"], 100)

    def test_invalid_traces(self):
        for text in ("", "t=nan", "t=inf", "t=-1", "t=0 x=NaN", "t=0 x=1e999",
                     "t=0 x=garbage", "t=0 x=1suffix", "t=0 x=1 x=2", "t=0 broken",
                     "t=0\nt=0", "t=1\nt=0", "t=0 tick=.5", "t=0 tick=-1",
                     "t=0 heading=1/2", "t=0 drive=1/2/3", "t=0 drive=1/2 drive_limit=3"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                load_text(text)

    def test_alignment_schema_length_and_perturbation(self):
        a = load_text("t=0 tick=0 x=1 heading=2")
        for text, field in (("t=1 tick=0 x=1 heading=2", "time/tick"),
                            ("t=0 tick=1 x=1 heading=2", "time/tick"),
                            ("t=0 tick=0 x=1", "schema"),
                            ("t=0 tick=0 x=1 heading=2\nt=1 tick=1 x=1 heading=2", "length"),
                            ("t=0 tick=0 x=1 heading=2.001", "heading")):
            with self.subTest(field=field):
                diff, _ = compare_rows(a, load_text(text), {}, all_fields=True)
                self.assertEqual(diff["field"], field)
        self.assertIsNone(compare_rows(a, a, {}, all_fields=True)[0])

    def test_tolerance_validation(self):
        a = load_text("t=0 x=1")
        b = load_text("t=0 x=1.1")
        self.assertIsNone(compare_rows(a, b, {"x": 0.11})[0])
        self.assertEqual(compare_rows(a, b, {"x": 0.09})[0]["field"], "x")
        self.assertEqual(compare_rows(a, a, {"typo": 1}, all_fields=True)[0]["field"], "typo")
        for tolerance in ({}, {"x": -1}, {"x": float("nan")}, {"x": float("inf")}):
            with self.subTest(tolerance=tolerance), self.assertRaises(ValueError):
                compare_rows(a, b, tolerance)
        with self.assertRaises(ValueError):
            compare_rows([], [], {}, all_fields=True)

    def test_full_schema_and_tick_count(self):
        text = trace(2)
        self.assertEqual(len(solo_replay.FIELDS), 98)
        self.assertEqual(len(solo_replay.validate_trace(text, 2)), 3)
        for invalid in ("\n".join(text.splitlines()[1:]), "\n".join(text.splitlines()[::2]),
                        text.replace("trace_schema=1", "trace_schema=2"),
                        text.replace("b_corner_z3=0 ", ""), text.replace("tick=1", "tick=9"),
                        text.replace("b_mass=0", "b_mass=nan"),
                        text.replace("t=0.013888888888888888", "t=0.014")):
            with self.subTest(text=invalid), self.assertRaises(ValueError):
                solo_replay.validate_trace(invalid, 2)


class RegressionTests(unittest.TestCase):
    def setUp(self):
        self.good = {"rc": 0, "nan": False, "image": "frame", "states": [[1, 10, 0, 0]]}

    def test_independent_categories(self):
        got = dict(self.good, image="changed", states=[[2, 10, 0, 0]])
        diff = regress.differences(got, self.good, 0)
        self.assertFalse(diff["runtime"])
        self.assertTrue(diff["state"])
        self.assertTrue(diff["image"])
        self.assertTrue(regress.compare(got, self.good, False, 0))
        self.assertFalse(regress.compare(dict(self.good, image="changed"), self.good, False, 0))

    def test_invalid_capture_never_passes(self):
        for changes in ({"rc": 1}, {"nan": True}, {"image": None}, {"states": []}):
            with self.subTest(changes=changes):
                self.assertTrue(regress.compare(dict(self.good, **changes), self.good, False, 0))
        self.assertTrue(regress.compare(dict(self.good, rc=1), dict(self.good, rc=1), False, 0))

    def test_environment_is_controlled(self):
        with patch.dict(os.environ, {"IGN_TRACE": "1", "IGN_LIFT": "100", "IGNITION_DATA": "wrong"}):
            env = regress.controlled_environment("/data", "full")
        self.assertEqual(env["IGN_TRACE"], "full")
        self.assertEqual(env["IGNITION_DATA"], "/data")
        self.assertNotIn("IGN_LIFT", env)

    def test_sha256(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "file"
            path.write_bytes(b"abc")
            self.assertEqual(regress.sha256(path),
                             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")

    def test_failed_process_writes_failure_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary, report = Path(tmp) / "binary", Path(tmp) / "report.json"
            binary.write_bytes(b"not executed")
            argv = ["regress", "--binary", str(binary), "--report", str(report), "-k", "USA solo"]
            with patch.object(sys, "argv", argv), patch.object(
                    regress, "run_case", side_effect=subprocess.TimeoutExpired("fake", 600)), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(regress.main(), 1)
            self.assertEqual(json.loads(report.read_text())["status"], "fail")

    def test_empty_case_selection_fails(self):
        for module, argv in ((regress, ["regress", "-k", "nonexistent"]),
                             (solo_replay, ["solo", "-k", "nonexistent", "--report", "unused.json"])):
            with self.subTest(module=module.__name__), patch.object(sys, "argv", argv), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                module.main()
            self.assertEqual(error.exception.code, 2)

    def test_missing_binary_writes_failure_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "report.json"
            argv = ["solo", "--binary", str(Path(tmp) / "missing"), "--report", str(report)]
            with patch.object(sys, "argv", argv), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(solo_replay.main(), 1)
            self.assertEqual(json.loads(report.read_text())["status"], "fail")

    def test_missing_or_mutated_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp)
            for path in (data / "missing", data):
                with self.assertRaises(ValueError):
                    solo_replay.data_manifest(path)
            (data / "fixture").write_bytes(b"first")
            first = solo_replay.data_manifest(data)
            (data / "fixture").write_bytes(b"second")
            self.assertNotEqual(first, solo_replay.data_manifest(data))


class HeadlessTests(unittest.TestCase):
    binary = None
    data = None

    def setUp(self):
        if self.binary is None:
            self.skipTest("CLI checks require --binary and game data")

    def capture(self, ticks, trace_mode=None, option="--sim-ticks"):
        env = regress.controlled_environment(self.data, trace_mode)
        _, result = regress.run_case(str(self.binary), "contract",
                                    ["USA", option, str(ticks), "0", "--rivals", "0", "--car", "9"],
                                    env=env, trace=True)
        self.assertEqual(result["rc"], 0, result["output"])
        self.assertIsNotNone(result["image"])
        self.assertFalse(result["nan"])
        return result

    def test_exact_ticks_and_legacy_duration(self):
        for ticks in (0, 1, 576):
            with self.subTest(ticks=ticks):
                result = self.capture(ticks, "full")
                self.assertEqual(len(solo_replay.validate_trace(result["trace"], ticks)), ticks + 1)
        legacy = self.capture(8, "full", "--sim")
        self.assertEqual(len(solo_replay.validate_trace(legacy["trace"], 577)), 578)

    def test_tracing_does_not_change_results(self):
        captures = [self.capture(72, mode) for mode in (None, "1", "full")]
        for field in ("rc", "nan", "image", "states"):
            self.assertEqual(captures[0][field], captures[1][field])
            self.assertEqual(captures[0][field], captures[2][field])
        self.assertEqual(captures[0]["trace"].strip(), "")
        self.assertEqual(len(load_text(captures[1]["trace"])), 72)
        self.assertEqual(len(load_text(captures[2]["trace"])), 73)

    def test_invalid_tick_arguments_fail(self):
        for args in (["--sim-ticks"], ["--sim-ticks", "1"],
                     ["--sim-ticks", "-1", "0", "--shot", "unused.ppm"],
                     ["--sim-ticks", "1.5", "0", "--shot", "unused.ppm"],
                     ["--sim-ticks", "2147483648", "0", "--shot", "unused.ppm"],
                     ["--sim-ticks", "1", "0"]):
            with self.subTest(args=args):
                result = subprocess.run([str(self.binary)] + args, capture_output=True, text=True,
                                        timeout=10, env=regress.controlled_environment(self.data))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("--sim-ticks", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--data", default=os.getenv("IGNITION_DATA", "/Users/Shared/Epic Games/Ignition/ignition"))
    options, rest = parser.parse_known_args()
    HeadlessTests.binary = options.binary
    HeadlessTests.data = options.data
    unittest.main(argv=[sys.argv[0]] + rest)
