#!/usr/bin/env python3
"""Replay complete body corner, pair impulse and box contact functions from the LE."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess

from physics_original import Original as PhysicsOriginal, BODY, BODY_SIZE, ESP, FPCW, RETURN, STACK

ROOT = Path(__file__).resolve().parents[1]
BODIES = (BODY, BODY + 0x200)
FIELDS = [
    ("mass", 0), ("inertia", 8), ("restitution", 0x10), ("heading", 0x28),
    ("yaw_rate", 0x30), ("px", 0x68), ("pz", 0x70), ("vx", 0x78), ("vz", 0x80),
    ("radius", 0x130), ("impact", 0x1B8), ("contact_x", 0x1C0), ("contact_z", 0x1C8),
] + [(f"corner_x[{i}]", 0xF0 + i * 8) for i in range(4)] + [
    (f"corner_z[{i}]", 0x110 + i * 8) for i in range(4)
]
ABS_TOL, REL_TOL = 2e-14, 1e-14


class UndefinedContact(ValueError):
    """The original uses uninitialized farthest-pair indices for a single point."""


class Original(PhysicsOriginal):
    def __init__(self, executable):
        self.mode = None
        self.calls = Counter()
        super().__init__(executable)
        edges = struct.unpack("<8i", self.uc.mem_read(0xBFE40, 32))
        if edges != (0, 1, 1, 2, 2, 3, 3, 0):
            raise ValueError(f"Unexpected edge table: {edges}")

    def code(self, uc, address, size, _):
        allowed = ((0x1E638, 0x1EBAF), (0x1EBB4, 0x1F3F8), (0x1FDB4, 0x1FEAD),
                   (0x647EF, 0x64803), (0x64C2A, 0x64DB7), (0x7D68F, 0x7D731))
        if not any(lo <= address <= hi for lo, hi in allowed):
            raise ValueError(f"Unexpected original code at 0x{address:x}")
        if address in (0x1E638, 0x1EBB4, 0x1FDB4):
            self.calls[f"{address:08x}"] += 1
        if address == 0x1F290:
            esp = uc.reg_read(self.reg.UC_X86_REG_ESP)
            best, = struct.unpack("<d", uc.mem_read(esp + 0x140, 8))
            if best == 0:
                raise UndefinedContact("No distinct intersection points: original indices are uninitialized")
        self.visited.add(address)

    def write(self, uc, access, address, size, value, _):
        allowed = [(STACK, STACK + 0x10000)] + [(p, p + BODY_SIZE) for p in BODIES]
        if self.mode == "B":
            allowed.append((0xEB800, 0xEB880))
        if not any(lo <= address and address + size <= hi for lo, hi in allowed):
            raise ValueError(f"Unexpected original write at 0x{address:x}")

    def prepare(self, address, values):
        raw = bytearray(b"\xa5" * BODY_SIZE)
        for name, offset in FIELDS:
            struct.pack_into("<d", raw, offset, values[name])
        self.uc.mem_write(address, bytes(raw))

    def run(self, mode, args, count=2):
        self.mode = mode
        entry = {"C": 0x1FDB4, "I": 0x1E638, "B": 0x1EBB4}[mode]
        before = [bytes(self.uc.mem_read(p, BODY_SIZE)) for p in BODIES[:count]]
        self.uc.mem_write(STACK, b"\xa5" * 0x10000)
        self.uc.mem_write(ESP, struct.pack("<I" + "d" * len(args), RETURN, *args))
        for reg in ("EBX", "ECX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, f"UC_X86_REG_{reg}"), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_EAX, BODIES[0])
        self.uc.reg_write(self.reg.UC_X86_REG_EDX, BODIES[1])
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, FPCW)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.uc.emu_start(entry, RETURN, timeout=2_000_000, count=100000)
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN:
            raise ValueError(f"{mode}: original failed to return within limits")
        if self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4 + 8 * len(args):
            raise ValueError(f"{mode}: unbalanced Watcom stack")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF:
            raise ValueError(f"{mode}: leaked x87 values")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != FPCW:
            raise ValueError(f"{mode}: changed x87 control word")
        mutable = set(range(0x138, 0x1B8)) if mode in ("C", "B") else set()
        if mode in ("I", "B"):
            for offset in (0x30, 0x68, 0x70, 0x78, 0x80, 0x1B8):
                mutable.update(range(offset, offset + 8))
        if mode == "B":
            mutable.update(range(0x1C0, 0x1D0))
        after = [bytes(self.uc.mem_read(p, BODY_SIZE)) for p in BODIES[:count]]
        for old, new in zip(before, after):
            if any(old[i] != new[i] for i in range(BODY_SIZE) if i not in mutable):
                raise ValueError(f"{mode}: changed input/unknown body bytes")
        if mode == "C":
            return list(struct.unpack_from("<16d", after[0], 0x138))
        result = [self.uc.reg_read(self.reg.UC_X86_REG_EAX)]
        for raw in after:
            result.extend(struct.unpack_from("<d", raw, offset)[0] for _, offset in FIELDS)
        return result

    def case(self, mode, args, bodies):
        for p, body in zip(BODIES, bodies):
            self.prepare(p, body)
        if mode == "B":
            self.run("C", (1.0,), count=1)
            # Invoke the same complete corner function for B without intercepting any callee.
            raw_a = bytes(self.uc.mem_read(BODIES[0], BODY_SIZE))
            raw_b = bytes(self.uc.mem_read(BODIES[1], BODY_SIZE))
            self.uc.mem_write(BODIES[0], raw_b)
            self.run("C", (1.0,), count=1)
            self.uc.mem_write(BODIES[1], bytes(self.uc.mem_read(BODIES[0], BODY_SIZE)))
            self.uc.mem_write(BODIES[0], raw_a)
        return self.run(mode, args, count=len(bodies))


def body(**changes):
    values = dict(mass=500, inertia=1000, restitution=0.6, heading=0, yaw_rate=0,
                  px=0, pz=0, vx=0, vz=0, radius=4, impact=17, contact_x=23, contact_z=-29)
    values.update({f"corner_x[{i}]": x for i, x in enumerate((-1, -1, 1, 1))})
    values.update({f"corner_z[{i}]": z for i, z in enumerate((-2, 2, 2, -2))})
    values.update(changes)
    return values


def fixtures():
    cases = []

    def add(name, mode, args, *bodies):
        cases.append((name, mode, args, bodies))

    for heading in (0, -0.0, math.pi / 2, math.pi, -math.pi, 2 * math.pi, -19.75, 100):
        for scale in (0, -1, 0.5, 1, 1.1, 2):
            add(f"corners-{heading}-{scale}", "C", (scale,), body(heading=heading, px=127.25, pz=-512.75))
    for theta in (0, math.pi / 2, math.pi, -math.pi / 2, 0.37):
        for velocity in (-10, 0, 10):
            add(f"impulse-axis-{theta}-{velocity}", "I", (0, 1, theta),
                body(vz=velocity), body(px=2, mass=1000))
    add("impulse-contact-at-both-centres", "I", (0, 0, 0), body(), body())
    add("impulse-contact-at-A", "I", (0, 0, 0.3), body(vx=2), body(px=2, vz=-3))
    add("impulse-contact-at-B", "I", (0, 0, 0.3), body(px=2, vx=2), body(vz=-3))
    for name, b in (
            ("far", body(px=100)), ("radius-equal", body(px=8)),
            ("radius-below", body(px=math.nextafter(8, 0))),
            ("radius-above", body(px=math.nextafter(8, math.inf))),
            ("parallel-separated", body(px=3)), ("shared-edge", body(px=2)),
            ("shared-vertex", body(px=2, pz=4)), ("identical", body()),
            ("stationary-overlap", body(px=1, pz=1)),
            ("eight-intersections", body(heading=math.pi / 4)),
            ("nested", body(**{f"corner_x[{i}]": x * 0.1 for i, x in enumerate((-1, -1, 1, 1))},
                            **{f"corner_z[{i}]": z * 0.1 for i, z in enumerate((-2, 2, 2, -2))}))):
        add(name, "B", (), body(), b)
    add("more-than-eight-intersections", "B", (),
        body(**{f"corner_x[{i}]": x for i, x in enumerate((-2, 2, -2, 2))},
             **{f"corner_z[{i}]": 0 for i in range(4)}),
        body(**{f"corner_x[{i}]": 0 for i in range(4)},
             **{f"corner_z[{i}]": z for i, z in enumerate((-2, 2, -2, 2))}))
    rng = random.Random(0x1E638)
    for i in range(1200):
        bodies = []
        for _ in range(2):
            v = body(mass=rng.choice((500, 570, 750, 1000)),
                     inertia=rng.choice((8000, rng.uniform(500, 5000))),
                     restitution=rng.choice((0, 0.3, 0.6, 1)),
                     heading=rng.uniform(-25, 25), yaw_rate=rng.uniform(-3, 3),
                     px=rng.uniform(-4, 4), pz=rng.uniform(-4, 4),
                     vx=rng.uniform(-50, 50), vz=rng.uniform(-50, 50))
            sx, sz = rng.uniform(0.5, 1.5), rng.uniform(0.5, 1.5)
            for k in range(4):
                v[f"corner_x[{k}]"] *= sx
                v[f"corner_z[{k}]"] *= sz
            bodies.append(v)
        if i < 200:
            bodies[0].update(px=rng.uniform(-1e5, 1e5), pz=rng.uniform(-1e5, 1e5))
            add(f"random-corners-{i}", "C", (rng.uniform(-2, 3),), bodies[0])
        elif i < 600:
            add(f"random-impulse-{i}", "I",
                (rng.uniform(-5, 5), rng.uniform(-5, 5), rng.uniform(-math.pi, math.pi)), *bodies)
        else:
            add(f"random-contact-{i}", "B", (), *bodies)
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    report = {"status": "fail", "timestamp": datetime.now(timezone.utc).isoformat(),
              "original_sha256": hashlib.sha256(args.original.read_bytes()).hexdigest(),
              "runner_sha256": hashlib.sha256(args.runner.read_bytes()).hexdigest(),
              "tolerances": {"absolute": ABS_TOL, "relative": REL_TOL},
              "scope": "Complete unmodified 0x1fdb4, 0x1e638, 0x1ebb4 and Watcom math callees; "
                       "no intercepted calls. Defined contacts only; uninitialized farthest-pair indices excluded."}
    try:
        original = Original(args.original)
        cases, expected, excluded, lines = [], [], [], []
        for case in fixtures():
            name, mode, values, bodies = case
            try:
                want = original.case(mode, values, bodies)
            except UndefinedContact as error:
                excluded.append({"case": name, "reason": str(error)})
                continue
            cases.append(case)
            expected.append(want)
            parts = [mode] + [format(x, ".17g") for x in values]
            for b in bodies:
                parts.extend(format(b[name], ".17g") for name, _ in FIELDS)
            lines.append(" ".join(parts))
        result = subprocess.run([str(args.runner)], input="\n".join(lines) + "\n",
                                capture_output=True, text=True, check=True, timeout=60)
        actual = result.stdout.splitlines()
        if len(actual) != len(cases):
            raise ValueError(f"Runner produced {len(actual)} rows, expected {len(cases)}")
        maximum = {"absolute": 0.0, "relative": 0.0}
        outcomes = Counter()
        for case, want, line in zip(cases, expected, actual):
            name, mode, _, _ = case
            got = [float(v) for v in line.split()]
            if len(got) != len(want):
                raise ValueError(f"{name}: {len(got)} columns, expected {len(want)}")
            if mode != "C":
                if got[0] != want[0]:
                    raise ValueError(f"{name}: return {got[0]}, expected {want[0]}")
                outcomes[f"{mode}:{int(want[0])}"] += 1
            for i, (g, w) in enumerate(zip(got, want)):
                if not math.isfinite(g) or not math.isfinite(w) or not math.isclose(
                        g, w, abs_tol=ABS_TOL, rel_tol=REL_TOL):
                    raise ValueError(f"{name} column {i}: native {g:.17g}, original {w:.17g}")
                maximum["absolute"] = max(maximum["absolute"], abs(g - w))
                if w:
                    maximum["relative"] = max(maximum["relative"], abs(g - w) / abs(w))
        report.update(status="pass", cases=dict(Counter(c[1] for c in cases)), outcomes=dict(outcomes),
                      excluded=excluded, max_error=maximum, calls=dict(original.calls),
                      visited_instructions=len(original.visited),
                      fixture_sha256=hashlib.sha256(json.dumps(fixtures(), sort_keys=True).encode()).hexdigest(),
                      sources={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in (
                          "src/game/physics.c", "src/game/physics.h", "tests/body_pair.c",
                          "tests/body_pair_original.py", "tests/physics_original.py")})
        print(f"body_pair_original: {len(cases)} cases matched; {len(excluded)} undefined contacts excluded")
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        args.report.write_text(json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()
