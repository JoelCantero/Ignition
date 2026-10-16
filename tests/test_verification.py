#!/usr/bin/env python3
"""Synthetic verifier controls; optional --binary/--data enables headless CLI checks."""
import argparse
import contextlib
import io
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import regress
import solo_replay
import car_original
import input_original
import race_time_original
import respawn_original
import lap_original
import ai_stalls
from trace_compare import compare_rows, load_text


def trace(ticks):
    lines = []
    for tick in range(ticks + 1):
        row = dict.fromkeys(sorted(solo_replay.FIELDS), 0)
        row.update(t=tick / 72.0, tick=tick, trace_schema=5)
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
        self.assertEqual(len(solo_replay.FIELDS), 112)
        self.assertEqual(len(solo_replay.validate_trace(text, 2)), 3)
        for invalid in ("\n".join(text.splitlines()[1:]), "\n".join(text.splitlines()[::2]),
                        text.replace("trace_schema=5", "trace_schema=4"),
                        text.replace("b_corner_z3=0 ", ""), text.replace("tick=1", "tick=9"),
                        text.replace("b_mass=0", "b_mass=nan"),
                        text.replace("t=0.013888888888888888", "t=0.014")):
            with self.subTest(text=invalid), self.assertRaises(ValueError):
                solo_replay.validate_trace(invalid, 2)


class AiStallTests(unittest.TestCase):
    def row(self, tick, index=1, section=10, kmh=5, out=0):
        car = [section, 5, 0, 0, tick * 0.1, 0, kmh, kmh / 3.6, -7, 0, 0, 100, 0, 0, 0, 0, 0]
        return dict(schema=1, tick=tick, index=index,
                    case=[2, 0, 209, [car, list(car)], 1, [0] * 8, [0] * 9],
                    native=[100, 0, 0, 0, 0, 100, section, 0, 0, 1, 0, 0, 0],
                    motion=dict(lap=0, out=out, wall_hit=0),
                    applied=dict(drive=1000, steer=0, gear=1))

    def test_stalls_measure_duration_not_a_single_slow_snapshot(self):
        samples = [self.row(tick) for tick in range(2, 222, 4)]
        self.assertEqual(ai_stalls.intervals(samples[:10], "slow", 216), [])
        intervals = ai_stalls.intervals(samples, "slow", 216)
        self.assertEqual(len(intervals), 1)
        self.assertEqual(intervals[0]["seconds"], 3)
        self.assertEqual(intervals[0]["path_units"], intervals[0]["net_units"])

    def test_section_dwell_is_not_continuous_low_speed(self):
        samples = [self.row(tick, kmh=100 if ((tick - 2) // 4) % 2 else 5)
                   for tick in range(2, 366, 4)]
        self.assertEqual(ai_stalls.intervals(samples, "slow", 216), [])
        self.assertEqual(len(ai_stalls.intervals(samples, "section_dwell", 360)), 1)

    def test_finished_cars_gaps_laps_and_section_changes_break_intervals(self):
        samples = [self.row(tick) for tick in range(2, 222, 4)]
        for change in ("out", "gap", "lap", "section"):
            changed = json.loads(json.dumps(samples))
            if change == "out":
                changed[25]["motion"]["out"] = 1
            elif change == "gap":
                changed = changed[:25] + changed[26:]
            elif change == "lap":
                changed[25]["motion"]["lap"] = 1
            else:
                changed[25]["case"][3][0][0] = 11
            with self.subTest(change=change):
                self.assertEqual(ai_stalls.intervals(changed, "slow", 216), [])

    def test_diagnostic_schema_finiteness_and_per_car_order(self):
        row = self.row(2)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "trace.log"
            text = ai_stalls.PREFIX + json.dumps(row) + "\n"
            path.write_text("AI: unrelated diagnostic\n" + text)
            self.assertEqual(ai_stalls.load_trace(path, 209), [row])
            for invalid in ("", text + text, text.replace('"schema": 1', '"schema": 3'),
                            text.replace('"schema": 1', '"schema": 2'),
                            text.replace('"drive": 1000', '"drive": NaN'),
                            text.replace('"drive": 1000', '"drive": 1e999'),
                            text.replace('"index": 1', '"index": 2'),
                            text.replace('"native": [100,', '"native": [')):
                with self.subTest(text=invalid), self.assertRaises(ValueError):
                    path.write_text(invalid)
                    ai_stalls.load_trace(path, 209)


class CarVerifierTests(unittest.TestCase):
    def test_respawn_outputs_are_exact_and_finite(self):
        expected = [1, 0, 30, 0, 0, 0, 0, 0, 0, 37.25, 2, 29, 2, 17, 0, 1,
                    10.25, 4.25, 9.5, -1, -1]
        row = " ".join(map(str, expected))
        self.assertEqual(respawn_original.compare(row, expected), [])
        for index, (_, _, fmt) in enumerate((*respawn_original.FIELDS,
                                            ("request", None, "i"), ("event", None, "i"))):
            changed = list(expected)
            changed[index] += 1 if fmt == "i" else 1e-12
            with self.subTest(index=index):
                self.assertTrue(respawn_original.compare(row, changed))
                if fmt == "i":
                    changed[index] = 0.5
                    with self.assertRaises(ValueError):
                        respawn_original.compare(row, changed)
                    with self.assertRaises(ValueError):
                        respawn_original.compare(" ".join(map(str, changed)), expected)
        for invalid in (row + " 0", "", row.replace("37.25", "nan"),
                        row.replace("37.25", "inf"), row.replace("37.25", "garbage")):
            with self.assertRaises(ValueError):
                respawn_original.compare(invalid, expected)
        for invalid in (expected[:-1], expected + [0], [math.nan] + expected[1:]):
            with self.assertRaises(ValueError):
                respawn_original.compare(row, invalid)

    def test_clock_outputs_are_exact_and_finite(self):
        expected = [0.249, -1, 0.249, 0.149, 0.1, 0, 2, 2]
        row = " ".join(map(str, expected))
        self.assertEqual(race_time_original.compare(row, expected), [])
        for index in range(len(expected)):
            changed = list(expected)
            changed[index] += 1e-12 if index < 5 else 1
            self.assertTrue(race_time_original.compare(row, changed))
        for invalid in (row + " 0", "", row.replace("0.249", "nan"),
                        row.replace("0.249", "inf"), "0 0 0 0 0 0 0.5 0"):
            with self.assertRaises(ValueError):
                race_time_original.compare(invalid, expected)
        for invalid in (expected[:-1], expected + [0], [math.nan] + expected[1:],
                        expected[:-1] + [0.5]):
            with self.assertRaises(ValueError):
                race_time_original.compare(row, invalid)

    def test_lap_outputs_are_exact_and_finite(self):
        expected = [0] * 9 + [20.25] * 9
        row = " ".join(map(str, expected))
        self.assertEqual(lap_original.compare(row, expected), [])
        for i in range(len(expected)):
            changed = list(expected)
            changed[i] += 1 if i < 9 else 1e-12
            self.assertTrue(lap_original.compare(" ".join(map(str, changed)), expected))
        for invalid in ("", row + " 0", row.replace("20.25", "nan"), row.replace("20.25", "inf"),
                        "0.5 " + " ".join(map(str, expected[1:]))):
            with self.assertRaises(ValueError):
                lap_original.compare(invalid, expected)

    def test_input_integer_outputs_and_negative_controls(self):
        for mode, expected in (("D", [-100]), ("A", [1, 0, 7]), ("K", [2, 31]), ("H", [0]),
                               ("J", [2, 0, 29]), ("P", [2, 2])):
            with self.subTest(mode=mode):
                row = " ".join(map(str, expected))
                self.assertEqual(input_original.compare(mode, row, expected), [])
                changed = list(expected)
                changed[-1] += 1
                self.assertTrue(input_original.compare(mode, row, changed))
                for invalid in (row + " 0", row + " extra", "nan", "inf", "0.5", ""):
                    with self.assertRaises(ValueError):
                        input_original.compare(mode, invalid, expected)

    def test_perturbed_output_is_detected(self):
        differences, _ = car_original.compare_output("G", "0.4 7 8.001", [0.4, 7, 8])
        self.assertEqual(differences[0]["field"], "grip_r")
        self.assertFalse(car_original.compare_output("S", "1 0.2", [1, 0.2])[0])

    def test_flags_have_no_tolerance(self):
        differences, _ = car_original.compare_output("V", "5 0 0 0.000000000001 0", [5, 0, 0, 0, 0])
        self.assertEqual(differences[0]["field"], "airborne")
        for mode in ("T", "B", "U", "Q"):
            for index in car_original.EXACT[mode]:
                expected = [0] * len(car_original.FIELDS[mode])
                actual = list(expected)
                actual[index] = 1e-12
                differences, _ = car_original.compare_output(mode, " ".join(map(str, actual)), expected)
                self.assertEqual(differences[0]["field"], car_original.FIELDS[mode][index])

    def test_rev_output_controls(self):
        for mode in ("R", "Q"):
            expected = [0] * len(car_original.FIELDS[mode])
            actual = list(expected)
            actual[car_original.FIELDS[mode].index("revs")] = 0.001
            differences, _ = car_original.compare_output(mode, " ".join(map(str, actual)), expected)
            self.assertEqual(differences[0]["field"], "revs")
            for row in ("", "nan", " ".join(["0"] * (len(expected) + 1))):
                with self.subTest(mode=mode, row=row), self.assertRaises(ValueError):
                    car_original.compare_output(mode, row, expected)

    def test_malformed_output_fails(self):
        for row in ("", "1", "1 2 3", "nan 0", "0 inf", "1 garbage"):
            with self.subTest(row=row), self.assertRaises(ValueError):
                car_original.compare_output("S", row, [0, 0])

    def test_failed_run_replaces_success_report(self):
        errors = (ValueError("Wrong executable digest"), ModuleNotFoundError("unicorn"),
                  FileNotFoundError("missing runner"), subprocess.CalledProcessError(1, "runner"))
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "report.json"
            argv = ["car_original", "--original", "original", "--runner", "runner", "--report", str(report)]
            for error in errors:
                for verifier in (car_original, input_original, respawn_original, lap_original):
                    report.write_text('{"status":"pass"}')
                    with self.subTest(error=error, verifier=verifier.__name__), patch.object(sys, "argv", argv), \
                            patch.object(verifier, "verify", side_effect=error), \
                            contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                        self.assertEqual(verifier.main(), 1)
                    self.assertEqual(json.loads(report.read_text())["status"], "fail")


class CarKernelTests(unittest.TestCase):
    runner = None

    def setUp(self):
        if self.runner is None:
            self.skipTest("Car kernel checks require --runner")

    def test_invalid_lap_fixtures_fail(self):
        valid = "1 20 19 19 3 0 0 0 0 2 4 0 0 99.5 20.25 3.5 100.5 20.25 0".split()
        fixtures = ["L " + " ".join(valid[:-1]), "L " + " ".join(valid + ["extra"])]
        for index, value in ((0, 0), (1, 0), (2, -2147483648), (3, 10001), (4, 4), (5, -1),
                             (6, 3), (7, 2), (8, 4), (9, -1), (10, 9), (11, -10001),
                             (12, 10001), (13, "nan"), (14, "inf"), (15, "nan"),
                             (16, "inf"), (17, "nan"), (18, 3)):
            row = list(valid)
            row[index] = str(value)
            fixtures.append("L " + " ".join(row))
        for fixture in fixtures:
            with self.subTest(fixture=fixture):
                result = subprocess.run([str(self.runner)], input=fixture, capture_output=True,
                                        text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr.strip())

    def test_invalid_respawn_fixtures_fail(self):
        valid = "2 2 1 2 31 30 37.25 2 29 2 17 0 0 10.25 1 1 1 0".split()
        bad_values = {0: (-1, 4), 1: (0, 10001), 2: (0, 4), 3: (-1, 3),
                      4: (-1, 10001), 5: (-1, 10001), 6: (-1, 1001, "nan", "inf"),
                      7: (-1, 3), 8: (-10001, 10001), 9: (-1, 3), 10: (-1, 10001),
                      11: (-1, 2), 12: (-1, 2), 13: ("nan", "inf"), 14: (-1, 3),
                      15: (-1, 2), 16: (-2, 2), 17: (-1, 3)}
        fixtures = ["X " + " ".join(valid[:-1]), "X " + " ".join(valid + ["extra"])]
        for index, values in bad_values.items():
            for value in values:
                changed = list(valid)
                changed[index] = str(value)
                fixtures.append("X " + " ".join(changed))
        for fixture in fixtures:
            with self.subTest(fixture=fixture):
                result = subprocess.run([str(self.runner)], input=fixture, capture_output=True,
                                        text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr.strip())

    def test_invalid_fixtures_fail(self):
        for fixture in ("N nan", "N inf", "N 10001", "N", "N 0 extra",
                        "S 1 12 0 0 0 0 1 0 0", "S 1 0 0 0 0 nan 1 0 0",
                        "S 1 0 0 0 0 0 0 0 0", "G 0 0 4 1 0 0 0 0",
                        "G 0 0 0 1 0 0", "V 1 0 5 0 0 nan 0 0",
                        "T 1 12 0 0 1 0 0 0 0 0 0 1 0 0 0",
                        "T 1 0 0 0 1 0 0 0 0 0 0 1 0 0 0 100 nan 0 1 0 0",
                        "B 1 101 1 0 0 0", "B 1 50 1 nan 0 0",
                        "U 1 0 0 3 100 0 0 0 0 0 0", "U 1 0 0 1 nan 0 0 0 0 0 0",
                        "R 1 0 0 1 100 200 1 0 0 30 0 0", "R 1 0 0 1 100 -1 1 0 0 30 0 0",
                        "R 1 0 0 1 100 30 1 0 0 30 nan 0", "R 1 0 0 3 100 30 1 0 0 30 0 0",
                        "R 1 0 0 1 101 30 1 0 0 30 0 0", "R 1 0 0 1 100 30 1 0 0 30 0",
                        "Q 1 0 0 0 1 0 0 0 0 0 0 1 0 0 30 nan 0",
                        "Q 1 0 0 0 1 0 0 0 0 0 0 1 0 0 30 0 2",
                        "D 3 100 0 1 1 0 0 0 7", "A 1 101 0 1 1 0 0 0 7",
                        "D 1 100 0 1 1 nan 0 0 7", "A 1 100 0 1 1 0 inf 0 7",
                        "D 1 100 0 2 1 0 0 0 7", "A 1 100 0 1 1 0 0 0",
                        "K 0 1 0 30 0 0 0", "K 1 1 -1 30 0 0 0",
                        "K 1 1 0 -1 0 0 0", "K 1 1 0 30 nan 0 0",
                        "K 1 1 0 30 0 0 4", "K 1 1 0 30 0 0",
                        "H 1 0", "H 1 2", "H 3 1", "H 1",
                        "J 0 1 0 100 0 0 0 -1 0", "J 1 3 0 100 0 0 0 -1 0",
                        "J 1 1 3 100 0 0 0 -1 0", "J 1 1 0 nan 0 0 0 -1 0",
                        "J 1 1 0 -1 0 0 0 -1 0", "J 1 1 0 100 -10001 0 0 -1 0",
                        "J 1 1 0 100 0 2 0 -1 0", "J 1 1 0 100 0 0 2 -1 0",
                        "J 1 1 0 100 0 0 0 inf 0", "J 1 1 0 100 0 0 0 -1 3",
                        "J 1 1 0 100 0 0 0 -1",
                        "P 0 1 0 0 0 0 0 0 0", "P 1 3 0 0 0 0 0 0 0",
                        "P 1 1 3 0 0 0 0 0 0", "P 1 1 0 3 0 0 0 0 0",
                        "P 1 1 0 0 2 0 0 0 0", "P 1 1 0 0 0 2 0 0 0",
                        "P 1 1 0 0 0 0 2 0 0", "P 1 1 0 0 0 0 0 nan 0",
                        "P 1 1 0 0 0 0 0 0 3", "P 1 1 0 0 0 0 0 0", "unknown",
                        "C 0 0 0 14 0 0 0 0 0 0 0 0 1 0 0",
                        "C 1 0 0 -1 0 0 0 0 0 0 0 0 1 0 0",
                        "C 1 0 0 14 nan 0 0 0 0 0 0 0 1 0 0",
                        "C 1 0 0 14 0 4 0 0 0 0 0 0 1 0 0",
                        "C 1 0 0 14 0 0 5 0 0 0 0 0 1 0 0",
                        "C 1 0 0 14 0 0 0 2 0 0 0 0 1 0 0",
                        "C 1 0 0 14 0 0 0 0 2 0 0 0 1 0 0",
                        "C 1 0 0 14 0 0 0 0 0 inf 0 0 1 0 0",
                        "C 1 0 0 14 0 0 0 0 0 0 nan 0 1 0 0",
                        "C 1 0 0 14 0 0 0 0 0 0 0 inf 1 0 0",
                        "C 1 0 0 14 0 0 0 0 0 0 0 0 3 0 0",
                        "C 1 0 0 14 0 0 0 0 0 0 0 0 1 3 0",
                        "C 1 0 0 14 0 0 0 0 0 0 0 0 1 0 3", "C 1 0"):
            with self.subTest(fixture=fixture):
                result = subprocess.run([str(self.runner)], input=fixture, capture_output=True,
                                        text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr.strip())

    def test_declared_upward_following_deviation(self):
        rows = "V 1 100 5 0 -36 0 0 0\nV 1 100 5 0 -36 0 0 1\n"
        result = subprocess.run([str(self.runner)], input=rows, capture_output=True,
                                text=True, check=True, timeout=10)
        self.assertEqual(result.stdout.splitlines(), ["5 -36 -36 0 0", "5 0 -36 0 0"])

    def test_brake_lock_threshold_and_start(self):
        rows = "B 1 50 1 3 1 7\nB 1 51 1 3 0 7\nB 1 0 0 3 0 7\nB 1 81 1 3 0 7\n"
        result = subprocess.run([str(self.runner)], input=rows, capture_output=True,
                                text=True, check=True, timeout=10)
        self.assertEqual(result.stdout.splitlines(), ["0 7", "1 7", "1 7", "1 10"])

    def test_exhausted_turbo_does_not_add_force(self):
        rows = "U 1 0 0 1 2.5 19 0 1234 3 -4 0\nU 1 0 0 2 0 19 0 1234 3 -4 0\n"
        result = subprocess.run([str(self.runner)], input=rows, capture_output=True,
                                text=True, check=True, timeout=10)
        self.assertEqual(result.stdout.splitlines(), ["0 0 0 1234 3 -4"] * 2)

    def test_rev_ai_airborne_threshold_and_finish(self):
        rows = ("R 1 0 1 0 80 30 0 1 0 80 37 1\n"
                "R 1 0 1 0 81 30 0 1 0 80 37 1\n"
                "R 1 0 0 0 -100 30 0 1 0 80 37 1\n"
                "R 1 0 0 0 100 91 1 0 0 80 37 0\n")
        result = subprocess.run([str(self.runner)], input=rows, capture_output=True,
                                text=True, check=True, timeout=10)
        values = list(map(float, result.stdout.splitlines()))
        self.assertEqual(len(values), 4)
        for actual, expected in zip(values, (33, 37 + (81 - 37) / 6, (37 + (100 - 37) / 6) * 5 / 6, 37)):
            self.assertAlmostEqual(actual, expected)

    def test_rev_skid_then_human_finish(self):
        rows = ("R 1 0 0 0 0 199 1 0 1 59.999996185302734 37 1\n"
                "R 1 0 1 0 0 199 1 0 1 59.999996185302734 37 1\n"
                "R 1 0 1 0 0 199 1 0 1 60 37 1\n")
        result = subprocess.run([str(self.runner)], input=rows, capture_output=True,
                                text=True, check=True, timeout=10)
        values = list(map(float, result.stdout.splitlines()))
        self.assertEqual(len(values), 3)
        for actual, expected in zip(values, (47.5 * 5 / 6, 47.5, 37)):
            self.assertAlmostEqual(actual, expected)

    def test_wrong_original_rejected_with_persistent_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            executable, report = Path(tmp) / "wrong.exe", Path(tmp) / "report.json"
            executable.write_bytes(b"not the supported original executable")
            result = subprocess.run([sys.executable, str(Path(car_original.__file__)),
                                     "--original", str(executable), "--runner", str(self.runner),
                                     "--report", str(report)], capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            failure = json.loads(report.read_text())
            self.assertEqual(failure["status"], "fail")
            self.assertIn("Unsupported original SHA-256", failure["error"])


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

    def test_production_trace_uses_virtual_car_seconds(self):
        result = self.capture(18, "full")
        rows = solo_replay.validate_trace(result["trace"], 18)
        for tick, row in enumerate(rows):
            seconds = (tick * 1000 // 72) * 0.001
            self.assertEqual(row["race_elapsed"], seconds)
            self.assertEqual(row["lap_elapsed"], seconds)
            self.assertEqual(row["lap_origin"], 0)

    def test_autopilot_controls_follow_total_car_rotation(self):
        controls = ("in_a_accel", "in_a_brake", "in_a_steer", "in_turbo")
        for ncars in range(1, 9):
            with self.subTest(ncars=ncars):
                ticks = ncars * 12
                _, result = regress.run_case(
                    str(self.binary), "AI rotation",
                    ["CANADA", "--sim-ticks", str(ticks), "auto", "--rivals", str(ncars - 1)],
                    env=regress.controlled_environment(self.data, "full"), trace=True)
                self.assertEqual(result["rc"], 0, result["output"])
                self.assertEqual(len(result["states"]), ncars)
                self.assertIsNotNone(result["image"])
                rows = solo_replay.validate_trace(result["trace"], ticks)
                changed = []
                for tick in range(1, len(rows)):
                    before = tuple(rows[tick - 1][field] for field in controls)
                    after = tuple(rows[tick][field] for field in controls)
                    if before != after:
                        changed.append(tick)
                        self.assertEqual(tick % (2 * ncars), 0)
                self.assertTrue(changed, "autopilot never produced a control decision")
                self.assertEqual(changed[0], 2 * ncars)

    def test_ai_diagnostics_preserve_all_car_states_and_image(self):
        captures = []
        for mode in (None, "ai"):
            _, result = regress.run_case(
                str(self.binary), "AI diagnostic contract",
                ["USA", "--sim-ticks", "72", "auto", "--rivals", "7"],
                env=regress.controlled_environment(self.data, mode), trace=True)
            self.assertEqual(result["rc"], 0, result["output"])
            captures.append(result)
        for field in ("states", "image"):
            self.assertEqual(captures[0][field], captures[1][field])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "trace.log"
            path.write_text(captures[1]["output"])
            rows = ai_stalls.load_trace(path, 143)
        self.assertEqual(len(rows), 36)
        self.assertEqual({r["index"] for r in rows}, set(range(8)))
        for row in rows:
            self.assertEqual(row["tick"] % 16, row["index"] * 2)
            self.assertEqual(row["schema"], 2)
            self.assertGreaterEqual(row["case"][3][0][8], 0)
            self.assertLess(row["case"][3][0][8], 6.2831853071796)

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
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--data", default=os.getenv("IGNITION_DATA", "/Users/Shared/Epic Games/Ignition/ignition"))
    options, rest = parser.parse_known_args()
    HeadlessTests.binary = options.binary
    HeadlessTests.data = options.data
    CarKernelTests.runner = options.runner.resolve() if options.runner else None
    unittest.main(argv=[sys.argv[0]] + rest)
