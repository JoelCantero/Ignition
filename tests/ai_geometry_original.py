#!/usr/bin/env python3
"""Compare both load_tri geometry fragments, including real x87 runtime calls."""
import argparse
from datetime import datetime, timezone
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import struct
import subprocess

from physics_original import Original, ORIGINAL_SHA256, ESP, STACK

CURRENT = 0x430000
NEXT = CURRENT + 0x100


class GeometryOriginal(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.uc.mem_map(CURRENT, 0x1000)
        self.boundary = 0
        self.observed = set()
        self.geometry_mode = True
        self.ranges = ((0x1D80A, 0x1DA22), (0x1DA75, 0x1DC8C),
                       (0x1BE03, 0x1BF59),
                       (0x647EF, 0x64803), (0x55F9C, 0x55FB8))
        if struct.unpack("<d", self.uc.mem_read(0xA7C18, 8))[0] != 18:
            raise ValueError("Unexpected original inset distance")

    def code(self, uc, address, size, _):
        if address == self.boundary:
            uc.emu_stop()
            return
        if not any(lo <= address <= hi for lo, hi in self.ranges):
            raise ValueError(f"Unexpected geometry instruction 0x{address:x}")
        self.observed.add(address)

    def write(self, uc, access, address, size, value, _):
        if not (STACK <= address and address + size <= STACK + 0x10000 or
                self.geometry_mode and
                CURRENT + 0x1A <= address and address + size <= CURRENT + 0x4A):
            raise ValueError(f"Unexpected geometry write 0x{address:x}")

    def geometry(self, points, secondary):
        self.geometry_mode = True
        initial = bytearray(b"\xa5" * 0x4B)
        struct.pack_into("<6i", initial, 2, *points[:6])
        following = bytearray(b"\x5a" * 0x4B)
        struct.pack_into("<6i", following, 2, *points[6:])
        self.uc.mem_write(CURRENT, bytes(initial))
        self.uc.mem_write(NEXT, bytes(following))
        for reg in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + reg), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_ECX, CURRENT)
        self.uc.reg_write(self.reg.UC_X86_REG_EBX, NEXT)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, 0x037F)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        entry, self.boundary = ((0x1DA75, 0x1DC8F) if secondary else (0x1D80A, 0x1DA25))
        self.uc.emu_start(entry, self.boundary + 1, timeout=1_000_000, count=2000)
        if (self.uc.reg_read(self.reg.UC_X86_REG_EIP) != self.boundary or
                self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != 0x037F):
            raise ValueError("Original geometry boundary/stack/x87 contract failed")
        raw = self.uc.mem_read(CURRENT, 0x4B)
        if raw[:0x1A] != initial[:0x1A] or raw[0x4A:] != initial[0x4A:]:
            raise ValueError("Original changed geometry input bytes")
        if self.uc.mem_read(NEXT, 0x4B) != following:
            raise ValueError("Original changed successor record")
        return list(struct.unpack_from("<8I", raw, 0x2A)) + list(struct.unpack_from("<4i", raw, 0x1A))

    def travel(self, heading, motion):
        self.geometry_mode = False
        self.uc.mem_write(0x1FEFE0, struct.pack("<I", CURRENT))
        self.uc.mem_write(CURRENT + 0xF8, struct.pack("<2d", heading, motion))
        self.uc.mem_write(ESP + 0x17C, b"\0")
        initial = bytes(self.uc.mem_read(CURRENT, 0x108))
        self.uc.reg_write(self.reg.UC_X86_REG_EDX, CURRENT)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, 0x037F)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.boundary = 0x1BF60
        self.uc.emu_start(0x1BE03, self.boundary + 1, timeout=1_000_000, count=200)
        if (self.uc.reg_read(self.reg.UC_X86_REG_EIP) != self.boundary or
                self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != 0x037F):
            raise ValueError("Original travel boundary/stack/x87 contract failed")
        if bytes(self.uc.mem_read(CURRENT, 0x108)) != initial:
            raise ValueError("Original travel changed car input")
        return struct.unpack("<d", self.uc.mem_read(ESP + 0xD0, 8))[0]


def fixtures():
    cases = []
    for dx, dy, dz in itertools.product((-101, -1, 0, 1, 101), repeat=3):
        for width in (-73, 0, 73):
            current = (25601, -11, 25599, 25601 + width, 14, 25602)
            following = tuple(value + (dx, dy, dz)[i % 3] + (i % 2)
                              for i, value in enumerate(current))
            cases.append(current + following)
    for x, z, width in itertools.product((-25600, 0, 25600), repeat=3):
        cases.append((x, 0, z, x + width, 0, z, x, 0, z, x + width, 0, z))
    rng = random.Random(0x1CE30)
    for _ in range(500):
        cases.append(tuple(rng.randrange(-100000, 100001) for _ in range(12)))
    return cases


def verify(original_path, runner, assets_runner, data):
    original = GeometryOriginal(original_path)
    cases = fixtures()
    result = subprocess.run([str(runner), "--geometry-oracle"], text=True,
                            input="".join(" ".join(map(str, c)) + "\n" for c in cases),
                            capture_output=True, check=True, timeout=30)
    rows = result.stdout.splitlines()
    if len(rows) != len(cases):
        raise ValueError("Geometry runner row count mismatch")
    failures = []
    actual_rows = [list(map(int, row.split())) for row in rows]
    synthetic_count = len(cases)
    real_result = subprocess.run([str(assets_runner), str(data), "--geometry-oracle"],
                                 text=True, capture_output=True, check=True, timeout=30)
    for row in real_result.stdout.splitlines():
        values = list(map(int, row.split()))
        if len(values) != 24:
            raise ValueError("Real geometry runner row shape mismatch")
        cases.append(tuple(values[:12]))
        actual_rows.append(values[12:])
    if len(cases) == synthetic_count:
        raise ValueError("Real geometry runner emitted no records")
    for i, (case, actual) in enumerate(zip(cases, actual_rows)):
        if len(actual) != 12:
            raise ValueError("Geometry output shape mismatch")
        for secondary in (False, True):
            expected = original.geometry(case, secondary)
            if actual != expected:
                failures.append(dict(index=i, secondary=secondary, points=case,
                                     original=expected, port=actual))
    headings = (-3.1415926535898, -math.pi, -0.78539816339745, 0,
                0.78539816339745, math.pi, 3.1415926535898, 6.2831853071796)
    angles = sorted({neighbor for value in headings for neighbor in
                     (math.nextafter(value, -math.inf), value, math.nextafter(value, math.inf))})
    travels = list(itertools.product(angles, repeat=2))
    for heading in (-math.pi, -1.5, 0, 1.5, math.pi, 2 * math.pi):
        for offset in (-6.2831853071796, -3.1415926535898, -0.78539816339745,
                       0.78539816339745, 3.1415926535898, 6.2831853071796):
            motion = heading + offset
            travels += [(heading, value) for value in
                        (math.nextafter(motion, -math.inf), motion,
                         math.nextafter(motion, math.inf))]
    rng = random.Random(0x1BE03)
    travels += [(rng.uniform(0, 2 * math.pi), rng.uniform(-math.pi, math.pi)) for _ in range(500)]
    result = subprocess.run([str(runner), "--travel-oracle"], text=True,
                            input="".join(f"{h:.17g} {m:.17g}\n" for h, m in travels),
                            capture_output=True, check=True, timeout=30)
    rows = result.stdout.splitlines()
    if len(rows) != len(travels):
        raise ValueError("Travel runner row count mismatch")
    max_error = 0
    for (heading, motion), row in zip(travels, rows):
        actual = float(row)
        expected = original.travel(heading, motion)
        error = abs(actual - expected)
        max_error = max(max_error, error)
        if not math.isfinite(actual) or error > 2e-15:
            failures.append(dict(heading=heading, motion=motion, original=expected, port=actual))
    code = sorted(original.observed)
    digest = hashlib.sha256()
    for address in code:
        digest.update(struct.pack("<I", address))
        digest.update(original.uc.mem_read(address, 1))
    return dict(schema=1, generated=datetime.now(timezone.utc).isoformat(),
                original_sha256=ORIGINAL_SHA256,
                runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                assets_runner_sha256=hashlib.sha256(assets_runner.read_bytes()).hexdigest(),
                inputs_sha256=hashlib.sha256(repr((cases, travels)).encode()).hexdigest(),
                sources_sha256={str(p.relative_to(Path(__file__).resolve().parents[1])):
                                hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in (Path(__file__).resolve(),
                                          Path(__file__).resolve().parents[1] / "src/game/track.c",
                                          Path(__file__).resolve().parents[1] / "src/game/ai.c")},
                scope="load_tri geometry, both slots, actual atan2/__CHP; ai_drive travel blend",
                cases=len(cases) * 2, synthetic_records=synthetic_count,
                real_records=len(cases) - synthetic_count,
                travel_cases=len(travels), travel_max_error=max_error,
                failures=len(failures), examples=failures[:12],
                observed_addresses=[hex(a) for a in code], observed_digest=digest.hexdigest())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--assets-runner", required=True, type=Path)
    parser.add_argument("--data", required=True, type=Path)
    args = parser.parse_args()
    report = verify(args.original, args.runner.resolve(), args.assets_runner.resolve(), args.data)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"AI geometry: {report['cases']} cases, {report['failures']} failures")
    return bool(report["failures"])


if __name__ == "__main__":
    raise SystemExit(main())
