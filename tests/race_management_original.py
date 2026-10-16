#!/usr/bin/env python3
"""LE differential oracle: race configuration/reset/ranking/elimination management.

Only OS I/O, RNG input, allocation, string formatting and elimination presentation
boundaries are substituted; the compared management decisions execute original bytes.
"""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import random
import struct
import subprocess

from physics_original import Original, ESP, STACK, RETURN, ORIGINAL_SHA256

CAR, INFO, GHOST = 0x700000, 0x730000, 0x740000
STRIDE, BTZ = 0x484C, 0x1FF158


class RaceOriginal(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.uc.mem_map(CAR, 0x30000)
        self.uc.mem_map(INFO, 0x1000)
        self.uc.mem_map(GHOST, 0x1000)
        # A controlled RNG boundary returns the fixture's binary64 variate in ST0.
        self.uc.mem_write(RETURN + 0x100, b"\xdd\x05" + struct.pack("<I", RETURN + 0x200) + b"\xc3")
        self.visits = set()
        self.substitutions = Counter()

    def put(self, address, fmt, *values):
        self.uc.mem_write(address, struct.pack("<" + fmt, *values))

    def get(self, address, fmt="i"):
        return struct.unpack("<" + fmt, self.uc.mem_read(address, struct.calcsize("<" + fmt)))[0]

    def ret(self):
        sp = self.uc.reg_read(self.reg.UC_X86_REG_ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_EIP, self.get(sp, "I"))
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, sp + 4)

    def code(self, uc, address, size, _):
        if self.active == "C":
            if address == 0x22947:
                self.substitutions["BTZ filesystem read"] += 1
                uc.reg_write(self.reg.UC_X86_REG_EIP, 0x229B9)
                return
            if address == 0x22F38:
                self.substitutions["track name/path formatting"] += 1
                self.ret()
                return
            if address == 0x62775:
                sp = uc.reg_read(self.reg.UC_X86_REG_ESP)
                destination, source = self.get(sp + 4, "I"), self.get(sp + 12, "I")
                raw = bytearray()
                for i in range(15):
                    value = uc.mem_read(source + i, 1)[0]
                    if not value:
                        break
                    raw.append(value)
                uc.mem_write(destination, bytes(raw) + b"\0")
                self.substitutions["participant %s formatting"] += 1
                self.ret()
                return
            if address == 0x51714:
                self.substitutions["controlled random01 input"] += 1
                uc.reg_write(self.reg.UC_X86_REG_EIP, RETURN + 0x100)
                return
        if self.active == "E":
            if address == 0x2DEEC:
                self.substitutions["elimination log/effect geometry"] += 1
                uc.reg_write(self.reg.UC_X86_REG_EBX, 0)
                uc.reg_write(self.reg.UC_X86_REG_EIP, 0x2DF1D)
                return
            if address == 0x2DF28:
                self.substitutions["elimination FX dispatch"] += 1
                uc.reg_write(self.reg.UC_X86_REG_EIP, 0x2DF91)
                return
            if address == 0x2DFAB:
                self.substitutions["elimination audio dispatch"] += 1
                uc.reg_write(self.reg.UC_X86_REG_EIP, 0x2DFDA)
                return
            if address == 0x2E054:
                self.substitutions["elimination localized HUD message"] += 1
                uc.reg_write(self.reg.UC_X86_REG_EIP, 0x2E14B)
                return
        if self.active == "Q" and address == 0x6509C:
            self.substitutions["time-trial buffer allocation"] += 1
            uc.reg_write(self.reg.UC_X86_REG_EAX, GHOST)
            self.ret()
            return
        ranges = {"R": [(0x4C2D8, 0x4C734)], "E": [(0x2DE30, 0x2E232)],
                  "Q": [(0x2A97C, 0x2ABDB)], "T": [(0x2A410, 0x2A4EA)],
                  "C": [(0x2293C, 0x22D07), (0x55F9C, 0x55FB8),
                        (RETURN + 0x100, RETURN + 0x106)]}
        if not any(lo <= address <= hi for lo, hi in ranges[self.active]):
            raise ValueError(f"Unexpected {self.active} instruction 0x{address:x}")
        self.visits.add(address)

    def write(self, uc, access, address, size, value, _):
        if not any(lo <= address and address + size <= hi for lo, hi in self.allowed):
            raise ValueError(f"Unexpected {self.active} write 0x{address:x}+{size}")

    def execute(self, entry):
        for name in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + name), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, self.fpcw)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.put(ESP, "I", RETURN)
        self.uc.emu_start(entry, RETURN, timeout=1_000_000, count=30000)
        if (self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN or
                self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4 or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != self.fpcw):
            raise ValueError(f"{self.active} return/stack/x87 contract failed")

    def start(self, active, n):
        self.active = active
        self.uc.mem_write(CAR, bytes(STRIDE * 8))
        self.uc.mem_write(INFO, bytes(0x4C * 8))
        self.put(0x1FEFE0, "I", CAR)
        self.put(0x1FEFD0, "I", INFO)
        self.put(0x1FEFE4, "i", n)
        self.put(0x1FEF78, "I", GHOST)
        self.put(0x1FEE64, "i", 0)
        self.allowed = [(STACK, STACK + 0x10000)]

    def rank_or_eliminate(self, case):
        command, header, cars = case
        n, mode, count, total, players, ghost, progress, remaining, finished, clock, camera, hold = header
        self.start(command, n)
        for address, value in ((0x1FEEFC, mode), (0x1FF004, count), (0x1FEF14, players),
                               (0x1FEE4C, ghost), (0x1FED78, remaining), (0x1FEE60, finished),
                               (0x1FEE80, camera), (0xC0B38, hold), (0x1FEE10, 7)):
            self.put(address, "i", value)
        self.put(0x1FB4D8, "d", total)
        self.put(0x1FB4D0, "d", clock)
        self.put(0x1FED9C, "2i", -1, -1)
        self.put(GHOST + 0xA, "I", (progress & 0xFFFF) << 16)
        for i, values in enumerate(cars):
            lap, section, phase, out, finish, position, bias, elapsed, blow, wreck, grid = values
            base = CAR + i * STRIDE
            for off, value in ((0x374, lap), (0x364, section), (0x378, phase), (0x528, out),
                               (0x3A0, finish), (0x39C, position), (0x608, bias), (0x354, wreck)):
                self.put(base + off, "i", value)
            self.put(base + 0x3A4, "d", elapsed)
            self.put(base + 0x60C, "d", blow)
            self.put(0xC33D0 + i * 4, "i", grid)
            if command == "R":
                self.allowed += [(base + 0x39C, base + 0x3A4)]
            else:
                self.allowed += [(base + 0x354, base + 0x358), (base + 0x3A0, base + 0x3A4),
                                 (base + 0x528, base + 0x52C), (base + 0x60C, base + 0x614)]
        self.allowed += [(0x1FEFE0, 0x1FEFE8)]
        if command == "R":
            self.allowed += [(0x1FEDDC, 0x1FEDE0), (0x1FEE10, 0x1FEE14)]
            self.execute(0x4C2D8)
            return [self.get(0x1FEE10)] + [self.get(CAR + i * STRIDE + off)
                    for i in range(n) for off in (0x39C, 0x3A0)]
        self.allowed += [(0x1FB4D0, 0x1FB4D8), (0x1FED9C, 0x1FEDBC),
                         (0x1FEE60, 0x1FEE64), (0x1FED78, 0x1FED7C),
                         (0x1FEE80, 0x1FEE84), (0xC0B38, 0xC0B3C)]
        self.execute(0x2DE30)
        result = [self.get(0x1FED78), self.get(0x1FEE60), self.get(0x1FB4D0, "d"),
                  self.get(0x1FEE80), self.get(0xC0B38), self.get(0x1FED9C), self.get(0x1FEDA0)]
        for i in range(n):
            base = CAR + i * STRIDE
            result += [self.get(base + 0x528), self.get(base + 0x3A0),
                       self.get(base + 0x60C, "d"), self.get(base + 0x354)]
        return result

    def config(self, raw, cd, random01):
        n = struct.unpack_from("<i", raw, 0x153)[0]
        self.start("C", n)
        self.uc.mem_write(BTZ, raw)
        self.put(0x25A934, "i", cd)
        self.put(RETURN + 0x200, "d", random01)
        # Inputs are fixed; only known configuration destinations and participant records may change.
        globals_ = [0x1FEEFC, 0x1FEF10, 0x1FEFE4, 0x1FEFCC, 0x1FEFDC, 0x1FEF08, 0x1FEF14,
                    0x1FEBE0, 0x1FEE4C, 0x1FEE48, 0x1FEE84, 0x1FED80, 0x1FED70, 0x1FEDD8,
                    0x1FED64, 0x1FEED8, 0x1FEE94, 0x1FEEC0, 0x1FEEC4, 0x1FEEF4, 0x1FEE28, 0x1FEEA0]
        self.allowed += [(a, a + 4) for a in globals_]
        self.allowed += [(INFO, INFO + 0x4C * n), (0xC33D0, 0xC33D0 + n * 4)]
        self.execute(0x2293C)
        get = self.get
        result = [get(0x1FEEFC), get(0x1FEF10), get(BTZ + 0xF3), get(0x1FEDD8), n,
                  get(0x1FEF14), get(0x1FEEA0), get(0x1FEE4C), get(0x1FEED8), get(0x1FEEF4),
                  get(0x1FEE94), get(0x1FEE28)]
        result += [get(a) for a in (0x1FEFDC, 0x1FEFCC, 0x1FEBE0, 0x1FEE48,
                                    0x1FEE84, 0x1FED80, 0x1FED70, 0x1FED64)]
        for i in range(n):
            base = INFO + i * 0x4C
            result += [get(0xC33D0 + i * 4)] + [get(base + off) for off in (0, 12, 4, 20, 8, 24)]
            result += list(self.uc.mem_read(base + 0x1C, 8)) + list(self.uc.mem_read(base + 0x3C, 15))
        return result

    def reset(self, mode, n, round_):
        self.start("Q", n)
        self.put(0x1FEEFC, "i", mode)
        self.put(BTZ + 0xF3, "i", round_)
        self.put(0x1FEF10, "i", 2)
        for a, value in ((0x1FB390, 15), (0x1FB540, 12), (0x1FB4E8, 99)):
            self.put(a, "d", value)
        self.put(0x1FEF04, "i", 123)
        self.put(0x1AEBB0, "8i", *range(10, 18))
        self.put(0x1FB490, "8i", *([77] * 8))
        self.put(0x1AC458, "8i", *([77] * 8))
        destinations = [0x1FEE60, 0x1FEE30, 0x1FEE34, 0x1FEE50, 0x1FEE58, 0x1FEDFC,
                        0x1FEDCC, 0xC0B34, 0xC0B20, 0x1FED90, 0x1FED94, 0x1FED34,
                        0x1FED68, 0x1FED6C, 0x1FED3C, 0x1FED4C, 0x1FED58, 0x1FED44,
                        0x1FED60, 0x162DA0, 0x162DA4, 0x1FED30, 0x1FED9C, 0x1FEDA0,
                        0x1FEE54, 0x1FEDE4, 0x1FEDDC, 0x1FED38, 0x1FED54, 0x1FED8C,
                        0x1FED78, 0x1FEE00, 0x1FEE64, 0xC0B10, 0x1FEF78, 0x1FEDE0, 0x1FED7C,
                        0xD7D0C, 0xD7D50]
        self.allowed += [(a, a + 4) for a in destinations]
        self.allowed += [(0x1FB370, 0x1FB380), (0x1FB488, 0x1FB4E0),
                         (0x1AC458, 0x1AC478), (0x1AEBB0, 0x1AEBD0), (0x1FB398, 0x1FB3E8)]
        self.execute(0x2A97C)
        result = [self.get(0x1FEE60), self.get(0x1FED78), self.get(0x1FB4D0, "d"),
                  self.get(0x1FB4D8, "d"), self.get(0x1FED68), self.get(0x1FED8C),
                  self.get(0x1FEDE0), self.get(0x1FED7C), self.get(0x1FB390, "d"),
                  self.get(0x1FB540, "d"), self.get(0x1FB4E8, "d"), self.get(0x1FEF04),
                  self.get(0x1FED9C), self.get(0x1FEDA0)]
        for i in range(8):
            result += [self.get(a + 4 * i) for a in (0x1AEBB0, 0x1FB490, 0x1AC458)]
        for i in range(10):
            result += [self.get(a + 4 * i) for a in (0x1FB398, 0x1FB3C0)]
        return result

    def times(self, n, now, saved):
        self.start("T", n)
        self.put(0x1FEF10, "i", 3)
        self.put(0xAB5DC, "i", now)
        self.put(0x1FF367 + 3 * 0x78, "d", saved)
        self.put(0x1FB390, "d", 15)
        self.put(0x1FB540, "d", 12)
        for i in range(n):
            base = CAR + i * STRIDE
            for off, value in ((0x3A4, 1), (0x3B4, 2), (0x3AC, 3), (0x3D4, 4),
                               (0x3BC, 5), (0x3C4, 6), (0x3CC, 7)):
                self.put(base + off, "d", value)
            self.allowed += [(base + off, base + off + 8) for off in (0x3A4, 0x3B4, 0x3BC, 0x3C4, 0x3CC)]
        self.allowed += [(a, a + size) for a, size in ((0x1FB390, 8), (0x1FEF04, 4),
                         (0x1FEF10, 4), (0x1FEFE0, 8))]
        self.execute(0x2A410)
        result = [self.get(0x1FB390, "d"), self.get(0x1FB540, "d"), self.get(0x1FEF04)]
        for i in range(n):
            result += [self.get(CAR + i * STRIDE + off, "d")
                       for off in (0x3A4, 0x3B4, 0x3AC, 0x3D4, 0x3BC, 0x3C4, 0x3CC)]
        return result


def fixtures():
    rng = random.Random(0x4C2D8)
    cases = []
    for index in range(1000):
        n = rng.randrange(1, 9)
        mode = index % 4
        total = (0, 249.999, 250, 251, math.nan)[index % 5]
        header = [n, mode, rng.choice([4, 20, 100, 191]), total, int(n > 1) * (index % 2),
                  index % 3, rng.randrange(-32768, 32768), n, 0, -1, 0, 0]
        cars = []
        for i in range(n):
            cars.append([rng.choice([0, 1, 2, 3, 2147483647, -2147483648]),
                         rng.choice([-2147483648, -100, -30, 0, 3, 10, 30, 100]),
                         rng.randrange(4), rng.randrange(3), rng.randrange(4), i + 1,
                         rng.choice([-2147483648, -30, 0, 0, 10, 2147483647]),
                         rng.choice([0, 12, 12, 30, math.nan]), 0, 0, n - i - 1])
        cases.append(("R", header, cars))
    # Elimination covers one/many/no trailers, existing outs, human identities and +/-0.
    for index in range(800):
        n = rng.randrange(2, 9)
        header = [n, 3, 100, 500, index % 2, 0, 0, n, 0,
                  rng.choice([-1, -0.0, 0, 20, math.nan]), rng.randrange(4), rng.randrange(100)]
        cars = [[rng.randrange(3), i * 10, 1, 0, 0, n - i, 0, 10, 0, 0, i] for i in range(n)]
        if index % 4 == 0:
            for car in cars:
                car[0] = 2
            cars[index % n][0] = 1
        elif index % 4 == 1:
            for car in cars:
                car[0] = 1
        for i, car in enumerate(cars):
            if index % 7 == 0 and i != index % n:
                car[3] = 1
                header[7] -= 1
                header[8] += 1
            car[8] = rng.choice([0.0, -0.0, 0.0, 0.0, 1, math.nan])
        cases.append(("E", header, cars))
    return cases


def run(original, runner):
    commands, expected, counts = [], [], Counter()
    for command, header, cars in fixtures():
        commands.append(command + " " + " ".join(map(str, header + sum(cars, []))))
        expected.append(original.rank_or_eliminate((command, header, cars)))
        counts[command] += 1
    rng = random.Random(0x2293C)
    for mode in range(4):
        for difficulty in range(4):
            for round_ in range(7):
                raw = bytearray(0x597)
                n = rng.randrange(1, 9)
                players = int(n > 1) * rng.randrange(2)
                values = {0x8: mode, 0xC: difficulty, 0xF3: round_, 0x153: n, 0x4: players,
                          0x10: rng.randrange(3), 0xF7: rng.choice([-1, 0, 0, 3]),
                          0xFB: rng.randrange(11), 0xFF: rng.randrange(11)}
                for off in (0x107, 0x10B, 0x197, 0x15B, 0x10F, 0x113, 0x117, 0x14):
                    values[off] = rng.randrange(100)
                for i in range(n):
                    values.update({0x1C + i * 4: i + 1, 0x48 + i * 4: i % 2,
                                   0x15F + i * 4: int(i > players), 0x133 + i * 4: n - i - 1})
                    name = f"driver-{i}".encode()
                    raw[0x6C + 15 * i:0x6C + 15 * i + len(name)] = name
                for i in range(2):
                    values[0x123 + 4 * i] = rng.randrange(4)
                    values[0x12B + 4 * i] = rng.randrange(2)
                    raw[0x17F + 8 * i:0x187 + 8 * i] = bytes(rng.randrange(256) for _ in range(8))
                for off, value in values.items():
                    struct.pack_into("<i", raw, off, value)
                cd, random01 = rng.choice([0, 1, 4, 8]), rng.choice([0, .125, .75, .999999])
                commands.append(f"C {cd} {random01} " + raw.hex())
                expected.append(original.config(bytes(raw), cd, random01))
                counts["C"] += 1
    for mode in range(4):
        for n in range(9):
            for round_ in (0, 1, 6):
                commands.append(f"Q {mode} {n} {round_}")
                expected.append(original.reset(mode, n, round_))
                counts["Q"] += 1
    for n in range(9):
        for now in (-2147483648, -1, 0, 2147483647):
            saved = rng.choice([0, 12.5, 1e30])
            commands.append(f"T {n} {now} {saved}")
            expected.append(original.times(n, now, saved))
            counts["T"] += 1
    result = subprocess.run([str(runner), "--oracle"], input="\n".join(commands) + "\n",
                            capture_output=True, text=True, check=True, timeout=60)
    rows = result.stdout.splitlines()
    if len(rows) != len(expected):
        raise ValueError("Race oracle output row count mismatch")
    failures = []
    for i, (row, want) in enumerate(zip(rows, expected)):
        got = list(map(float, row.split()))
        if len(got) != len(want):
            raise ValueError(f"Wrong field count for case {i}")
        for field, (actual, reference) in enumerate(zip(got, want)):
            if actual != reference and not (math.isnan(actual) and math.isnan(reference)):
                failures.append(dict(case=i, command=commands[i][0], field=field,
                                     port=actual, original=reference))
    required = {0x4C3A8, 0x4C3FC, 0x4C42E, 0x4C477, 0x4C617, 0x4C6B7, 0x4C706, 0x4C717,
                0x2E048, 0x2E18A, 0x2E216, 0x2AAC7, 0x2AB6D, 0x2A460, 0x22C32, 0x22CF0}
    missing = sorted(required - original.visits)
    return dict(status="fail" if failures or missing else "pass", original_sha256=ORIGINAL_SHA256,
                cases=len(commands), cases_by_operation=dict(counts), differences=failures[:30],
                difference_count=len(failures), missing_required_instructions=[hex(a) for a in missing],
                instruction_count=len(original.visits), substitutions=dict(original.substitutions),
                limitations=["RNG variates are explicit controlled inputs, not RNG-sequence verification.",
                             "BTZ filesystem I/O and track paths are tested by their separate subsystems.",
                             "Participant formatting is bounded %s; unsafe/unbounded original names are excluded.",
                             "Elimination FX geometry, audio and localized HUD delivery are not replayed.",
                             "Reset comparison covers race-owned fields; audio/weather/render/input scratch and allocation are not ported here.",
                             "Ranking car+0x608 is an explicit input; its producer remains outside this proof.",
                             "Valid BTZ index domain only; port rejects unsafe original configurations."])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    report = run(RaceOriginal(args.original), args.runner)
    args.report.write_text(json.dumps(report, indent=2, allow_nan=True) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "cases", "difference_count", "missing_required_instructions")}))
    return report["status"] != "pass"


if __name__ == "__main__":
    raise SystemExit(main())
