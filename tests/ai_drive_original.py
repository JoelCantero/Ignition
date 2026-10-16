#!/usr/bin/env python3
"""Execute the unmodified ai_drive controller up to car_input, then its post-call epilogue."""
import argparse
import copy
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess

from physics_original import Original, ORIGINAL_SHA256, ESP, STACK, RETURN, FPCW
from ai_target_original import f32_bits, records_from, road

CARS, BODIES, STATE, CONTROL = 0x700000, 0x760000, 0x770000, 0x780000
SECTIONS, SLOTS, RECORDS = 0x790000, 0x7A0000, 0x7B0000
PARAM_OFFSETS = (0, 8, 0x10, 0x18, 0x20, 0x28, 0x38, 0x40, 0x48)
NAMES = ("accel", "brake", "steer", "turbo", "stuck", "last_accel", "section",
         "route_choice", "route_selector", "seed", "side", "steering", "random")


class UndefinedOriginalRead(ValueError):
    pass


class DriveOriginal(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.uc.mem_map(CARS, 0x200000)
        self.observed = set()
        self.boundary = 0
        self.invalid_records = set()
        self.uc.hook_add(self.emulator.UC_HOOK_MEM_READ, self.read,
                         begin=RECORDS, end=RECORDS + 0x20000)

    def read(self, uc, access, address, size, value, _):
        start = RECORDS + (address - RECORDS) // 0x50 * 0x50
        if start in self.invalid_records and (address != start or size != 1):
            ip = uc.reg_read(self.reg.UC_X86_REG_EIP)
            raise UndefinedOriginalRead(
                f"Controller read undefined placeholder geometry at {address:x}, ip={ip:x}")

    def code(self, uc, address, size, _):
        if address == self.boundary:
            uc.emu_stop()
            return
        if not any(lo <= address <= hi for lo, hi in (
                (0x1AEA0, 0x1CC42), (0x1DECC, 0x1E08F), (0x6476E, 0x64797),
                (0x647EF, 0x64803), (0x55F9C, 0x55FB8))):
            raise ValueError(f"Unexpected controller instruction {address:x}")
        self.observed.add(address)

    def write(self, uc, access, address, size, value, _):
        if not any(lo <= address and address + size <= hi for lo, hi in (
                (STACK, STACK + 0x10000), (STATE, STATE + 0x7C),
                (0xEB7B0, 0xEB7B4), (0xEB7C4, 0xEB7C8),
                (0xEB7F2, 0xEB7FA), (0xD7D94, 0xD7D98))):
            ip = uc.reg_read(self.reg.UC_X86_REG_EIP)
            raise ValueError(f"Unexpected controller write {address:x}/{size} at {ip:x}")

    def put(self, address, fmt, *values):
        self.uc.mem_write(address, struct.pack("<" + fmt, *values))

    def run(self, c, one_byte_placeholders=False):
        tid, diff, count, cars, seed, state, params, sections = c
        self.invalid_records.clear()
        for address, value in ((0x1FEFE0, CARS), (0x1FEFE8, BODIES), (0xEB7C0, STATE),
                               (0x1FEFD0, CONTROL), (0x1FEFF8, SECTIONS),
                               (0xEB7C4, SLOTS), (0xEB7E0, SLOTS),
                               (0x1FF004, count), (0x1FEFE4, len(cars)),
                               (0x1FEF10, tid), (0x1FF164, diff), (0xD7D94, seed)):
            self.put(address, "I", value)
        self.put(0xEB7E4, "7h", 1, 2, 3, 6, 4, 0, 5)
        self.uc.mem_write(STATE, bytes(0x7C * len(cars)))
        side, steer, stuck, accel, section, choice, selector, rnd = state
        for at, value in zip(PARAM_OFFSETS, params):
            self.put(STATE + at, "i" if at == 0x48 else "d",
                     value * (0.7 if diff == 1 else 0.4 if diff >= 2 else 1)
                     if at == 0 else value)
        self.put(STATE + 0x58, "d", side)
        self.put(STATE + 0x74, "d", steer)
        self.put(STATE + 0x60, "ihhi", stuck, choice, selector, section)
        self.put(STATE + 0x54, "i", accel)
        self.put(STATE + 0x6C, "d", rnd)
        for i, car in enumerate(cars):
            sec, model, air, flat, x, z, kmh, vf, heading, sx, sz, charge, other_side, *axles = car
            base = CARS + i * 0x484C
            self.uc.mem_write(base, bytes(0x484C))
            for at, value in ((0, x + 25600), (0x10, z + 25600), (0x118, kmh),
                               (0xF8, heading), (0x100, math.atan2(sz, sx)), (0x534, charge)):
                self.put(base + at, "d", value)
            self.put(base + 0x364, "i", sec)
            self.put(base + 0x270, "i", air)
            self.put(base + 0x558, "i", flat)
            for at, value in zip((0x48, 0x50, 0x68, 0x70), axles):
                self.put(base + at, "d", value)
            self.put(BODIES + i * 0x1D0 + 0x98, "d", vf)
            self.put(CONTROL + i * 76, "i", model)
            if i:
                self.put(STATE + i * 0x7C + 0x58, "d", other_side)
        for s, (main, branch, records) in enumerate(sections):
            self.put(SECTIONS + s * 24, "i", main)
            self.put(SECTIONS + s * 24 + 12, "i", branch)
            ptrs = []
            for k, (valid, kind, surface, point, inset, heading) in enumerate(records):
                at = RECORDS + (s * 2 + k) * 0x50
                raw = bytearray(0x4B)
                struct.pack_into("<Bb6i", raw, 0, not valid, surface, *point)
                struct.pack_into("<4i", raw, 0x1A, *inset)
                struct.pack_into("<I", raw, 0x2E, heading)
                raw[0x4A] = kind
                self.uc.mem_write(at, bytes(raw))
                if not valid and one_byte_placeholders:
                    self.invalid_records.add(at)
                ptrs.append(at)
            self.put(SLOTS + s * 4, "I", SLOTS + 0x2000 + s * 8)
            self.put(SLOTS + 0x2000 + s * 8, "2I", ptrs[0], ptrs[1] if len(ptrs) == 2 else 0)
        self.put(ESP, "I", RETURN)
        for reg in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + reg), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, FPCW)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.boundary = 0x1CBF9
        self.uc.emu_start(0x1AEA0, self.boundary + 1, count=100000)
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != self.boundary:
            raise ValueError("Controller did not reach car_input")
        controls = [self.uc.reg_read(getattr(self.reg, "UC_X86_REG_" + r))
                    for r in ("EAX", "EDX", "EBX", "ECX")]
        controls = [v if v < 0x80000000 else v - 0x100000000 for v in controls]
        self.boundary = RETURN
        self.uc.emu_start(0x1CBFE, RETURN, count=1000)
        if (self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4 or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != FPCW):
            raise ValueError("Controller stack/x87 contract failed")
        raw = bytes(self.uc.mem_read(STATE, 0x7C))
        return controls + [struct.unpack_from("<i", raw, at)[0] for at in (0x60, 0x54, 0x68)] + [
            struct.unpack_from("<h", raw, at)[0] for at in (0x64, 0x66)] + [
            struct.unpack("<I", self.uc.mem_read(0xD7D94, 4))[0]] + [
            struct.unpack_from("<d", raw, at)[0] for at in (0x58, 0x74, 0x6C)]


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def fixtures():
    rng = random.Random(0x1AEA0)
    cases = []
    for i in range(1800):
        count = 200 if i < 350 else 24
        tid, diff = i % 7, i % 4
        s = rng.choice((2, 5, 10, 33, 50, 90, 94, 100, 110, 130, 140, 146, 156, 174, 190)) if count == 200 else rng.randrange(count)
        s %= count
        angle = rng.uniform(-math.pi, math.pi)
        gates = road(rng, (25600, 25600), angle, rng.choice((0, 0, rng.uniform(-0.3, 0.3))),
                     rng.randint(60, 400), rng.randint(40, 250))
        records = records_from(rng, gates, [0, 1, 2, 4, -21, 79])
        sections = []
        for j in range(count):
            point, inset, heading, surface = records[(j - s) % 7]
            point = list(point)
            if i % 3 == 0:
                point[1] = point[4] = (s - j) * 100
            sections.append((j, -1, [(1, 0, surface, point, inset, heading)]))
        if i % 4 == 0:
            for j in range(count):
                rec = sections[j][2][0]
                kind = (i // 4) % 2 + 1
                point, inset = list(rec[3]), list(rec[4])
                midpoint = [point[k] + int((point[k + 3] - point[k]) / 2) for k in range(3)]
                half_a, half_b = point[:3] + midpoint, midpoint + point[3:]
                halves = (half_b, half_a) if kind == 1 else (half_a, half_b)
                sections[j] = (j, j, [(1, kind, rec[2], halves[k], inset, rec[5]) for k in range(2)])
        elif i % 4 == 1:
            for j in range(count):
                rec = sections[j][2][0]
                point = [v + (15 if k % 3 == 0 else -10 if k % 3 == 1 else 5)
                         for k, v in enumerate(rec[3])]
                sections[j] = (j, j + count, [rec, (1, 0, rec[2], point, rec[4], rec[5])])
                if i % 8 == 1 and j != s and j % 2:
                    sections[j] = (j, -1, [rec])
        if i >= 1100:
            # Invalid future records exercise the real selector, while current records stay allocated.
            for j in range(count):
                if j in ((s + 1) % count, (s + 2) % count):
                    a, b, recs = sections[j]
                    sections[j] = (a, b, [(0, *r[1:]) for r in recs])
        x = f32(rng.uniform(-200, 200))
        z = f32(rng.uniform(-200, 200))
        kmh = f32(rng.choice((0, 10, 50, 100, 180)))
        heading = angle + rng.choice((0, rng.uniform(-2, 2)))
        car = [s, rng.choice((1, 5, 8)), int(i % 13 == 0), rng.choice((0, 0, 1, 10, 30, 40)),
               x, z, kmh, kmh / 3.6, heading, math.cos(angle), math.sin(angle),
               rng.choice((50, 100)), 0, x - 10 * math.cos(heading), z - 10 * math.sin(heading),
               x + 10 * math.cos(heading), z + 10 * math.sin(heading)]
        if i % 4 == 1:
            car[0] = -s
        if i >= 1500 and i % 3 == 0:
            car[0] = 10000
        cars = [car]
        if i % 3:
            other = list(car)
            other[4] = f32(x + rng.uniform(0, 150) * math.cos(heading))
            other[5] = f32(z + rng.uniform(0, 150) * math.sin(heading))
            other[12] = rng.choice((-3, -1, 0, 1, 3))
            other[13:17] = [other[4], other[5], other[4], other[5]]
            cars.append(other)
        if i % 23 == 0:
            for k in range(7):
                other = list(car)
                other[4] = f32(x + 100 + k * 25)
                other[5] = f32(z + 80 + k * 25)
                other[12] = (-3, 0, 3)[k % 3]
                other[13:17] = [other[4], other[5], other[4], other[5]]
                cars.append(other)
        state = [rng.choice((-2, 0, 2)), 0, rng.choice((0, 43, 44, 53, 54, 96, 97, 107, 150)),
                 0, s, i % 2, (i // 2) % 2, 0]
        params = [0.9, 0.8, 0.4, 0.6, 0.75, 0.5, 5.0, 0.6, 0]
        if i > 1000:
            params = [rng.uniform(0, 1) for _ in params]
            params[-1] = 0
        cases.append((tid, diff, count, cars, rng.getrandbits(32), state, params, sections))
    for prior in (80, 81, 90, 93, 94):
        case = copy.deepcopy(cases[1])
        tid, diff, count, cars, seed, state, params, sections = case
        cars[0][0] = 10000
        state[4] = prior
        cases.append((0, diff, count, cars, seed, state, params, sections))
    return cases


def serialize(c):
    tid, diff, count, cars, seed, state, params, sections = c
    values = [tid, diff, count, len(cars), seed, *state, *params]
    for car in cars:
        values.extend(car)
    for main, branch, records in sections:
        values.extend((main, branch, len(records)))
        for valid, kind, surface, point, inset, heading in records:
            values.extend((valid, kind, surface, *point, *inset, heading))
    return " ".join(map(str, values)) + "\n"


def runtime_sections(data):
    sections = []
    if len(data["sections"]) != len(data["records"]):
        raise ValueError("Original section/record table length mismatch")
    for table, slots in zip(data["sections"], data["records"]):
        records = []
        for raw in slots:
            if raw is None:
                continue
            packed = bytes.fromhex(raw)
            if len(packed) == 1:
                records.append((0, 0, 0, [0] * 6, [0] * 4, 0))
                continue
            invalid, surface = struct.unpack_from("<Bb", packed)
            point = struct.unpack_from("<6i", packed, 2)
            inset = struct.unpack_from("<4i", packed, 0x1A)
            heading = struct.unpack_from("<I", packed, 0x2E)[0]
            records.append((int(not invalid), packed[0x4A], surface, point, inset, heading))
        sections.append((table[0], table[2], records))
    return sections


def real_fixtures(path):
    tracks = json.loads(path.read_text())
    cases = []
    hashes = {}
    for name, data in tracks.items():
        hashes[name] = data["asset_sha256"]
        sections = runtime_sections(data)
        count = len(sections)
        p = data["original_rival_params"]
        params = [p.get(hex(at), 0) for at in PARAM_OFFSETS]
        for s, (_, _, records) in enumerate(sections):
            for variant in (0, 1):
                slot = variant if len(records) == 2 and records[variant][0] else 0
                current = records[slot]
                if not current[0]:
                    continue
                point = current[3]
                heading = struct.unpack("<f", struct.pack("<I", current[5]))[0]
                x = f32(point[0] + (point[3] - point[0]) * 0.4 - 25600 + 1)
                z = f32(point[2] + (point[5] - point[2]) * 0.4 - 25600 + 1)
                section = -s if slot and current[1] not in (1, 2) else s
                car = [section, 5 if variant else 1, 0, 1 if variant else 0,
                       x, z, 100, 100 / 3.6, heading, math.cos(heading), math.sin(heading), 100,
                       0, x - 10 * math.cos(heading), z - 10 * math.sin(heading),
                       x + 10 * math.cos(heading), z + 10 * math.sin(heading)]
                other = list(car)
                other[4] = f32(x + 100 * math.cos(heading))
                other[5] = f32(z + 100 * math.sin(heading))
                other[12] = -3 if variant else 3
                other[13:17] = [other[4], other[5], other[4], other[5]]
                state = [-2 if variant else 2, 0, 0, 0, s, slot, 1, 0]
                cases.append((data["track_id"], variant * 3, count, [car, other], 1,
                              state, params, sections))
    if len(tracks) != 7 or not cases:
        raise ValueError("Original runtime export must contain seven nonempty tracks")
    return cases, hashes


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--original", type=Path, required=True)
    ap.add_argument("--runner", type=Path, required=True)
    ap.add_argument("--report", type=Path, required=True)
    ap.add_argument("--records", type=Path, help="Runtime original tables exported by track_original.py")
    args = ap.parse_args()
    original = DriveOriginal(args.original)
    cases = fixtures()
    synthetic_count = len(cases)
    hashes = {}
    if args.records:
        real_cases, hashes = real_fixtures(args.records)
        cases += real_cases
    result = subprocess.run([str(args.runner.resolve())], input="".join(map(serialize, cases)),
                            text=True, capture_output=True, check=True, timeout=60)
    rows = result.stdout.splitlines()
    if len(rows) != len(cases):
        raise ValueError(f"Controller runner emitted {len(rows)} rows for {len(cases)} cases")
    failures = []
    executed = 0
    undefined_reads = []
    for i, (case, row) in enumerate(zip(cases, rows)):
        actual = [int(v) for v in row.split()[:10]] + [float.fromhex(v) for v in row.split()[10:]]
        if len(actual) != len(NAMES) or not all(math.isfinite(v) for v in actual):
            raise ValueError(f"Invalid native controller output in case {i}")
        try:
            expected = original.run(case, True)
        except UndefinedOriginalRead as error:
            raw = bytes(original.uc.mem_read(STATE, 0x7C))
            partial = {
                "route_choice": struct.unpack_from("<h", raw, 0x64)[0],
                "route_selector": struct.unpack_from("<h", raw, 0x66)[0],
                "seed": struct.unpack("<I", original.uc.mem_read(0xD7D94, 4))[0],
                "random": struct.unpack_from("<d", raw, 0x6C)[0],
            }
            for name, expected_value in partial.items():
                if actual[NAMES.index(name)] != expected_value:
                    failures.append(dict(case=i, field=name, port=actual[NAMES.index(name)],
                                         original=expected_value))
            undefined_reads.append(dict(case=i, track=case[0], section=case[3][0][0],
                                        reason=str(error), compared_fields=list(partial)))
            continue
        except ValueError as error:
            raise ValueError(f"Case {i}, track={case[0]}, section={case[3][0][0]}: {error}") from error
        executed += 1
        if len(expected) != len(NAMES):
            raise ValueError("Controller output shape mismatch")
        for name, a, e in zip(NAMES, actual, expected):
            if a != e and not (isinstance(e, float) and math.isclose(a, e, rel_tol=2e-13, abs_tol=2e-13)):
                failures.append(dict(case=i, field=name, port=a, original=e))
    required = {0x1AF89, 0x1B018, 0x1AEFF, 0x1B876, 0x1B8B9, 0x1C05D, 0x1C3EC,
                0x1C600, 0x1C7D1, 0x1C9B0, 0x1CB2E, 0x1CBBB}
    if not failures and not required <= original.observed:
        raise ValueError(f"Missing original controller branches: {sorted(required - original.observed)}")
    report = dict(timestamp=datetime.now(timezone.utc).isoformat(), original_sha256=ORIGINAL_SHA256,
                  runner_sha256=hashlib.sha256(args.runner.read_bytes()).hexdigest(),
                  runtime_export_sha256=hashlib.sha256(args.records.read_bytes()).hexdigest()
                  if args.records else None,
                  cases=executed, candidate_cases=len(cases), observed_instructions=len(original.observed),
                  controller_instructions=sum(0x1AEA0 <= a <= 0x1CC42 for a in original.observed),
                  synthetic_candidates=synthetic_count,
                  synthetic_cases=synthetic_count - sum(r["case"] < synthetic_count for r in undefined_reads),
                  real_cases=len(cases) - synthetic_count -
                  sum(r["case"] >= synthetic_count for r in undefined_reads),
                  undefined_original_reads=undefined_reads,
                  asset_sha256=hashes,
                  native_diagnostics=sorted(set(result.stderr.splitlines())),
                  required_branches=[hex(a) for a in sorted(required)],
                  scope="ai_drive entry through car_input arguments and original post-call epilogue",
                  exclusions=["car_input side effects", "current absent/invalid TRI slots",
                              "listed original one-byte-placeholder overreads",
                              "whole-race equivalence"],
                  tolerances=dict(integer=0, float_absolute=2e-13, float_relative=2e-13),
                  failures=failures[:30], failure_count=len(failures), passed=not failures)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
