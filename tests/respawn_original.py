#!/usr/bin/env python3
"""Bounded initialization, wreck turbo and respawn controls against unchanged LE bytes."""
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
from car_original import CAR, CAR_SIZE, INFO
from physics_original import ESP, ROOT, ORIGINAL_SHA256

# Independent Watcom offsets; the port's structure layout is not used.
FIELDS = (
    ("gear", 0x27C, "i"), ("counter", None, "i"), ("delay", None, "i"),
    ("revs", 0x288, "d"), ("jump", 0x274, "i"), ("landed", 0x278, "i"),
    ("skid", 0x2F8, "i"), ("spark", 0x33C, "i"), ("slip", 0x90, "d"),
    ("charge", 0x534, "d"), ("mode", 0x53C, "i"), ("wait", 0x548, "i"),
    ("latch", 0x530, "i"), ("frames", 0x54C, "i"), ("ai", None, "i"),
    ("out", 0x528, "i"), ("elapsed", 0x3A4, "d"),
    ("lap_elapsed", 0x3AC, "d"), ("lap_origin", 0x3D4, "d"),
)
INPUTS = ("stage", "ticks", "kind", "gear", "counter", "delay", "charge", "mode",
          "wait", "latch", "frames", "ai", "out", "elapsed", "key", "manual", "shift", "pattern")


class RespawnOriginal(InputOriginal):
    def __init__(self, executable):
        super().__init__(executable)
        self.visited_reset = set()
        self.reset_ranges = [(0x27451, 0x2748D), (0x3A7F5, 0x3A80B),
                             (0x3C26E, 0x3C284), (0x3DBB4, 0x3DBC8),
                             (0x41DE5, 0x41E83), (0x41E93, 0x41E93), (0x41EE5, 0x41F03)]

    def code(self, uc, address, size, data):
        if self.active == "K" and address in self.boundaries:
            uc.emu_stop()
            return
        if self.active != "X":
            return super().code(uc, address, size, data)
        if address in self.boundaries:
            uc.emu_stop()
            return
        if not any(lo <= address <= hi for lo, hi in self.reset_ranges):
            raise ValueError(f"Unexpected reset instruction 0x{address:x}")
        self.visited_reset.add(address)

    def reset_registers(self):
        for name in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + name), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, self.fpcw)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)

    def start_reset(self, v):
        super().start("J", v)
        self.put(CAR + 0x530, "i", v["latch"])
        self.put(CAR + 0x54C, "i", v["frames"])
        self.put(CAR + 0x288, "d", 31.5)
        self.put(CAR + 0x90, "d", 2.5)
        self.put(CAR + 0x274, "2i", 1, 1)
        self.put(CAR + 0x33C, "i", 13)
        self.put(CAR + 0x3A4, "2d", v["elapsed"], 4.25)
        self.put(CAR + 0x3D4, "d", 9.5)
        self.put(CAR + 0x354, "3i", 0, 0, 0)
        for i in range(200):
            self.put(0x1FB6D8 + 2 * i, "h", 0)
            self.put(0x1FB868 + 2 * i, "h", 1)
        self.put(0x1FB4E8, "d", -1)
        self.reset_registers()
        if v["stage"] == 3:
            self.reset_fragment(2, v["kind"])

    def reset_fragment(self, stage, kind):
        self.active = "X"
        self.reset_registers()
        self.uc.reg_write(self.reg.UC_X86_REG_EDX, CAR)
        before = bytes(self.uc.mem_read(CAR, CAR_SIZE))
        info_before = bytes(self.uc.mem_read(INFO, 76))
        if stage == 0:
            ranges = [(0x534, 0x550)]
            fragments = [(0x27451, 0x27497)]
        elif stage == 1:
            ranges = [(0x534, 0x540)]
            fragments = [{1: (0x3A7F5, 0x3A812), 2: (0x3C26E, 0x3C28B),
                          3: (0x3DBB4, 0x3DBCE)}[kind]]
            if kind == 3:
                self.uc.reg_write(self.reg.UC_X86_REG_EAX, CAR)
        else:
            ranges = [(0x344, 0x354), (0x27C, 0x280), (0x298, 0x2A0),
                      (0x118, 0x120), (0x274, 0x27C), (0x288, 0x290),
                      (0x2F8, 0x2FC), (0x33C, 0x340), (0x88, 0x98)]
            fragments = [(0x41DE5, 0x41E8D), (0x41E93, 0x41E9D), (0x41EE5, 0x41F0D)]
        self.allowed = [(ESP - 0x8000, ESP + 0x8000)] + [(CAR + lo, CAR + hi) for lo, hi in ranges]
        if stage == 2:
            self.allowed += [(INFO + 16, INFO + 20)]
        for entry, end in fragments:
            self.fragment(entry, (end,))
        mutable = {index for lo, hi in ranges for index in range(lo, hi)}
        after = self.uc.mem_read(CAR, CAR_SIZE)
        if any(before[i] != after[i] for i in range(CAR_SIZE) if i not in mutable):
            raise ValueError("Reset changed an undeclared car byte")
        info_after = self.uc.mem_read(INFO, 76)
        if any(info_before[i] != info_after[i] for i in range(76) if not (stage == 2 and 16 <= i < 20)):
            raise ValueError("Reset changed an undeclared control byte")

    def gearbox_step(self):
        self.active = "K"
        self.reset_registers()
        end = self.fragment(0x502CC, (0x5040E, 0x50449, 0x5045B))
        if end != 0x5045B:
            # Separate post-audio commit, using the decision's EDI and car index ESI.
            self.fragment(0x50449, (0x5045B,))

    def step_reset(self, v, tick):
        request = event = -1
        if v["stage"] != 3:
            self.reset_fragment(v["stage"], v["kind"])
        else:
            sampled = (v["key"] if v["pattern"] == 0 else
                       (v["key"] if tick % 2 else 0) if v["pattern"] == 1 else
                       (0 if tick == 1 else v["key"]))
            self.allowed = [(ESP - 0x8000, ESP + 0x8000),
                            (CAR + 0x52C, CAR + 0x534), (CAR + 0x53C, CAR + 0x540),
                            (CAR + 0x548, CAR + 0x54C), (CAR + 0x27C, CAR + 0x280),
                            (INFO + 16, INFO + 20)]
            self.reset_registers()
            self.active = "P"
            if v["ai"]:
                # AI request source is supplied, not reconstructed from its controller.
                request = int(sampled == 1)
                self.put(CAR + 0x52C, "i", request)
            else:
                self.uc.reg_write(self.reg.UC_X86_REG_EAX, sampled)
                self.fragment(0x2C93A, (0x2C9D4,))
                request = self.get(CAR + 0x52C)
            self.active = "J"
            event = self.step("J", dict(request=request, pattern=0))[0]
            if v["manual"] and v["shift"]:
                self.active = "H"
                self.step("H", dict(shift=v["shift"]))
            elif not v["manual"]:
                self.gearbox_step()
            self.active = "P"
            self.reset_registers()
            self.fragment(0x2CC9B, (0x2CCB5,))
        result = []
        for name, offset, fmt in FIELDS:
            address = (INFO + {"counter": 16, "delay": 20, "ai": 4}[name]
                       if offset is None else CAR + offset)
            result.append(struct.unpack("<" + fmt, self.uc.mem_read(address, struct.calcsize(fmt)))[0])
        return result + [request, event]


def fixtures():
    base = dict(stage=2, ticks=2, kind=1, gear=2, counter=31, delay=30,
                charge=37.25, mode=2, wait=29, latch=2, frames=17, ai=0, out=0,
                elapsed=10.25, key=1, manual=1, shift=1, pattern=0)
    cases = []
    for stage, kind, gear, counter, mode, latch, out in itertools.product(
            range(3), range(1, 4), range(3), (0, 30, 31), range(3), range(3), range(2)):
        cases.append(dict(base, stage=stage, kind=kind, gear=gear, counter=counter,
                          mode=mode, latch=latch, out=out))
    for ai, out, delay, mode, latch, key, gearbox, pattern, elapsed in itertools.product(
            range(2), range(2), (0, 30), range(3), range(3), range(3),
            ((0, 0), (1, -1), (1, 0), (1, 1)), (0, 2),
            (math.nextafter(0.25, -math.inf), 0.25, 10.25)):
        manual, shift = gearbox
        cases.append(dict(base, stage=3, ticks=5, ai=ai, out=out, delay=delay, mode=mode,
                          latch=latch, key=key, manual=manual, shift=shift, pattern=pattern,
                          elapsed=elapsed, charge=100))
    charges = (0, math.nextafter(20, -math.inf), 20, math.nextafter(20, math.inf),
               math.nextafter(100, -math.inf), 100, math.nextafter(100, math.inf), 101)
    for stage, kind, charge, wait, frames in itertools.product(
            range(4), range(1, 4), charges, (-1, 0, 1, 30), (0, 99)):
        cases.append(dict(base, stage=stage, kind=kind, charge=charge, wait=wait, frames=frames,
                          mode=0 if stage == 3 else 2, ticks=5 if stage == 3 else 2))
    for ai, out, pattern in itertools.product(range(2), range(2), range(3)):
        cases.append(dict(base, stage=3, ticks=33, ai=ai, out=out, manual=0, shift=0,
                          pattern=pattern, charge=100))
    return cases


def compare(row, expected):
    names = [field[0] for field in FIELDS] + ["request", "event"]
    tokens = row.split()
    if len(tokens) != len(names) or len(expected) != len(names):
        raise ValueError("Malformed respawn output width")
    actual = list(map(float, tokens))
    if not all(math.isfinite(value) for value in actual + list(expected)):
        raise ValueError("Nonfinite respawn output")
    integers = [i for i, field in enumerate(FIELDS) if field[2] == "i"] + [19, 20]
    if any(values[i] != int(values[i]) for values in (actual, expected) for i in integers):
        raise ValueError("Noninteger respawn output")
    return [dict(field=name, port=a, original=b)
            for name, a, b in zip(names, actual, expected) if a != b]


def verify(executable, runner):
    original = RespawnOriginal(executable)
    cases = fixtures()
    text = "\n".join("X " + " ".join(str(v[name]) for name in INPUTS) for v in cases) + "\n"
    result = subprocess.run([str(runner)], input=text, text=True, capture_output=True, check=True, timeout=60)
    rows = result.stdout.splitlines()
    if len(rows) != sum(v["ticks"] for v in cases):
        raise ValueError("Respawn output row count mismatch")
    failures, cursor = [], 0
    for index, v in enumerate(cases):
        original.start_reset(v)
        for tick in range(v["ticks"]):
            expected = original.step_reset(v, tick)
            failures.extend(dict(case=index, tick=tick, **diff) for diff in compare(rows[cursor], expected))
            cursor += 1
    required = {0x27451, 0x27483, 0x2748D, 0x3A7F5, 0x3A80B, 0x3C26E, 0x3C284,
                0x3DBB4, 0x3DBC8, 0x41E17, 0x41E21, 0x41E6F, 0x41E83, 0x41E93, 0x41F03}
    if not required <= original.visited_reset:
        raise ValueError("Incomplete reset instruction coverage")
    sources = ("src/game/car.c", "src/game/car.h", "src/game/race.c", "tests/car_kernels.c",
               "tests/respawn_original.py", "tests/input_original.py", "tests/car_original.py",
               "tests/physics_original.py", "tools/le2elf.py")
    return dict(status="fail" if failures else "pass", cases=len(cases), steps=cursor, failures=failures,
                original_sha256=ORIGINAL_SHA256, runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                source_sha256={path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sources},
                fixture_input_sha256=hashlib.sha256(text.encode()).hexdigest(), tolerance=0,
                observed_at=datetime.now(timezone.utc).isoformat(), host=platform.platform(),
                required_addresses_executed=[hex(a) for a in sorted(required)],
                fragment_executions=original.fragment_executions,
                scope=["turbo initialization 0x27451..0x27497",
                       "three wreck-effect turbo refresh fragments",
                       "respawn reset 0x41de5..0x41e8d, 0x41e93..0x41e9d, 0x41ee5..0x41f0d",
                       "composed P/J/H/K/latch on supplied post-placement state, five or 33 updates",
                       "K decision 0x502cc up to audio/commit/epilogue, independent 0x50449 commit"],
                exclusions=["full initialization, allocation and initial control-table setup",
                            "geometry, collision/wreck eligibility, placement and completion FX",
                            "finished-car eligibility gate and exception car+0x604==1",
                            "charge refill at wreck completion and wreck-flag clearing",
                            "throttle/turbo arithmetic, audio, AI request source and full tick interleaving"])


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
