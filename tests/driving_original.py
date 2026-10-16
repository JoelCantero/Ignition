#!/usr/bin/env python3
"""Bounded replay of driving helpers and the full original ghost sample writer."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess

from physics_original import Original, BODY, ESP, RETURN, STACK, FPCW, ORIGINAL_SHA256
from car_original import CAR, INFO

GHOST = 0x700000
RAD = math.pi / 1800.0


class DrivingOriginal(Original):
    def __init__(self, path):
        super().__init__(path)
        self.uc.mem_map(CAR, 0x5000)
        self.uc.mem_map(INFO, 0x1000)
        self.uc.mem_map(GHOST, 0x4A000)
        self.active = None
        self.boundary = None
        self.visits = {}

    def code(self, uc, address, size, _):
        if address == RETURN and size == 6:
            return
        if address == self.boundary:
            uc.emu_stop()
            return
        ranges = {
            "S": [(0x1FEB0, 0x1FECF), (0x350CC, 0x350EB),
                  (0x51760, 0x517F6), (0x517FC, 0x51816)],
            "W": [(0x51980, 0x519C5)],
            "G": [(0x2E660, 0x2E6A1)],
            "M": [(0x2E6A4, 0x2E6F5)],
            "L": [(0x2E6F8, 0x2EA41)],
            "B": [(0x40EED, 0x41014), (0x65782, 0x658FF)],
            "R": [(0x51204, 0x5146B), (0x5192E, 0x5197C), (0x55F9C, 0x55FB8)],
            "C": [(0x1DCFC, 0x1DEC8)],
        }
        if not any(lo <= address <= hi for lo, hi in ranges[self.active]):
            raise ValueError(f"Unexpected {self.active} instruction {address:#x}")
        self.visits.setdefault(self.active, set()).add(address)

    def write(self, uc, access, address, size, value, _):
        ranges = [(STACK, STACK + 0x10000), (CAR, CAR + 0x484C), (BODY, BODY + 0x1D0),
                  (GHOST, GHOST + 0x4A000), (0x1FEE64, 0x1FEE68), (0x1FEE00, 0x1FEE04),
                  (0x1FEFE0, 0x1FEFE4), (0xEB460, 0xEB7C0)]
        if not any(lo <= address and address + size <= hi for lo, hi in ranges):
            raise ValueError(f"Unexpected {self.active} write {address:#x}")

    def put(self, address, fmt, *values):
        self.uc.mem_write(address, struct.pack("<" + fmt, *values))

    def get(self, address, fmt):
        return struct.unpack("<" + fmt, self.uc.mem_read(address, struct.calcsize("<" + fmt)))

    def execute(self, address, arguments=b"", eax=0, boundary=None):
        self.boundary = boundary
        self.put(ESP, "I", RETURN)
        self.uc.mem_write(ESP + 4, arguments)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_EAX, eax)
        self.uc.reg_write(self.reg.UC_X86_REG_EBX, 0)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, FPCW)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.uc.emu_start(address, RETURN, timeout=1_000_000, count=100000)
        stop = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
        if stop != (boundary if boundary else RETURN):
            raise ValueError(f"Original did not return: {stop:#x}")

    def run(self, mode, v):
        self.active = mode
        self.uc.mem_write(CAR, bytes(0x484C))
        self.uc.mem_write(BODY, bytes(0x1D0))
        self.uc.mem_write(INFO, bytes(76))
        for ptr, value in [(0x1FEFE0, CAR), (0x1FEFE8, BODY), (0x1FEFD0, INFO)]:
            self.put(ptr, "I", value)
        if mode == "S":
            value, integer = v
            self.execute(0x1FEB0, struct.pack("<d", value))
            sign, = struct.unpack("<i", struct.pack("<I", self.uc.reg_read(self.reg.UC_X86_REG_EAX)))
            self.execute(0x350CC, struct.pack("<f", value))
            float_sign, = struct.unpack("<i", struct.pack("<I", self.uc.reg_read(self.reg.UC_X86_REG_EAX)))
            if float_sign != sign:
                raise ValueError("Unexpected float sign mismatch")
            self.execute(0x51760, struct.pack("<d", value))
            # Store st0 with an actual x87 instruction, rather than Unicorn's extended register layout.
            self.uc.mem_write(RETURN, b"\xdd\x1d" + struct.pack("<I", STACK))
            self.uc.ctl_remove_cache(RETURN, RETURN + 6)
            self.uc.emu_start(RETURN, RETURN + 6, count=10)
            control, = self.get(STACK, "d")
            self.execute(0x517FC, eax=integer & 0xFFFFFFFF)
            integer_sign, = struct.unpack("<i", struct.pack("<I", self.uc.reg_read(self.reg.UC_X86_REG_EAX)))
            return [sign, control, integer_sign]
        if mode == "W":
            self.execute(0x51980, struct.pack("<2d", *v))
            self.uc.mem_write(RETURN, b"\xdd\x1d" + struct.pack("<I", STACK))
            self.uc.ctl_remove_cache(RETURN, RETURN + 6)
            self.uc.emu_start(RETURN, RETURN + 6, count=10)
            return list(self.get(STACK, "d"))
        if mode == "G":
            ground, height, airborne, landed = v
            self.put(CAR + 0x120, "d", ground)
            self.put(CAR + 8, "d", height)
            self.put(CAR + 0x270, "i", airborne)
            self.put(CAR + 0x278, "i", landed)
            self.execute(0x2E660)
            return [self.get(CAR + offset, "i")[0] for offset in (0x270, 0x278)]
        if mode == "M":
            px, pz, vx, vz, paused = v
            self.put(BODY + 0x68, "4d", px, pz, vx, vz)
            self.put(CAR + 0x4848, "i", paused)
            self.execute(0x2E6A4)
            return list(self.get(BODY + 0x68, "2d"))
        if mode == "L":
            dx, dz, oldx, oldz, heading, roll, pitch = v
            for offset, value in [(0x18, dx), (0x28, dz), (0x30, oldx), (0x40, oldz),
                                  (0xF8, heading), (0xB8, roll), (0xC0, pitch),
                                  (0xC8, 0.97), (0xD0, 0.97), (0xD8, 35), (0xE0, 20)]:
                self.put(CAR + offset, "d", value)
            self.execute(0x2E6F8)
            return list(self.get(CAR + 0xB8, "2d"))
        if mode == "B":
            roll, pitch, lean_roll, lean_pitch, old_roll, old_pitch, launch = v
            for offset, value in [(0x108, roll), (0x110, pitch), (0xB8, lean_roll), (0xC0, lean_pitch),
                                  (0x584, old_roll / RAD), (0x58C, old_pitch / RAD)]:
                self.put(CAR + offset, "d", value)
            self.put(CAR + 0x554, "i", launch)
            self.execute(0x40EED, boundary=0x4101A)
            return [a * RAD for a in self.get(CAR + 0x584, "2d")]
        if mode == "R":
            x, y, z, roll, heading, pitch, wreck, section, lap, phase, sections, count = v
            for offset, value in [(0, x + 25600), (8, y), (0x10, z + 25600),
                                  (0x108, roll), (0xF8, heading), (0x110, pitch)]:
                self.put(CAR + offset, "d", value)
            for offset, value in [(0x354, int(wreck == 1)), (0x364, section), (0x374, lap), (0x378, phase)]:
                self.put(CAR + offset, "i", value)
            self.put(0x1FEE50, "i", 0)
            self.put(0x1FB4D8, "d", 0)
            self.put(0x1FEF78, "I", GHOST)
            self.put(0x1FEE64, "i", count)
            self.put(0x1FEE00, "i", 0)
            self.put(0x1FF004, "i", sections)
            self.execute(0x51204)
            return list(self.get(GHOST + count * 14, "7h")) + [
                self.get(0x1FEE64, "i")[0], self.get(0x1FEE00, "i")[0]]
        if mode == "C":
            nv, nf, px, py, pz = v[:5]
            self.put(GHOST, "2i", nv, nf)
            self.put(GHOST + 8, f"{nv * 3}i", *v[5:5 + nv * 3])
            self.put(INFO, "3i", px, py, pz)
            for f in range(nf):
                surface, a, b, c = v[5 + nv * 3 + f * 4:9 + nv * 3 + f * 4]
                self.put(GHOST + 0x2000 + f * 44, "4I", surface << 16, a, b, c)
            for ptr, value in [(0xEB790, GHOST), (0xEB78C, GHOST + 8),
                               (0xEB7B8, INFO), (0xEB7BC, GHOST + 0x2000)]:
                self.put(ptr, "I", value)
            self.execute(0x1DCFC)
            faces, = self.get(0xEB7A0, "i")
            vertices, = self.get(0xEB79C, "i")
            return [self.get(0xEB7AC, "i")[0], self.get(0xEB7A8, "i")[0], faces, vertices] + list(
                self.get(0xEB528, f"{faces}i")) + list(self.get(0xEB460, f"{vertices}i"))
        raise ValueError(mode)


def fixtures():
    cases = []
    # Unicorn's SAHF unordered path is inconsistent with the x87 status word for NaNs.
    for value in [-math.inf, -1e30, -1.0, -0.0, 0.0, 1.0, 1e30, math.inf]:
        for integer in [-2147483648, -1, 0, 1, 2147483647]:
            cases.append(("S", [value, integer]))
    for period in [1.0, 6.2831853071796, 3600.0]:
        for value in [-100 * period, -period, -1e-30,
                      -0.0, 0.0, period, math.nextafter(period, -math.inf), 100 * period]:
            cases.append(("W", [value, period]))
    for height in [14, math.nextafter(15, -math.inf), 15, math.nextafter(15, math.inf), 20]:
        for airborne in [0, 1, 2]:
            for landed in [0, 1]:
                cases.append(("G", [10, height, airborne, landed]))
    rng = random.Random(0x2E6F8)
    for i in range(200):
        cases.append(("M", [rng.uniform(-1000, 1000) for _ in range(4)] + [i % 3]))
        cases.append(("L", [rng.uniform(-5, 5) for _ in range(4)] +
                      [rng.uniform(0, 6.2831853071796), rng.uniform(-50, 50), rng.uniform(-50, 50)]))
        cases.append(("B", [rng.uniform(-7, 7), rng.uniform(-7, 7), rng.uniform(-35, 35),
                            rng.uniform(-20, 20), rng.uniform(-7, 7), rng.uniform(-7, 7), i % 2]))
        cases.append(("R", [rng.randint(-20000, 20000), rng.randint(-1000, 1000), rng.randint(-20000, 20000),
                            rng.uniform(-7, 7), rng.uniform(-14, 14), rng.uniform(-7, 7),
                            i % 4, rng.randint(-200, 200), i % 5, i % 4, 200,
                            21599 if i % 3 == 0 else 0]))
    for i in range(120):
        nv = [0, 3, 20, 60, 100][i % 5]
        nf = [0, 1, 50, 51, 100][i // 5 % 5] if nv else 0
        position = [rng.randint(-2000000000, 2000000000) for _ in range(3)]
        vertices = [rng.randint(-100000, 100000) for _ in range(nv * 3)]
        faces = []
        for f in range(nf):
            surface = [0, 4, 5, 6, 7, 8, 15, 17, 20, 39, 40, 65535][(i + f) % 12]
            faces.extend([surface, rng.randrange(nv), rng.randrange(nv), rng.randrange(nv)])
        cases.append(("C", [nv, nf] + position + vertices + faces))
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    cases = fixtures()
    original = DrivingOriginal(args.original)
    text = "".join(mode + " " + " ".join(map(str, values)) + "\n" for mode, values in cases)
    result = subprocess.run([str(args.runner.resolve())], input=text, text=True, capture_output=True, check=True)
    lines = result.stdout.splitlines()
    if len(lines) != len(cases):
        raise ValueError("Native output count mismatch")
    failures = []
    for index, ((mode, values), line) in enumerate(zip(cases, lines)):
        expected = original.run(mode, values)
        actual = list(map(float, line.split()))
        tolerance = 5e-13 if mode in ("L", "B") else 0
        if len(actual) != len(expected) or any(
                not math.isclose(a, b, abs_tol=tolerance, rel_tol=tolerance) for a, b in zip(actual, expected)):
            failures.append({"index": index, "mode": mode, "input": list(map(str, values)),
                             "expected": expected, "actual": actual})
    report = {
        "original_sha256": ORIGINAL_SHA256, "cases": len(cases), "failures": failures,
        "coverage": {mode: len(addresses) for mode, addresses in sorted(original.visits.items())},
        "scope": "Full sign/wrap/legacy ground/planar helpers, 1dcfc and 51204; lean model!=7; sway arithmetic only; NaNs excluded from emulator comparison",
        "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "runner_sha256": hashlib.sha256(args.runner.read_bytes()).hexdigest(),
    }
    args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(f"driving original: {len(cases)} cases, {len(failures)} failures")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
