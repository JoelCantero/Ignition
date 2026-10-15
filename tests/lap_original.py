#!/usr/bin/env python3
"""Compare lap bookkeeping with bounded, unmodified MAINDOS.EXE fragments."""
import argparse
from datetime import datetime, timezone
import hashlib
import itertools
import json
import math
from pathlib import Path
import platform
import struct
import subprocess
import sys

from car_original import CAR, CAR_SIZE, INFO
from input_original import InputOriginal
from physics_original import BODY, BODY_SIZE, ESP, STACK, ROOT, ORIGINAL_SHA256

FIELDS = ("event", "section", "lap_section", "phase", "lap", "out", "finished",
          "finish_position", "progress_lock", "last_lap", "lap1", "lap2", "lap3",
          "lap_origin", "best_lap", "race_elapsed", "lap_elapsed", "sample")
INPUTS = ("ticks", "count", "section", "lap_section", "phase", "lap", "lock", "out",
          "mode", "finished", "position", "probe", "section_sample", "race_elapsed",
          "lap_elapsed", "lap_origin", "sample", "best_lap", "pattern")


class LapOriginal(InputOriginal):
    def __init__(self, executable):
        super().__init__(executable)
        self.visited = set()

    def code(self, uc, address, size, _):
        if address in self.boundaries:
            uc.emu_stop()
            return
        if not any(lo <= address <= hi for lo, hi in
                   ((0x2F2BE, 0x2F376), (0x2F3A9, 0x2F430), (0x2F617, 0x2F63F),
                    (0x2FB84, 0x2FBD9), (0x2FC1C, 0x2FD58))):
            raise ValueError(f"Unexpected lap execution at 0x{address:x}")
        self.visited.add(address)

    def double(self, address):
        return struct.unpack("<d", self.uc.mem_read(address, 8))[0]

    def start_lap(self, v):
        self.active = "L"
        self.uc.mem_write(CAR, b"\xa5" * CAR_SIZE)
        self.uc.mem_write(BODY, b"\xa5" * BODY_SIZE)
        self.uc.mem_write(INFO, bytes(76))
        for address, value in ((0x1FEFE0, CAR), (0x1FEEFC, v["mode"]),
                               (0x1FEE60, v["finished"]), (0x1FF004, v["count"])):
            self.put(address, "I", value)
        for offset, value in ((0x364, v["section"]), (0x370, v["lap_section"]),
                              (0x378, v["phase"]), (0x374, v["lap"]), (0x604, v["lock"]),
                              (0x528, v["out"]), (0x39C, v["position"]), (0x3A0, 7)):
            self.put(CAR + offset, "i", value)
        for offset, value in ((0x3A4, v["race_elapsed"]), (0x3AC, v["lap_elapsed"]),
                              (0x3D4, v["lap_origin"]), (0x3B4, 11.25), (0x3BC, 12.25),
                              (0x3C4, 13.25), (0x3CC, 14.25)):
            self.put(CAR + offset, "d", value)
        self.put(0x1FB540, "d", v["sample"])
        self.put(0x1FB390, "d", v["best_lap"])
        self.allowed = [(STACK, STACK + 0x10000), (CAR + 0x364, CAR + 0x37C),
                        (CAR + 0x3A0, CAR + 0x3A4), (CAR + 0x3B4, CAR + 0x3DC),
                        (CAR + 0x528, CAR + 0x52C), (0x1FEE60, 0x1FEE64),
                        (0x1FB390, 0x1FB398)]

    def step_lap(self, v, tick):
        probe, section = v["probe"], v["section_sample"]
        if v["pattern"] == 1:
            q = v["count"] // 4
            probe = section = (q, 2 * q, 3 * q, v["count"] - 1, 0, 0)[tick % 6]
        elif v["pattern"] == 2:
            probe = section = 10000 if tick % 2 else -probe
        for register in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + register), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, self.fpcw)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        for offset in (0x2F4, 0x330, 0x334, 0x338, 0x344):
            self.put(ESP + offset, "i", 0)
        self.put(ESP + 0x2B0, "I", BODY)
        self.put(BODY + 0x38, "i", probe)
        self.put(ESP + 0x38, "i", section)
        self.put(ESP + 0x3C, "i", 123)
        before_car = bytes(self.uc.mem_read(CAR, CAR_SIZE))
        before_body = bytes(self.uc.mem_read(BODY, BODY_SIZE))
        before_info = bytes(self.uc.mem_read(INFO, 76))
        lap = self.get(CAR + 0x374)
        finished = self.get(0x1FEE60)
        end = self.fragment(0x2F2BE, (0x2F617, 0x2F3A9, 0x2F378))
        event = 0
        if end != 0x2F617:
            if self.get(CAR + 0x374) != lap + 1:
                raise ValueError("Crossing boundary did not increment lap")
            event = 1 | (2 if self.get(0x1FEE60) == finished + 1 else 0)
            end = self.fragment(0x2F3A9, (0x2F436, 0x2F4F9))
            if end == 0x2F436:
                event |= 4
        self.fragment(0x2F617, (0x2F645,))
        self.fragment(0x2FB84, (0x2FBDF,))
        self.fragment(0x2FC1C, (0x2FD63,))
        after = bytes(self.uc.mem_read(CAR, CAR_SIZE))
        for offset, (old, new) in enumerate(zip(before_car, after)):
            if old != new and not any(lo <= CAR + offset < hi for lo, hi in self.allowed):
                raise ValueError(f"Undeclared car byte changed: 0x{offset:x}")
        if before_body != bytes(self.uc.mem_read(BODY, BODY_SIZE)) or \
                before_info != bytes(self.uc.mem_read(INFO, 76)):
            raise ValueError("Lap bookkeeping changed body or controls")
        return [event, self.get(CAR + 0x364), self.get(CAR + 0x370), self.get(CAR + 0x378),
                self.get(CAR + 0x374), self.get(CAR + 0x528), self.get(0x1FEE60),
                self.get(CAR + 0x3A0), self.get(CAR + 0x604)] + [
                    self.double(CAR + o) for o in (0x3B4, 0x3BC, 0x3C4, 0x3CC, 0x3D4)] + [
                    self.double(0x1FB390), self.double(CAR + 0x3A4), self.double(CAR + 0x3AC),
                    self.double(0x1FB540)]


def fixtures():
    base = dict(ticks=1, count=20, section=19, lap_section=19, phase=3, lap=0, lock=0, out=0,
                mode=0, finished=2, position=4, probe=0, section_sample=0,
                race_elapsed=99.5, lap_elapsed=20.25, lap_origin=3.5, sample=100.5,
                best_lap=20.25, pattern=0)
    cases = []
    def add(**changes):
        cases.append(base | changes)
    for count in (1, 3, 4, 5, 7, 8, 19, 20, 21, 23):
        q = count // 4
        for section, phase in itertools.product(
                sorted({0, q - 1, q, 2*q - 1, 2*q, 3*q - 1, 3*q, 4*q - 1, 4*q,
                        count - 1, count, 10000}), range(4)):
            for sign in (-1, 1):
                add(count=count, section=sign*section, phase=phase, probe=10000, section_sample=sign*section)
    for lap, lock, mode, out, previous, probe in itertools.product(
            range(5), range(3), range(4), range(2), (19, -19, 18, 0), (0, 1, -1, 10000)):
        add(lap=lap, lock=lock, mode=mode, out=out, lap_section=previous, probe=probe)
    for elapsed, best, mode, lap in itertools.product(
            (-1.0, 0.0, math.nextafter(20.25, -math.inf), 20.25,
             math.nextafter(20.25, math.inf), 999.125),
            (0.0, 20.25, 1000.0), range(4), range(5)):
        add(lap_elapsed=elapsed, best_lap=best, mode=mode, lap=lap)
    for count, mode, sign in itertools.product((8, 20, 21, 23), range(4), (-1, 1)):
        add(ticks=30, count=count, section=0, lap_section=0, phase=0, pattern=1, best_lap=1000)
        add(ticks=6, count=count, probe=sign*(count - 1), pattern=2)
    return cases


def compare(row, expected):
    actual = [float(word) for word in row.split()]
    if len(actual) != len(FIELDS) or len(expected) != len(FIELDS):
        raise ValueError("Invalid lap output width")
    if any(not math.isfinite(x) for x in actual + expected) or \
            any(x != int(x) for x in actual[:9] + expected[:9]):
        raise ValueError("Non-finite or fractional lap output")
    return [{"field": field, "original": want, "port": got}
            for field, want, got in zip(FIELDS, expected, actual) if want != got]


def verify(executable, runner):
    original = LapOriginal(executable)
    cases = fixtures()
    text = "".join("L " + " ".join(str(v[k]) for k in INPUTS) + "\n" for v in cases)
    result = subprocess.run([str(runner)], input=text, text=True, capture_output=True,
                            check=True, timeout=60)
    rows = result.stdout.splitlines()
    if len(rows) != sum(v["ticks"] for v in cases):
        raise ValueError("Unexpected lap runner row count")
    failures, cursor = [], 0
    for index, v in enumerate(cases):
        original.start_lap(v)
        for tick in range(v["ticks"]):
            differences = compare(rows[cursor], original.step_lap(v, tick))
            if differences:
                failures.append(dict(case=index, tick=tick, input=v, differences=differences))
            cursor += 1
    required = {0x2F319, 0x2F325, 0x2F344, 0x2F35B, 0x2F3C3, 0x2F3D6, 0x2F409, 0x2F430,
                0x2F63F, 0x2FBCF, 0x2FC85, 0x2FCFB, 0x2FD58, 0x2FD51}
    if not required <= original.visited:
        raise ValueError("Incomplete lap branch coverage")
    sources = ("src/game/car.c", "src/game/car.h", "src/game/race.c", "src/game/race.h",
               "src/platform/main.c", "tests/car_kernels.c", "tests/lap_original.py",
               "tests/input_original.py", "tests/car_original.py", "tests/physics_original.py", "tools/le2elf.py")
    import unicorn
    return dict(status="fail" if failures else "pass", cases=len(cases), steps=cursor, failures=failures,
                fields=FIELDS, tolerance=0, original_sha256=ORIGINAL_SHA256,
                runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                fixture_input_sha256=hashlib.sha256(text.encode()).hexdigest(),
                source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources},
                observed_at=datetime.now(timezone.utc).isoformat(), host=platform.platform(),
                python=platform.python_version(), unicorn=unicorn.__version__,
                required_addresses_executed=[hex(a) for a in sorted(required)],
                fragment_executions=original.fragment_executions,
                scope=["crossing and finish 0x2f2be up to 0x2f378/0x2f3a9/0x2f617",
                       "lap slots, sampled origin and strict best comparison 0x2f3a9..0x2f436/0x2f4f9",
                       "lap probe retention 0x2f617..0x2f645",
                       "section acceptance 0x2fb84..0x2fbdf and phase decision 0x2fc1c..0x2fd63",
                       "supplied independent probe families; preserved other car/body/control bytes"],
                exclusions=["probe geometry, section+0x368/+0x36c port counterparts, progress-lock producer",
                            "out/wreck eligibility caller, full physics/timer interleaving, elimination controller",
                            "finish camera 0x2f378, localized notifications and track-record comparison 0x2f4f9",
                            "best-lap seed from saved table, saved records and persistence; port uses session seed",
                            "whole DOS race and global visual/audio equivalence"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "runner", "report"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    args.report.write_text('{"status":"running"}\n')
    try:
        report = verify(args.original, args.runner.resolve())
    except (ValueError, OSError, ModuleNotFoundError, subprocess.SubprocessError) as error:
        report = dict(status="fail", error=str(error))
        print(f"FAIL: {error}", file=sys.stderr)
    args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k in ("status", "cases", "steps", "error")}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
