#!/usr/bin/env python3
"""Locate sustained rival stalls and replay actual decisions through the DOS controller.

This diagnoses the port's trajectories; it is not a full DOS race replay.
"""
import argparse
import copy
from collections import defaultdict
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile

from regress import controlled_environment
from ai_drive_original import DriveOriginal, NAMES, UndefinedOriginalRead, runtime_sections
from physics_original import ESP, FPCW, ORIGINAL_SHA256, STACK
from ai_drive_original import BODIES, CARS

PREFIX = "AI_DIAG "


class StallOriginal(DriveOriginal):
    normalizing = False

    def code(self, uc, address, size, data):
        if not self.normalizing:
            return super().code(uc, address, size, data)
        if address == 0x2D9EE:
            uc.emu_stop()
        elif not (0x2D9C7 <= address <= 0x2D9E8 or 0x5192E <= address <= 0x5197C):
            raise ValueError(f"Unexpected heading-copy instruction 0x{address:x}")

    def write(self, uc, access, address, size, value, data):
        if not self.normalizing:
            return super().write(uc, access, address, size, value, data)
        if not (STACK <= address and address + size <= STACK + 0x10000 or
                CARS + 0xF8 <= address and address + size <= CARS + 0x100):
            raise ValueError(f"Unexpected heading-copy write 0x{address:x}")

    def car_heading(self, body_heading):
        if not math.isfinite(body_heading):
            raise ValueError("Original heading copy requires a finite body angle")
        self.put(0x1FEFE0, "I", CARS)
        self.put(0x1FEFE8, "I", BODIES)
        self.put(BODIES + 0x28, "d", body_heading)
        for register, value in (("ESP", ESP), ("ESI", 0), ("EDI", 0), ("FPCW", FPCW),
                                ("FPTAG", 0xFFFF)):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + register), value)
        self.normalizing = True
        try:
            self.uc.emu_start(0x2D9C7, 0x2D9EE, count=100000)
        finally:
            self.normalizing = False
        if (self.uc.reg_read(self.reg.UC_X86_REG_EIP) != 0x2D9EE or
                self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != FPCW or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF):
            raise ValueError("Original heading copy violated its execution/stack/x87 contract")
        return struct.unpack("<d", self.uc.mem_read(CARS + 0xF8, 8))[0]


def reject_constant(value):
    raise ValueError(f"Non-finite diagnostic value: {value}")


def load_trace(path, count):
    rows = []
    previous = {}
    with path.open() as stream:
        for line in stream:
            if not line.startswith(PREFIX):
                continue
            row = json.loads(line[len(PREFIX):], parse_constant=reject_constant)
            tick, index = row["tick"], row["index"]
            case = row["case"]
            if (row["schema"] not in (1, 2) or not isinstance(tick, int) or tick < 1 or len(case) != 7 or
                    not isinstance(index, int) or not 0 <= index < len(case[3]) or
                    tick <= previous.get(index, 0) or case[2] != count or
                    len(case[5]) != 8 or len(case[6]) != 9 or
                    any(len(car) != 17 for car in case[3]) or len(row["native"]) != len(NAMES)):
                raise ValueError(f"Invalid diagnostic schema/order at tick {tick}, car {index}")
            if row["schema"] == 2 and "body_heading" not in row["motion"]:
                raise ValueError("Diagnostic schema 2 requires the separate physical body heading")
            def finite(value):
                if isinstance(value, dict):
                    return all(finite(v) for v in value.values())
                if isinstance(value, list):
                    return all(finite(v) for v in value)
                return isinstance(value, (int, float)) and math.isfinite(value)
            if not finite(row):
                raise ValueError(f"Non-finite/non-numeric diagnostic at tick {tick}, car {index}")
            previous[index] = tick
            rows.append(row)
    if not rows:
        raise ValueError(f"No decision samples in {path}")
    return rows


def describe_interval(samples, kind):
    first, last = samples[0], samples[-1]
    cars = [s["case"][3][0] for s in samples]
    path = sum(math.hypot(b[4] - a[4], b[5] - a[5]) for a, b in zip(cars, cars[1:]))
    net = math.hypot(cars[-1][4] - cars[0][4], cars[-1][5] - cars[0][5])
    return dict(kind=kind, index=first["index"], model=cars[0][1],
                start_tick=first["tick"], end_tick=last["tick"],
                seconds=(last["tick"] - first["tick"]) / 72,
                section=cars[0][0], lap=first["motion"]["lap"], samples=len(samples),
                min_kmh=min(c[6] for c in cars), max_kmh=max(c[6] for c in cars),
                path_units=path, net_units=net,
                wall_samples=sum(s["motion"]["wall_hit"] != 0 for s in samples),
                airborne_samples=sum(c[2] != 0 for c in cars),
                reverse_decisions=sum(s["native"][0] == 0 and s["native"][1] > 0 for s in samples),
                normalized_defined_samples=sum("heading_copy" in s for s in samples),
                normalized_control_changes=sum(
                    s["heading_copy"]["controls"] != s["native"][:4]
                    for s in samples if "heading_copy" in s),
                first=first, last=last)


def intervals(rows, kind, minimum_ticks):
    by_car = defaultdict(list)
    for row in rows:
        by_car[row["index"]].append(row)
    result = []
    for samples in by_car.values():
        active = []
        def finish():
            if active and active[-1]["tick"] - active[0]["tick"] >= minimum_ticks:
                result.append(describe_interval(active, kind))
        for row in samples:
            car = row["case"][3][0]
            eligible = not row["motion"]["out"] and (kind == "section_dwell" or abs(car[6]) < 10)
            continuous = active and (
                row["tick"] - active[-1]["tick"] <= 2 * len(row["case"][3]) and
                row["motion"]["lap"] == active[-1]["motion"]["lap"] and
                car[0] == active[-1]["case"][3][0][0])
            if not eligible or not continuous:
                finish()
                active = []
            if eligible:
                active.append(row)
        finish()
    return sorted(result, key=lambda r: (-r["seconds"], r["start_tick"], r["index"]))


def replay(rows, sections, original):
    failures, undefined = [], []
    checked = 0
    normalization = dict(outside_heading=0, changed_controls=0, changed_stuck=0,
                         first_heading_difference=None, examples=[], copy_mismatches=0,
                         accumulated_body_outside=0)
    for row in rows:
        case = row["case"] + [sections]
        heading = case[3][0][8]
        body_heading = row["motion"]["body_heading"] if row["schema"] == 2 else heading
        normalized = original.car_heading(body_heading)
        normalization["accumulated_body_outside"] += not 0 <= body_heading < 6.2831853071796
        normalization["outside_heading"] += not 0 <= heading < 6.2831853071796
        if not 0 <= normalized < 6.2831853071796:
            raise ValueError("Original heading copy did not normalize its output")
        if normalized != heading:
            normalization["copy_mismatches"] += 1
            if normalization["first_heading_difference"] is None:
                normalization["first_heading_difference"] = dict(
                    tick=row["tick"], index=row["index"], body_heading=body_heading,
                    port_heading=heading, car_heading=normalized)
        try:
            expected = original.run(case, True)
        except UndefinedOriginalRead as error:
            undefined.append(dict(tick=row["tick"], index=row["index"],
                                  section=case[3][0][0], reason=str(error)))
            continue
        checked += 1
        different = []
        for name, actual, want in zip(NAMES, row["native"], expected):
            if not math.isfinite(want) or (actual != want and not (
                    isinstance(want, float) and math.isclose(actual, want, rel_tol=2e-13, abs_tol=2e-13))):
                different.append(dict(field=name, port=actual, original=want))
        if different:
            failures.append(dict(tick=row["tick"], index=row["index"], differences=different))
        corrected_output = expected
        if normalized != heading:
            corrected = list(case)
            corrected[3] = copy.deepcopy(case[3])
            corrected[3][0][8] = normalized
            corrected_output = original.run(corrected, True)
            controls_changed = corrected_output[:4] != expected[:4]
            stuck_changed = corrected_output[4] != expected[4]
            normalization["changed_controls"] += controls_changed
            normalization["changed_stuck"] += stuck_changed
            if (controls_changed or stuck_changed) and len(normalization["examples"]) < 20:
                normalization["examples"].append(dict(
                    tick=row["tick"], index=row["index"], section=case[3][0][0],
                    body_heading=heading, car_heading=normalized,
                    raw_controls=expected[:4], normalized_controls=corrected_output[:4],
                    raw_stuck=expected[4], normalized_stuck=corrected_output[4]))
        row["heading_copy"] = dict(heading=normalized, controls=corrected_output[:4],
                                   stuck=corrected_output[4])
    return dict(checked=checked, mismatched=len(failures), first_mismatch=failures[0] if failures else None,
                mismatches=failures, undefined_original_reads=undefined,
                heading_copy_counterfactual=normalization)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--records", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--tracks", nargs="+", default=["CARIB"])
    parser.add_argument("--ticks", type=int, default=14400)
    args = parser.parse_args()
    if args.ticks < 1:
        parser.error("--ticks must be positive")
    tables = json.loads(args.records.read_text())
    if len(set(args.tracks)) != len(args.tracks) or any(t not in tables for t in args.tracks):
        parser.error("--tracks must be distinct names from the original runtime export")
    original = StallOriginal(args.original)
    report = dict(timestamp=datetime.now(timezone.utc).isoformat(),
                  status="running",
                  authority="captured-port-states/original-controller-only",
                  original_sha256=ORIGINAL_SHA256,
                  binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                  records_sha256=hashlib.sha256(args.records.read_bytes()).hexdigest(),
                  ticks=args.ticks, slow_threshold_kmh=10, minimum_slow_seconds=3,
                  minimum_dwell_seconds=5, tracks={})
    args.report.parent.mkdir(parents=True, exist_ok=True)
    for name in args.tracks:
        data = tables[name]
        trace = args.report.parent / f"ai-stalls-{name}.log"
        for suffix, expected_hash in data["asset_sha256"].items():
            asset = args.data / "LEVELS" / name / f"{name}.{suffix}"
            if hashlib.sha256(asset.read_bytes()).hexdigest() != expected_hash:
                raise ValueError(f"Original runtime export does not match {asset}")
        env = controlled_environment(args.data, "ai")
        with tempfile.TemporaryDirectory(prefix="ignition-ai-stalls-") as temporary:
            with trace.open("w") as stream:
                result = subprocess.run(
                    [str(args.binary.resolve()), name, "--sim-ticks", str(args.ticks), "auto",
                     "--rivals", "7", "--shot", str(Path(temporary) / "shot.ppm")],
                    env=env, stdout=subprocess.PIPE, stderr=stream, text=True, check=True, timeout=180)
        rows = load_trace(trace, len(data["sections"]))
        if {row["index"] for row in rows} != set(range(8)):
            raise ValueError(f"{name}: missing car decisions")
        if any(row["case"][0] != data["track_id"] for row in rows):
            raise ValueError(f"{name}: trace/runtime track ID mismatch")
        comparison = replay(rows, runtime_sections(data), original)
        slow = intervals(rows, "slow", 3 * 72)
        dwells = intervals(rows, "section_dwell", 5 * 72)
        report["tracks"][name] = dict(
            asset_sha256=data["asset_sha256"], trace_sha256=hashlib.sha256(trace.read_bytes()).hexdigest(),
            trace=str(trace), decisions=len(rows), controller=comparison, slow_intervals=slow,
            section_dwells=dwells, final_state=result.stdout)
        args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        print(f"{name}: {len(rows)} decisions, {comparison['mismatched']} controller mismatches, "
              f"{comparison['heading_copy_counterfactual']['copy_mismatches']} heading-copy mismatches, "
              f"{len(comparison['undefined_original_reads'])} undefined original reads, "
              f"{len(slow)} slow intervals, {len(dwells)} section dwells", flush=True)
        for interval in dwells[:4]:
            print(f"  car {interval['index']} section {interval['section']}: "
                  f"{interval['seconds']:.2f}s, {interval['min_kmh']:.2f}..{interval['max_kmh']:.2f} km/h, "
                  f"{interval['wall_samples']}/{interval['samples']} wall samples", flush=True)
    report["status"] = "complete"
    args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()
