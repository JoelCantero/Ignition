#!/usr/bin/env python3
"""Car contact and collision objects (car_ground_contact 0x350f0) against unchanged LE bytes.

Replays sub_000349e8 (polygon crossings), sub_000346cc (impulse), sub_00035f38 (reflection) and the flying-object
fragments 0x356b8-0x358ff (motion) and 0x35967-0x35c84 (bounce on a supplied ground-query result) under Unicorn,
and compares them with `contact_test --runner`. Not a DOS/game emulator: the ground query, sounds, random numbers and
effects of the full frame are outside these fragments.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from le2elf import LE, convert  # noqa: E402

ORIGINAL_SHA256 = "179c654ba65281f08bbcda2b5943ab9f35c309ec718600f81104644d22db948c"
SCRATCH, STACK, RETURN = 0x400000, 0x500000, 0x600000
ESP = STACK + 0x8000
POLY_A, POLY_B, BODY_A, BODY_B, RECORD, HIT = (SCRATCH + o for o in (0, 0x100, 0x200, 0x300, 0x400, 0x500))
THETA, CX, CZ = 0x1FEE98, 0x1FEE9C, 0x1FEEA8
CROSS_X, CROSS_Z = 0x207A80, 0x207A60
COLL_LIST = 0x1FEFC0
# Watcom layouts, independent of the port's structures.
BODY_FMT = "<5d3f"
RECORD_FIELDS = ((8, "i"), (0xC, "3d"), (0x24, "3d"), (0x3C, "3d"), (0x54, "3d"))


class Original:
    def __init__(self, executable):
        import unicorn
        from unicorn import x86_const
        if unicorn.__version__ != "2.1.4":
            raise ValueError(f"Expected Unicorn 2.1.4, found {unicorn.__version__}")
        self.unicorn, self.reg = unicorn, x86_const
        data = executable.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != ORIGINAL_SHA256:
            raise ValueError(f"Unsupported original SHA-256: {digest}")
        le = LE(data)
        images, _ = convert(le)
        self.uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        for obj in le.objects:
            self.uc.mem_map(obj["base"], (obj["vsize"] + 4095) & ~4095)
            self.uc.mem_write(obj["base"], bytes(images[obj["index"]]))
        for base in (SCRATCH, STACK, RETURN):
            self.uc.mem_map(base, 0x10000)
        if self.uc.mem_read(0xE68C8, 1)[0] & 1:
            raise ValueError("Original selects the software-FPU runtime path")
        constants = struct.unpack("<5d", self.uc.mem_read(0xA92CC, 40))
        if constants != (0.99, 0.98, 0.9, 0.6, 5.0):
            raise ValueError(f"Unexpected car_ground_contact constants: {constants}")
        self.allowed_code = []
        self.writes = []
        self.visited = set()
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self.code)
        self.uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self.write)

    def code(self, uc, address, size, _):
        if not any(lo <= address <= hi for lo, hi in self.allowed_code):
            raise ValueError(f"Unexpected code execution at 0x{address:x}")
        self.visited.add(address)

    def write(self, uc, access, address, size, value, _):
        if not any(lo <= address and address + size <= hi for lo, hi in self.writes):
            raise ValueError(f"Unexpected original write at 0x{address:x}")

    def put(self, address, fmt, *values):
        self.uc.mem_write(address, struct.pack("<" + fmt, *values))

    def get(self, address, fmt):
        return struct.unpack("<" + fmt, self.uc.mem_read(address, struct.calcsize("<" + fmt)))

    def run(self, entry, end, code, writes, regs):
        self.allowed_code = code + [(end, end)]
        self.writes = [(STACK, STACK + 0x10000)] + writes
        for name in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + name), regs.get(name, 0))
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, 0x037F)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        if end == RETURN:
            self.put(ESP, "I", RETURN)
            self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        else:
            self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP - 0x200)
        try:
            self.uc.emu_start(entry, end, timeout=2_000_000, count=200_000)
        except self.unicorn.UcError as error:
            address = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
            raise ValueError(f"Original emulation failed at 0x{address:x}: {error}") from error
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != end:
            raise ValueError(f"Original did not reach 0x{end:x}")
        if end == RETURN and self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4:
            raise ValueError("Original did not return with a balanced stack")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF:
            raise ValueError("Original leaked x87 stack values")
        return self.uc.reg_read(self.reg.UC_X86_REG_EAX)

    ATAN2 = [(0x647EF, 0x64803)]
    POW = [(0x60000, 0x86000)]  # Watcom math runtime (pow and its helpers)

    def polygons(self, a, b):
        for base, pts in ((POLY_A, a), (POLY_B, b)):
            for i, (x, z) in enumerate(pts):
                self.put(base + 8 * i, "2f", x, z)
        self.put(THETA, "3f", 0.0, 0.0, 0.0)
        result = self.run(0x349E8, RETURN, [(0x349E8, 0x350C8), (0x55F9C, 0x55FC0)] + self.ATAN2 + self.POW,
                          [(THETA, THETA + 4), (CX, CX + 4), (CZ, CZ + 4),
                           (CROSS_Z, CROSS_Z + 0x40)],
                          {"EAX": POLY_A, "EDX": len(a), "EBX": POLY_B, "ECX": len(b)})
        theta, = self.get(THETA, "f")
        cx, = self.get(CX, "f")
        cz, = self.get(CZ, "f")
        return [result, cx, cz, theta] if result else [0]

    def impulse(self, a, b, cx, cz, theta):
        self.put(BODY_A, BODY_FMT[1:], *a)
        self.put(BODY_B, BODY_FMT[1:], *b)
        self.put(THETA, "3f", theta, cx, 0.0)
        self.put(CZ, "f", cz)
        result = self.run(0x346CC, RETURN, [(0x346CC, 0x349E4), (0x55F9C, 0x55FC0)] + self.ATAN2,
                          [(BODY_A, BODY_A + 0x34), (BODY_B, BODY_B + 0x34)], {"EAX": BODY_A, "EDX": BODY_B})
        out = [result]
        for base in (BODY_A, BODY_B):
            x, z, vx, vz, w, mass, inertia, impact = self.get(base, BODY_FMT[1:])
            out += [vx, vz, w, impact]
        return out

    def reflect(self, n, v):
        self.put(SCRATCH, "6d", *n, *v)
        self.run(0x35F38, RETURN, [(0x35F38, 0x360EC)] + self.ATAN2, [(SCRATCH + 0x18, SCRATCH + 0x30)],
                 {"EAX": SCRATCH})
        return list(self.get(SCRATCH + 0x18, "3d"))

    def put_record(self, rec):
        self.put(RECORD, "38I", *([0xA5A5A5A5] * 38))
        self.put(RECORD + 8, "i", rec[0])
        for k, offset in enumerate((0xC, 0x24, 0x3C, 0x54)):
            self.put(RECORD + offset, "3d", *rec[1 + 3 * k:4 + 3 * k])
        self.put(COLL_LIST, "I", RECORD)

    def get_record(self):
        out = [self.get(RECORD + 8, "i")[0]]
        for offset in (0xC, 0x24, 0x3C, 0x54):
            out += list(self.get(RECORD + offset, "3d"))
        return out

    def fly(self, rec):
        self.put_record(rec)
        self.put(ESP - 0x200 + 0x12C, "I", 0)
        self.run(0x356B8, 0x358FF, [(0x356B8, 0x358FE)],
                 [(RECORD + 0xC, RECORD + 0x6C)], {"EDX": RECORD})
        return self.get_record()

    def bounce(self, rec, normal, points):
        self.put_record(rec)
        self.put(ESP - 0x200 + 0x12C, "I", 0)
        self.put(HIT, "13i", 0, *normal, *points)
        self.run(0x35967, 0x35C84, [(0x35967, 0x35C83), (0x35F38, 0x360EC)] + self.ATAN2 + self.POW,
                 [(RECORD + 8, RECORD + 0x6C)], {"EAX": HIT, "EDX": HIT})
        return self.get_record()


def f32(v):
    return struct.unpack("<f", struct.pack("<f", v))[0]


def cases(seed):
    rnd = random.Random(seed)
    out = []

    def box(cx, cz, hx, hz, a):
        c, s = math.cos(a), math.sin(a)
        return [(f32(cx + x * c - z * s), f32(cz + z * c + x * s)) for x, z in ((-hx, -hz), (-hx, hz), (hx, hz), (hx, -hz))]

    # Polygons: car-sized boxes at random headings against the 60x60 object box (engine coordinates).
    for i in range(1500):
        ox, oz = rnd.uniform(1000, 50000), rnd.uniform(1000, 50000)
        obj = [(f32(ox - 30), f32(oz - 30)), (f32(ox + 30), f32(oz - 30)), (f32(ox + 30), f32(oz + 30)),
               (f32(ox - 30), f32(oz + 30))]
        car = box(ox + rnd.uniform(-90, 90), oz + rnd.uniform(-90, 90), rnd.uniform(30, 60), rnd.uniform(15, 30),
                  rnd.uniform(0, 2 * math.pi) if i % 7 else rnd.choice((0, math.pi / 2, math.pi)))
        out.append(("P", car, obj))
    out.append(("P", [(0, 0), (10, 0), (10, 10), (0, 10)], [(0, 0), (10, 0), (10, 10), (0, 10)]))
    out.append(("P", [(-10, -10), (10, -10), (10, 10), (-10, 10)], [(0, -13), (13, 0), (0, 13), (-13, 0)]))
    comb = [(float(x), float(z)) for i in range(5) for x, z in ((i * 20, 0), (i * 20 + 10, 5))]
    out.append(("P", comb, [(-1, 1), (100, 1), (100, 2), (-1, 2)]))
    # Impulses: car (mass 10, inertia 4000) against objects of the three masses.
    for i in range(1500):
        mass, inertia = rnd.choice(((0.3, 100.0), (25.0, 8000.0), (2.0, 1500.0)))
        car = [rnd.uniform(1000, 50000), rnd.uniform(1000, 50000), rnd.uniform(-20, 20), rnd.uniform(-20, 20),
               rnd.uniform(-3, 3), 10.0, 4000.0, f32(rnd.uniform(0, 20))]
        obj = [car[0] + rnd.uniform(-80, 80), car[1] + rnd.uniform(-80, 80), rnd.uniform(-5, 5), rnd.uniform(-5, 5),
               rnd.uniform(-1, 1) if i % 3 else 0.0, mass, inertia, f32(rnd.uniform(0, 20))]
        cx, cz = (car[0] + obj[0]) / 2 + rnd.uniform(-20, 20), (car[1] + obj[1]) / 2 + rnd.uniform(-20, 20)
        out.append(("I", car, obj, f32(cx), f32(cz), f32(rnd.uniform(-math.pi, math.pi))))
    # Reflections, including axis-aligned normals.
    for i in range(1500):
        n = [rnd.choice((0.0, rnd.uniform(-1e6, 1e6))) for _ in range(3)]
        if not any(n):
            n[1] = 1e6
        out.append(("R", n, [rnd.uniform(-30, 30) for _ in range(3)]))
    # Flying motion, with angle wraps and the world clamp.
    for i in range(1500):
        pos = [rnd.uniform(-100, 51300), rnd.uniform(-10100, 10100), rnd.uniform(-100, 51300)]
        rec = [1] + pos + [rnd.uniform(-30, 30) for _ in range(3)] + \
              [rnd.choice((rnd.uniform(-50, 3700), 3599.5, 0.0, -0.25)) for _ in range(3)] + \
              [rnd.uniform(-200, 200) for _ in range(3)]
        out.append(("F", rec))
    # Bounces on integer faces around the object: above (no contact), within 15 and below, fast and slow.
    for i in range(1500):
        x, z, ground = rnd.uniform(1000, 50000), rnd.uniform(1000, 50000), rnd.uniform(-2000, 2000)
        pts = []
        for _ in range(3):
            px, pz = int(x + rnd.uniform(-300, 300)), int(z + rnd.uniform(-300, 300))
            pts.append([px, int(-ground + rnd.uniform(-40, 40)), pz])
        a = [pts[0][k] - pts[1][k] for k in range(3)]
        b = [pts[2][k] - pts[1][k] for k in range(3)]
        normal = [a[1] * b[2] - b[1] * a[2], a[2] * b[0] - b[2] * a[0], a[0] * b[1] - b[0] * a[1]]
        if normal[1] == 0:
            continue
        speed = rnd.choice((0.5, 1.5, 3.0, 10.0, 30.0))
        vel = [rnd.uniform(-1, 1) * speed for _ in range(3)]
        rec = [1, x, ground + rnd.uniform(-40, 60), z] + vel + [rnd.uniform(0, 3599) for _ in range(3)] + \
              [rnd.uniform(-50, 50) for _ in range(3)]
        out.append(("B", rec, normal, [v for p in pts for v in p]))
    return out


def runner_input(case):
    kind = case[0]
    if kind == "P":
        a, b = case[1], case[2]
        return "P %d %d %s %s" % (len(a), len(b), " ".join("%.9g %.9g" % p for p in a),
                                  " ".join("%.9g %.9g" % p for p in b))
    if kind == "I":
        body = lambda v: " ".join("%.17g" % x for x in v)
        return "I %s %s %.9g %.9g %.9g" % (body(case[1]), body(case[2]), case[3], case[4], case[5])
    if kind == "R":
        return "R " + " ".join("%.17g" % x for x in case[1] + case[2])
    if kind == "F":
        return "F %d " % case[1][0] + " ".join("%.17g" % x for x in case[1][1:])
    return "B %d " % case[1][0] + " ".join("%.17g" % x for x in case[1][1:]) + " " + \
        " ".join(str(v) for v in case[2] + case[3])


def original_output(orig, case):
    kind = case[0]
    if kind == "P":
        return orig.polygons(case[1], case[2])
    if kind == "I":
        return orig.impulse(case[1], case[2], case[3], case[4], case[5])
    if kind == "R":
        return orig.reflect(case[1], case[2])
    if kind == "F":
        return orig.fly(case[1])
    return orig.bounce(case[1], case[2], case[3])


def close(a, b, rel, abs_tol):
    if math.isnan(a) or math.isnan(b):
        return math.isnan(a) and math.isnan(b)
    return a == b or abs(a - b) <= max(abs_tol, rel * max(abs(a), abs(b)))


# Port intermediates are binary64 where the x87 keeps 80 bits. sub_000346cc rounds jB0 = -1.25 * j0 (masses 10 and
# 2) to float32: the exact value can lie on a float midpoint, and the 80-bit rounding of 1 / (mA + mB) breaks the tie,
# so that intermediate may end one float ulp apart; "I" allows it and the exact cases are counted separately.
TOLERANCE = {"P": (2.5e-7, 1e-4), "I": (1e-6, 1e-5), "R": (1e-12, 1e-10), "F": (1e-15, 1e-12), "B": (1e-12, 1e-9)}
EXACT = (1e-12, 1e-12)


def compare(kind, port, orig, tolerance=None):
    if kind == "P":
        if port[0] != orig[0]:
            return False
        if not orig[0]:
            return True
    if kind in ("I", "F", "B") and int(port[0]) != int(orig[0]):
        return False
    if kind == "I" and (port[4] != orig[4] or port[8] != orig[8]):
        return False  # impacts: |trunc(normal speed)| on contact, untouched otherwise
    rel, abs_tol = tolerance or TOLERANCE[kind]
    start = 1 if kind in ("P", "I", "F", "B") else 0
    return all(close(float(p), float(o), rel, abs_tol) for p, o in zip(port[start:], orig[start:]))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--original", type=Path, required=True)
    ap.add_argument("--runner", type=Path, required=True)
    ap.add_argument("--seed", type=int, default=0x350F0)
    ap.add_argument("--report", type=Path)
    args = ap.parse_args()
    try:
        orig = Original(args.original)
        todo = cases(args.seed)
        text = "\n".join(runner_input(c) for c in todo) + "\n"
        result = subprocess.run([str(args.runner), "--runner"], input=text, text=True, capture_output=True,
                                check=True, timeout=120)
        lines = result.stdout.splitlines()
        if len(lines) != len(todo):
            raise ValueError(f"Runner returned {len(lines)} lines for {len(todo)} cases")
        stats, failures = {}, []
        for case, line in zip(todo, lines):
            kind = case[0]
            port = [float(v) for v in line.split()]
            want = original_output(orig, case)
            ok = compare(kind, port, want)
            s = stats.setdefault(kind, {"cases": 0, "failures": 0, "contacts": 0, "exact": 0})
            s["cases"] += 1
            s["exact"] += int(compare(kind, port, want, EXACT))
            s["contacts"] += int(kind in "PI" and want[0] != 0) + int(kind == "B" and want[0] == 0)
            if not ok:
                s["failures"] += 1
                if len(failures) < 10:
                    failures.append({"case": runner_input(case), "port": port, "original": want})
    except (ValueError, OSError, ModuleNotFoundError, subprocess.SubprocessError) as error:
        print(f"contact_original: {error}", file=sys.stderr)
        return 2
    report = {"seed": args.seed, "stats": stats, "failures": failures,
              "instructions_visited": len(orig.visited)}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    for kind, s in sorted(stats.items()):
        print(f"{kind}: {s['cases']} cases, {s['exact']} exact (1e-12), {s['failures']} mismatches, "
              f"{s['contacts']} contacts/rests")
    for f in failures:
        print(json.dumps(f), file=sys.stderr)
    return 1 if any(s["failures"] for s in stats.values()) else 0


if __name__ == "__main__":
    sys.exit(main())
