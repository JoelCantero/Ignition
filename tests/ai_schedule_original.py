#!/usr/bin/env python3
"""Bounded LE evidence for AI turns, retained input and the stuck counter."""
import argparse
from datetime import datetime, timezone
import hashlib
import itertools
import json
import math
from pathlib import Path
import struct
import subprocess

from input_original import InputOriginal
from car_original import CAR, INFO
from physics_original import BODY, ESP, STACK, ROOT, ORIGINAL_SHA256

AI = 0x430000


class ScheduleOriginal(InputOriginal):
    def __init__(self, executable):
        super().__init__(executable)
        self.uc.mem_map(AI, 0x1000)
        self.observed = set()
        self.gearbox_gates = set()
        self.ranges = ((0x2DCBC, 0x2DD68), (0x2DD99, 0x2DDBA),
                       (0x2DDEF, 0x2DE18), (0x1C630, 0x1C777),
                       (0x4D5F4, 0x4D64F))
        self.allowed = [(STACK, STACK + 0x10000), (0x1FEFF0, 0x1FEFF4),
                        (0x1FED68, 0x1FED6C), (0x1FED8C, 0x1FED90),
                        (0x1FB4B0, 0x1FB4B8), (AI + 0x60, AI + 0x64),
                        (AI + 0x78, AI + 0x7C), (0x1DADE0, 0x1DAE60),
                        (0x1FECEC, 0x1FECF4)]
        self.active = "AI schedule"
        for address, expected in ((0xA8D96, 170), (0xA7B88, 1.4),
                                  (0xA7B90, 6.944444444444445),
                                  (0xA7B98, 43.2), (0xA7BA0, 151.2)):
            if struct.unpack("<d", self.uc.mem_read(address, 8))[0] != expected:
                raise ValueError(f"Unexpected original constant at {address:x}")

    def code(self, uc, address, size, _):
        if address in self.boundaries:
            uc.emu_stop()
            return
        if not any(lo <= address <= hi for lo, hi in self.ranges):
            raise ValueError(f"Unexpected schedule instruction at 0x{address:x}")
        self.observed.add(address)

    def execute(self, entry, boundaries, registers=None, stack_delta=0):
        for reg in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + reg), 0)
        for reg, value in (registers or {}).items():
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + reg), value)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, self.fpcw)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.boundaries = set(boundaries)
        self.uc.emu_start(entry, max(boundaries) + 1, timeout=1_000_000, count=1000)
        end = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
        if (end not in self.boundaries or
                self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + stack_delta or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != self.fpcw):
            raise ValueError(f"AI boundary/stack/x87 contract failed at 0x{end:x}")
        key = f"0x{entry:x}->0x{end:x}"
        self.fragment_executions[key] = self.fragment_executions.get(key, 0) + 1
        return end

    def schedule(self, case):
        n, phase, selected, index, computer, clock, wrecked, lock, out = case
        self.put(0x1FEFD0, "I", INFO)
        self.put(0x1FEFE0, "I", CAR)
        self.put(INFO + 4, "i", computer)
        self.put(INFO + 12, "i", 1)
        self.put(CAR + 0x27C, "i", 1)
        self.put(CAR + 0x604, "i", lock)
        self.put(CAR + 0x528, "i", out)
        self.put(ESP + 0x80, "i", index)
        self.put(0x1FED68, "i", phase)
        self.put(0x1FED8C, "i", selected)
        self.put(0x1FEFE4, "i", n)
        self.put(0x1FB4E8, "d", clock)
        # The earlier wreck eligibility gate is independently read from 0x2d98c.
        # Its unresolved fourth flag +0x350 is outside this comparison.
        action = 0
        if not wrecked:
            end = self.execute(0x2DCBC, (0x2DD1B, 0x2DD20, 0x2DD6D),
                               dict(EAX=CAR, ECX=phase))
            self.gearbox_gates.add(end)
            if end != 0x2DD6D:
                # Stop BEFORE the real gearbox call; its callee has its own oracle.
                end = self.execute(0x2DD20, (0x2DD56, 0x2DD68))
                action = 2 if end == 0x2DD56 else 1
        self.execute(0x2DD99, (0x2DDC0,))
        self.put(0x1FB4B0, "d", 0)
        self.execute(0x2DDEF, (0x2DE19,))
        return [action, self.get(0x1FED68), self.get(0x1FED8C)]

    def stuck(self, case):
        track, n, stuck, error, speed, airborne, section, side = case
        self.put(0xEB7C0, "I", AI)
        self.put(0x1FEFE0, "I", CAR)
        self.put(0x1FEFE8, "I", BODY)
        self.put(0x1FEFE4, "i", n)
        self.put(0x1FEF10, "i", track)
        self.put(AI + 0x60, "i", stuck)
        self.put(AI + 0x58, "d", side)
        self.put(BODY + 0x98, "d", speed)
        self.put(CAR + 0x270, "i", airborne)
        self.put(ESP + 0x18, "d", error)
        self.put(ESP + 0x13C, "i", section)
        self.put(ESP + 0x17C, "B", 0)
        self.execute(0x1C630, (0x1C77E,), dict(EAX=AI, EDI=abs(section)))
        return [self.get(AI + 0x60)]

    def retained_inputs(self):
        values = [(i * 11, 100 - i, i * 29 - 100, i % 2) for i in range(8)]
        for i, args in enumerate(values):
            self.put(0x1FEFF0, "i", i)
            self.execute(0x4D5F4, (0x4D655,),
                         dict(EAX=args[0], EDX=args[1], EBX=args[2], ECX=args[3]), -20)
        for repeat in range(3):
            for i in reversed(range(8)):
                self.put(0x1FEFF0, "i", i)
                self.execute(0x4D5F4, (0x4D655,), dict(EAX=0x1F02), -20)
                regs = tuple(self.uc.reg_read(getattr(self.reg, "UC_X86_REG_" + r))
                             for r in ("EDI", "ESI", "EDX", "EBP"))
                expected = tuple(v & 0xFFFFFFFF for v in values[i])
                stored = struct.unpack("<4i", self.uc.mem_read(0x1DADE0 + i * 16, 16))
                if regs != expected or stored != values[i]:
                    raise ValueError(f"Original input cache lost/crossed car {i}, repeat {repeat}")
        return len(values) * 4


def verify(executable, runner):
    original = ScheduleOriginal(executable)
    cases = []
    clocks = (-1, math.nextafter(0, -math.inf), 0, math.nextafter(170, -math.inf),
              170, math.nextafter(170, math.inf), 200)
    for n in range(1, 9):
        for phase, selected, index, clock in itertools.product((0, 1), range(n), range(n), clocks):
            cases.append(("S", (n, phase, selected, index, 1, clock, 0, 0, 0)))
        for phase, selected, index, computer, wreck, lock, out in itertools.product(
                (0, 1), range(n), range(n), (0, 1), (0, 1), (0, 1, 2), (0, 1)):
            cases.append(("S", (n, phase, selected, index, computer, -1, wreck, lock, out)))
    for track, n, stuck, section, side in itertools.product(
            range(7), (1, 8), (0, 43, 44, 97, 144, 151),
            (2, 61, 62, 64, 65, 84, 85, 94, 95, 145, 146, 148, 149, -62, -85, -146),
            (-0.5, 0)):
        cases.append(("K", (track, n, stuck, 0, 20, 0, section, side)))
    for n, stuck, error, speed, air in itertools.product(
            range(1, 9), (0, 43, 44, 97, 144, 151),
            (0, math.nextafter(1.4, -math.inf), 1.4, math.nextafter(1.4, math.inf),
             -1.4, -math.nextafter(1.4, math.inf)),
            (-20, math.nextafter(6.944444444444445, -math.inf), 6.944444444444445, 20), (0, 1)):
        cases.append(("K", (1, n, stuck, error, speed, air, 85, -0.5)))
    text = "".join(mode + " " + " ".join(map(str, case)) + "\n" for mode, case in cases)
    result = subprocess.run([str(runner), "--oracle"], input=text, text=True,
                            capture_output=True, check=True, timeout=60)
    rows = result.stdout.splitlines()
    if len(rows) != len(cases):
        raise ValueError("AI oracle row count mismatch")
    failures = []
    for i, ((mode, case), row) in enumerate(zip(cases, rows)):
        expected = original.schedule(case) if mode == "S" else original.stuck(case)
        actual = list(map(int, row.split()))
        if actual != expected:
            failures.append(dict(index=i, mode=mode, inputs=case, original=expected, port=actual))
    cache_rows = original.retained_inputs()
    required = {0x2DCCF, 0x2DCD8, 0x2DD1B, 0x2DD56, 0x2DD68,
                0x2DDB0, 0x2DDBA, 0x2DE0A, 0x1C684, 0x1C6B1,
                0x1C6D2, 0x1C6EC, 0x1C705, 0x1C72B, 0x1C746, 0x1C777}
    # Call boundaries are deliberately stopped, not executed.
    seen = original.observed | {int(k.split("->")[1], 16) for k in original.fragment_executions}
    if not required <= seen:
        raise ValueError(f"Missing original AI branches: {sorted(required - seen)}")
    sources = ("src/game/ai.c", "src/game/race.c", "src/game/car.c", "src/platform/main.c",
               "tests/ai_schedule.c", "tests/ai_schedule_original.py")
    import unicorn
    return dict(schema=1, timestamp=datetime.now(timezone.utc).isoformat(), status="pass" if not failures else "fail",
                original_sha256=ORIGINAL_SHA256, unicorn=unicorn.__version__,
                runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources},
                cases=len(cases), retained_input_fragment_checks=cache_rows,
                fragments=original.fragment_executions, failures=failures[:30],
                failure_count=len(failures),
                scope="Bounded scheduler gates/rotation, stuck arithmetic, original per-car input cache only.",
                limitations=["No complete original race replay, gearbox/AI callees stopped before calls.",
                             "The native wreck aggregate is supplied by the fixture; +0x350 is unmodelled.",
                             "Original retained-input registers checked separately from native integration tests.",
                             "Road error supplied, not proof of reconstructed TRI geometry."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.original, args.runner)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"AI original: {report['cases']} cases; {report['failure_count']} failures")
    return bool(report["failure_count"])


if __name__ == "__main__":
    raise SystemExit(main())
