#!/usr/bin/env python3
"""Composed millisecond clock, normal start, car timers and turbo-input LE oracle."""
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

from input_original import InputOriginal
from car_original import CAR
from physics_original import ESP, STACK, RETURN, ROOT, ORIGINAL_SHA256

FIELDS = ("elapsed", "clock", "race_elapsed", "lap_elapsed", "lap_origin",
          "origin_ms", "request", "latch")


class ClockOriginal(InputOriginal):
    def __init__(self, executable):
        super().__init__(executable)
        self.visited_clock = set()
        self.allow_ranges = [(0x2B7D6, 0x2B7F9), (0x2B857, 0x2B8B0),
                             (0x2B8D9, 0x2B8FC), (0x2A410, 0x2A4EA),
                             (0x4C738, 0x4C7BE), (0x2C93A, 0x2C9CA),
                             (0x2CC9B, 0x2CCAF)]
        for address, expected in ((0xA8CAE, 0.001), (0xA8CC6, 0.036), (0xA8CCE, 200.0)):
            if self.double(address) != expected:
                raise ValueError("Unexpected timing constant")

    def double(self, address):
        return struct.unpack("<d", self.uc.mem_read(address, 8))[0]

    def code(self, uc, address, size, _):
        if address in self.boundaries:
            uc.emu_stop()
            return
        if not any(lo <= address <= hi for lo, hi in self.allow_ranges):
            raise ValueError(f"Unexpected clock instruction 0x{address:x}")
        self.visited_clock.add(address)

    def start_clock(self, values):
        super().start("P", dict(elapsed=values["elapsed"], latch=values["latch"],
                               mode=values["turbo_mode"], exploded=values["wrecked"], sunk=0, slid=0))
        for address, value in ((0xAB5DC, values["now"]), (0x1FEF04, values["origin"]),
                               (0x1FEEFC, values["race_mode"]), (0x1FEFE4, 1), (0x1FED30, 0),
                               (0x1FEF10, 0), (CAR + 0x374, values["lap"]), (CAR + 0x528, values["out"])):
            self.put(address, "i", value)
        self.put(0x1FB4E8, "d", values["clock"])
        self.put(CAR + 0x3AC, "d", values["lap_elapsed"])
        self.put(CAR + 0x3D4, "d", values["lap_origin"])
        self.put(0x1FB390, "d", 123)
        self.put(0x1FF367, "d", 123)
        self.allowed += [(CAR + 0x3A4, CAR + 0x3DC),
                         (0x1FB540, 0x1FB548), (0x1FB4E8, 0x1FB4F0),
                         (0x1FED30, 0x1FED34), (CAR + 0x4844, CAR + 0x4848),
                         (0x1FEF04, 0x1FEF08), (0x1FEF10, 0x1FEF14),
                         (0x1FEFE0, 0x1FEFE8), (0x1FEEFC, 0x1FEF00), (0x1FB390, 0x1FB398)]

    def execute(self, entry, boundaries, returns=False):
        for register in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + register), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, 0x037F)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.put(ESP, "I", RETURN)
        self.boundaries = set(boundaries)
        self.uc.emu_start(entry, RETURN + 1, timeout=1_000_000, count=10000)
        end = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
        if (end not in self.boundaries or
                self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + (4 if returns else 0) or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != 0x037F):
            raise ValueError(f"Clock stack/x87/boundary contract failed at 0x{end:x}")
        identity = f"0x{entry:x}->0x{end:x}"
        self.fragment_executions[identity] = self.fragment_executions.get(identity, 0) + 1
        return end

    def clock_step(self, values, tick):
        now = (values["now"] + tick * values["delta"]) & 0xFFFFFFFF
        self.put(0xAB5DC, "I", now)
        self.execute(0x2B7D6, (0x2B7FF,))
        elapsed = self.double(0x1FB540)
        boundary = self.execute(0x2B857, (0x2B8CD, 0x2B906))
        if boundary == 0x2B8CD:
            # Real normal-start reset and clock commit, separated from frame_time.
            # The already sampled global elapsed value is not recomputed after reset.
            self.execute(0x2A410, (RETURN,), returns=True)
            self.execute(0x2B8D9, (0x2B906,))
        self.execute(0x4C738, (RETURN,), returns=True)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_EAX, values["key"])
        self.fragment(0x2C93A, (0x2C9D4,))
        request = self.get(CAR + 0x52C)
        self.fragment(0x2CC9B, (0x2CCB5,))
        return [elapsed, self.double(0x1FB4E8), self.double(CAR + 0x3A4),
                self.double(CAR + 0x3AC), self.double(CAR + 0x3D4), self.get(0x1FEF04),
                request, self.get(CAR + 0x530)]


def fixtures():
    cases = []
    for now, origin, clock, race_mode, lap, out, wrecked, key, latch in itertools.product(
            (-1, 0, 249, 250, 251, 5555, 5556, 5557), (0, 100),
            (-2.0, -1.0, math.nextafter(-1.0, math.inf), 0.0),
            (0, 3), (0, 2, 3, 4), (0, 1), (0, 1), (0, 1, 2), (0, 1, 2)):
        cases.append((4, dict(now=now, origin=origin, delta=14, clock=clock,
                              race_mode=race_mode, lap=lap, out=out, wrecked=wrecked, key=key,
                              latch=latch, turbo_mode=0, elapsed=17.0, lap_elapsed=9.0, lap_origin=8.0)))
    for now, origin in ((2147483647, -1), (-2147483648, 1), (-2147483648, 2147483647),
                        (2147483647, -2147483648), (240, 0), (5540, 0)):
        for mode in (0, 1, 2):
            cases.append((25, dict(now=now, origin=origin, delta=14, clock=0.0,
                                   race_mode=0, lap=0, out=0, wrecked=0, key=1, latch=0,
                                   turbo_mode=mode, elapsed=0.0, lap_elapsed=0.0, lap_origin=0.0)))
    return cases


def encode(ticks, values):
    order = ("now", "origin", "delta", "clock", "race_mode", "lap", "out", "wrecked",
             "elapsed", "lap_elapsed", "lap_origin", "key", "latch", "turbo_mode")
    return f"C {ticks} " + " ".join(str(values[key]) for key in order)


def compare(row, expected):
    tokens = row.split()
    if len(tokens) != len(FIELDS):
        raise ValueError("Malformed race clock output width")
    actual = list(map(float, tokens))
    if not all(math.isfinite(value) for value in actual):
        raise ValueError("Nonfinite race clock output")
    if any(value != int(value) for value in actual[5:]):
        raise ValueError("Noninteger clock control output")
    return [dict(field=field, port=a, original=b)
            for field, a, b in zip(FIELDS, actual, expected) if a != b]


def verify(executable, runner):
    original = ClockOriginal(executable)
    cases = fixtures()
    text = "\n".join(encode(ticks, values) for ticks, values in cases) + "\n"
    result = subprocess.run([str(runner)], input=text, text=True, capture_output=True, check=True, timeout=60)
    rows = result.stdout.splitlines()
    if len(rows) != sum(ticks for ticks, _ in cases):
        raise ValueError("Clock oracle output row count mismatch")
    failures, cursor = [], 0
    for index, (ticks, values) in enumerate(cases):
        original.start_clock(values)
        for tick in range(ticks):
            expected = original.clock_step(values, tick)
            failures.extend(dict(case=index, tick=tick, **difference)
                            for difference in compare(rows[cursor], expected))
            cursor += 1
    required = {0x2B7E1, 0x2B7F9, 0x2B866, 0x2B896, 0x2B8B0, 0x2B8FC,
                0x2A42B, 0x2A460, 0x2A4CE, 0x4C753, 0x4C766, 0x4C777,
                0x4C783, 0x4C78B, 0x4C797, 0x4C7A5, 0x2C9CA, 0x2CCAF}
    if not required <= original.visited_clock:
        raise ValueError(f"Clock branch coverage missing: {sorted(required - original.visited_clock)}")
    import unicorn
    sources = ("src/game/car.c", "src/game/car.h", "src/game/race.c", "src/game/race.h",
               "src/platform/main.c", "tests/car_kernels.c", "tests/race_time_original.py",
               "tests/input_original.py", "tests/physics_original.py", "tests/car_original.py", "tools/le2elf.py")
    return dict(status="fail" if failures else "pass", cases=len(cases), steps=cursor, failures=failures,
                original_sha256=ORIGINAL_SHA256, runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                fixture_input_sha256=hashlib.sha256(text.encode()).hexdigest(),
                source_sha256={path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sources},
                observed_at=datetime.now(timezone.utc).isoformat(), host=platform.platform(),
                python=platform.python_version(), unicorn=unicorn.__version__, tolerance=0,
                required_addresses_executed=[hex(address) for address in sorted(required)],
                fragment_executions=original.fragment_executions,
                scope=["0x2b7d6..0x2b7ff signed timer difference and seconds conversion",
                       "0x2b857..0x2b8cd/0x2b906 normal countdown decision, supplied timer and origin",
                       "complete 0x2a410 reset, one car, and independent 0x2b8d9..0x2b906 start commit",
                       "complete 0x4c738, one car; composed P generation and latch with produced car time"],
                exclusions=["DOS timer interrupt, pause origin adjustment and frame_time scheduling",
                            "forced early start ([0x1fed30]==1), frame_time call at 0x2b8cd",
                            "other reset fields' port counterparts and best-lap statistics",
                            "multi-car original allocation/initialization and full frame/physics interleaving",
                            "intermediate P dispatcher, audio/effect calls, respawn controller and whole DOS race"])


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
    print(json.dumps({key: value for key, value in report.items()
                      if key in ("status", "cases", "steps", "error")}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
