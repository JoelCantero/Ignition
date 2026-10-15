#!/usr/bin/env python3
"""Compare ai_drive target clipping and curvature (0x1b4a2..0x1b823) with the original LE bytes."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess

from physics_original import Original, ORIGINAL_SHA256, ESP, STACK, FPCW

RECORDS = 0x430000
CAR = 0x431000
STATE = 0x432000
STRIDE = 0x80
ENTRY, BOUNDARY = 0x1B4A2, 0x1B823
SECTION_COUNT, CARS, AI_STATE = 0x1FF004, 0x1FEFE0, 0xEB7C0
SECTION = 10
COUNT = 200
OUTSIDE_GRIP = (-21, 79, 90, 97)


class TargetOriginal(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.uc.mem_map(RECORDS, 0x3000)
        self.boundary = 0
        self.observed = set()
        self.ranges = ((ENTRY, BOUNDARY - 1), (0x647EF, 0x64803))
        offsets = [struct.unpack("<h", self.uc.mem_read(0xBF994 + 2 * i, 2))[0] for i in range(6)]
        if offsets != [1, 2, 3, 4, 6, 8]:
            raise ValueError(f"Unexpected lookahead offsets {offsets}")

    def code(self, uc, address, size, _):
        if address == self.boundary:
            uc.emu_stop()
            return
        if not any(lo <= address <= hi for lo, hi in self.ranges):
            raise ValueError(f"Unexpected target instruction 0x{address:x}")
        self.observed.add(address)

    def write(self, uc, access, address, size, value, _):
        if not (STACK <= address and address + size <= STACK + 0x10000):
            raise ValueError(f"Unexpected target write 0x{address:x}")

    def run(self, case):
        s38, car_x, car_z, records = case
        guarded = {}
        for i, (point, inset, heading, surface) in enumerate(records):
            raw = bytearray(b"\xa5" * 0x4B)
            raw[1] = surface & 0xFF
            struct.pack_into("<6i", raw, 2, *point)
            struct.pack_into("<4i", raw, 0x1A, *inset)
            struct.pack_into("<I", raw, 0x2E, heading)
            self.uc.mem_write(RECORDS + i * STRIDE, bytes(raw))
            guarded[RECORDS + i * STRIDE] = bytes(raw)
        car = bytearray(b"\x5a" * 0x18)
        struct.pack_into("<d", car, 0, car_x)
        struct.pack_into("<d", car, 0x10, car_z)
        self.uc.mem_write(CAR, bytes(car))
        state = bytearray(b"\x3c" * 0x7C)
        struct.pack_into("<d", state, 0x38, s38)
        self.uc.mem_write(STATE, bytes(state))
        self.uc.mem_write(SECTION_COUNT, struct.pack("<i", COUNT))
        self.uc.mem_write(CARS, struct.pack("<I", CAR))
        self.uc.mem_write(AI_STATE, struct.pack("<I", STATE))
        stack = bytearray(b"\xc3" * 0x180)
        struct.pack_into("<6I", stack, 0, *(RECORDS + i * STRIDE for i in range(1, 7)))
        struct.pack_into("<I", stack, 0x158, RECORDS)
        stack[0x17C] = 0
        self.uc.mem_write(ESP, bytes(stack))
        for reg in ("EAX", "EBX", "EDX", "ESI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + reg), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_ECX, RECORDS + STRIDE)
        self.uc.reg_write(self.reg.UC_X86_REG_EDI, SECTION)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, FPCW)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.boundary = BOUNDARY
        self.uc.emu_start(ENTRY, BOUNDARY + 1, timeout=1_000_000, count=20000)
        if (self.uc.reg_read(self.reg.UC_X86_REG_EIP) != BOUNDARY or
                self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != FPCW):
            raise ValueError("Original target boundary/stack/x87 contract failed")
        for address, raw in guarded.items():
            if bytes(self.uc.mem_read(address, 0x4B)) != raw:
                raise ValueError("Original changed a route record")
        frame = bytes(self.uc.mem_read(ESP, 0x180))
        if frame[:0x18] != bytes(stack[:0x18]) or frame[0x158:0x15C] != bytes(stack[0x158:0x15C]):
            raise ValueError("Original changed the lookahead record pointers")
        index = self.uc.reg_read(self.reg.UC_X86_REG_ESI)
        offset = struct.unpack_from("<i", frame, 0x130)[0]
        ebp = self.uc.reg_read(self.reg.UC_X86_REG_EBP)
        if ebp != (SECTION + offset) % COUNT:
            raise ValueError("Original slope section index differs from s + offset")

        def d(at):
            return struct.unpack_from("<d", frame, at)[0]
        return [index, offset, struct.unpack_from("<i", frame, 0x144)[0],
                d(0xE8), d(0xE0), d(0xF0), d(0x40), d(0x80), d(0xA8)]


def f32_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def mid(a, b):
    return a + int((b - a) / 2)


def inset(point, angle_from, angle_to):
    # Records generated here only need integer inset coordinates; the exact load_tri inset arithmetic
    # is verified separately by tests/ai_geometry_original.py.
    angle = math.atan2(angle_to[1] - angle_from[1], angle_to[0] - angle_from[0])
    return (math.trunc(point[0] + 18 * math.cos(angle)), math.trunc(point[1] + 18 * math.sin(angle)))


def road(rng, start, heading, curvature, width, step, wobble=0):
    gates = []
    x, z = start
    for i in range(7):
        nx, nz = -math.sin(heading), math.cos(heading)
        w = width + rng.randint(-wobble, wobble)
        left = (round(x - nx * w / 2), round(z - nz * w / 2))
        right = (round(x + nx * w / 2), round(z + nz * w / 2))
        gates.append((left, right))
        x += math.cos(heading) * step
        z += math.sin(heading) * step
        heading += curvature
    return gates


def records_from(rng, gates, surfaces, heading_noise=0.0):
    centres = [(mid(l[0], r[0]), mid(l[1], r[1])) for l, r in gates]
    headings = [math.atan2(b[1] - a[1], b[0] - a[0]) for a, b in zip(centres, centres[1:])]
    headings.append(headings[-1])
    records = []
    for (left, right), heading in zip(gates, headings):
        point = (left[0], rng.randint(-50, 50), left[1], right[0], rng.randint(-50, 50), right[1])
        heading += rng.uniform(-heading_noise, heading_noise)
        records.append((point, inset(left, left, right) + inset(right, right, left),
                        f32_bits(heading), rng.choice(surfaces)))
    return records


def fixtures():
    rng = random.Random(0x1B59D)
    surfaces = list(range(30)) + list(OUTSIDE_GRIP)
    cases = []
    for _ in range(1500):
        gates = road(rng, (rng.randint(-5000, 60000), rng.randint(-5000, 60000)),
                     rng.uniform(-math.pi, math.pi), rng.choice((0, rng.uniform(-0.6, 0.6))),
                     rng.randint(40, 600), rng.randint(20, 400), rng.choice((0, 30)))
        recs = records_from(rng, gates, surfaces, rng.choice((0, 0.4, 3.5)))
        (l0, r0) = gates[0]
        t = rng.uniform(-0.6, 1.6)
        car = (l0[0] + (r0[0] - l0[0]) * t + rng.uniform(-150, 150),
               l0[1] + (r0[1] - l0[1]) * t + rng.uniform(-150, 150))
        cases.append((rng.choice((0.0, 0.5, 5.0, rng.uniform(-3, 8))), car[0], car[1], recs))
    # Collinear and degenerate sight lines: car on a straight road's centre line or on an inset point.
    for width in (36, 37, 100):
        gates = [((0, 100 * i), (width, 100 * i)) for i in range(7)]
        recs = [((l[0], 0, l[1], r[0], 0, r[1]), (l[0] + 18, l[1], r[0] - 18, r[1]),
                 f32_bits(math.pi / 2), 0) for l, r in gates]
        for car in ((width // 2, 0), (width / 2, -50), (18, 0), (width - 18, 0), (0, 0),
                    (width // 2, 600), (width // 2, 700), (-1e-9, 0)):
            cases.append((5.0, float(car[0]), float(car[1]), recs))
    # Exact zeros: the car lies on the line through an inset point and a candidate centre
    # (car = 2*inset - centre, so both atan2 arguments differ by an exact factor of 2).
    for _ in range(300):
        gates = road(rng, (rng.randint(0, 50000), rng.randint(0, 50000)), rng.uniform(-math.pi, math.pi),
                     rng.uniform(-0.5, 0.5), rng.randint(60, 400), rng.randint(40, 300))
        recs = records_from(rng, gates, [0, 2, 4])
        k, j, side = rng.randint(1, 5), rng.randint(0, 5), rng.choice((0, 2))
        point = recs[k][0]
        centre = (mid(point[0], point[3]), mid(point[2], point[5]))
        anchor = recs[j][1][side:side + 2]
        car = (2 * anchor[0] - centre[0], 2 * anchor[1] - centre[1])
        cases.append((5.0, float(car[0]), float(car[1]), recs))
    # Heading differences around +-pi for both one-pass wraps.
    for h0, h1 in ((3.14, -3.14), (-3.14, 3.14), (math.pi, -math.pi), (0.0, 3.1415926535898),
                   (-3.1415926535898, 3.1415926535898), (3.141592, -3.141592)):
        gates = road(rng, (25600, 25600), 0, 0.2, 120, 150)
        recs = records_from(rng, gates, [0])
        recs = [(p, ins, f32_bits(h0 if i % 2 == 0 else h1), s)
                for i, (p, ins, _, s) in enumerate(recs)]
        cases.append((5.0, 25600.25, 25550.5, recs))
    # Random integers, including widths of odd sign for the truncating centre.
    for _ in range(300):
        recs = [(tuple(rng.randint(-100000, 100000) for _ in range(6)),
                 tuple(rng.randint(-100000, 100000) for _ in range(4)),
                 f32_bits(rng.uniform(-math.pi, math.pi)), rng.choice(surfaces)) for _ in range(7)]
        cases.append((rng.uniform(-2, 8), rng.uniform(-1e5, 1e5), rng.uniform(-1e5, 1e5), recs))
    return cases


def parse_line(line):
    values = line.split()
    s38, car_x, car_z = map(float, values[:3])
    ints = list(map(int, values[3:]))
    if len(ints) != 7 * 12:
        raise ValueError("Real target fixture shape mismatch")
    records = []
    for r in range(7):
        v = ints[r * 12:(r + 1) * 12]
        records.append((tuple(v[:6]), tuple(v[6:10]), v[10], v[11]))
    return s38, car_x, car_z, records


def encode(case):
    s38, car_x, car_z, records = case
    fields = [repr(float(s38)), repr(float(car_x)), repr(float(car_z))]
    for point, ins, heading, surface in records:
        fields += [str(v) for v in point + ins] + [str(heading), str(surface)]
    return " ".join(fields) + "\n"


def decode(row):
    v = row.split()
    return [int(v[0]), int(v[1]), int(v[2])] + [float.fromhex(x) for x in v[3:]]


def verify(original_path, runner, assets_runner, data):
    original = TargetOriginal(original_path)
    cases = fixtures()
    synthetic = len(cases)
    real = subprocess.run([str(assets_runner), str(data), "--target-fixtures"], text=True,
                          capture_output=True, check=True, timeout=60).stdout.splitlines()
    cases += [parse_line(line) for line in real]
    if len(cases) == synthetic:
        raise ValueError("Real target runner emitted no fixtures")
    result = subprocess.run([str(runner), "--target-oracle"], text=True,
                            input="".join(map(encode, cases)), capture_output=True, check=True,
                            timeout=60)
    rows = result.stdout.splitlines()
    if len(rows) != len(cases):
        raise ValueError("Target runner row count mismatch")
    names = ("index", "offset", "sign", "x", "z", "aim", "first", "grip", "curve")
    failures, aim_error, indices, signs, outside = [], 0.0, [0] * 6, {-1: 0, 0: 0, 1: 0}, 0
    for i, (case, row) in enumerate(zip(cases, rows)):
        expected, actual = original.run(case), decode(row)
        indices[expected[0]] += 1
        signs[expected[2]] += 1
        outside += case[3][0][3] not in range(30)
        mismatch = [n for n, e, a in zip(names, expected, actual)
                    if (n != "aim" and struct.pack("<d", float(e)) != struct.pack("<d", float(a)))]
        aim_error = max(aim_error, abs(expected[5] - actual[5]))
        if abs(expected[5] - actual[5]) > 2e-15:
            mismatch.append("aim")
        if mismatch:
            failures.append(dict(index=i, real=i >= synthetic, fields=mismatch,
                                 original=expected, port=actual))
    code = sorted(original.observed)
    digest = hashlib.sha256()
    for address in code:
        digest.update(struct.pack("<I", address))
        digest.update(original.uc.mem_read(address, 1))
    root = Path(__file__).resolve().parents[1]
    return dict(schema=1, generated=datetime.now(timezone.utc).isoformat(),
                original_sha256=ORIGINAL_SHA256, fpcw=hex(FPCW),
                runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                assets_runner_sha256=hashlib.sha256(assets_runner.read_bytes()).hexdigest(),
                inputs_sha256=hashlib.sha256("".join(map(encode, cases)).encode()).hexdigest(),
                sources_sha256={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in (Path(__file__).resolve(), root / "src/game/ai.c")},
                scope="ai_drive 0x1b4a2..0x1b823 only: first heading change, target clipping by "
                      "inset points, grip byte and curvature sum; record/lane selection excluded",
                cases=len(cases), synthetic_cases=synthetic, real_cases=len(cases) - synthetic,
                outside_grip_cases=outside, index_histogram=indices,
                sign_histogram={str(k): v for k, v in signs.items()},
                aim_max_error=aim_error, failures=len(failures), examples=failures[:12],
                observed_addresses=len(code), observed_digest=digest.hexdigest())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--assets-runner", required=True, type=Path)
    parser.add_argument("--data", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    report = verify(args.original, args.runner.resolve(), args.assets_runner.resolve(), args.data)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"AI target: {report['cases']} cases ({report['real_cases']} real), "
          f"{report['failures']} failures, aim max error {report['aim_max_error']:.3g}")
    return bool(report["failures"])


if __name__ == "__main__":
    raise SystemExit(main())
