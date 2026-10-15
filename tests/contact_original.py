#!/usr/bin/env python3
"""Car contact and collision objects (car_ground_contact 0x350f0) against unchanged LE bytes.

Replays sub_000349e8 (polygon crossings), sub_000346cc (impulse), sub_00035f38 (reflection), sub_0003383c (special object under a car) and sub_00032dc8 (ground
query over the engine list), both on supplied object lists, and the flying-object
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
SCRATCH, STACK, RETURN, GRID = 0x400000, 0x500000, 0x600000, 0x700000
ESP = STACK + 0x8000
POLY_A, POLY_B, BODY_A, BODY_B, RECORD, HIT = (SCRATCH + o for o in (0, 0x100, 0x200, 0x300, 0x400, 0x500))
VIEW, LIST, CARS, POOL, MESH = (SCRATCH + o for o in (0x600, 0x700, 0x800, 0x1000, 0x2000))
VIEW_PTR, LIST_PTR, CAM_PTR, CARS_PTR, RESULT, RESULT_G = 0x24C05C, 0x24C01C, 0x1FEF9C, 0x1FEFE0, 0x1FFCA8, 0x1FF970
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
        self.uc.mem_map(GRID, 0x40000)
        if self.uc.mem_read(0xE68C8, 1)[0] & 1:
            raise ValueError("Original selects the software-FPU runtime path")
        constants = struct.unpack("<5d", self.uc.mem_read(0xA92CC, 40))
        if constants != (0.99, 0.98, 0.9, 0.6, 5.0):
            raise ValueError(f"Unexpected car_ground_contact constants: {constants}")
        self.allowed_code = []
        self.writes = []
        self.stub_view = False
        self.visited = set()
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self.code)
        self.uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self.write)

    def code(self, uc, address, size, _):
        if address == 0x58C1C and self.stub_view:
            # sub_00058c1c builds the engine's object list from a view; the replay supplies that list itself.
            esp = uc.reg_read(self.reg.UC_X86_REG_ESP)
            ret, = struct.unpack("<I", uc.mem_read(esp, 4))
            uc.reg_write(self.reg.UC_X86_REG_ESP, esp + 4)
            uc.reg_write(self.reg.UC_X86_REG_EIP, ret)
            return
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

    def run(self, entry, end, code, writes, regs, stack_args=()):
        self.allowed_code = code + [(end, end)]
        self.writes = [(STACK, STACK + 0x10000)] + writes
        for name in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + name), regs.get(name, 0))
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, 0x037F)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        if end == RETURN:
            self.put(ESP, "I%di" % len(stack_args), RETURN, *stack_args)
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
        if end == RETURN and self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4 + 4 * len(stack_args):
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


    def query(self, x, y, z, jump, objects):
        """sub_0003383c on a supplied engine object list (car 0, chase camera zeros)."""
        self.put(SCRATCH, "%dI" % (0x8000 // 4), *([0] * (0x8000 // 4)))
        self.put(VIEW_PTR, "I", VIEW)
        self.put(VIEW + 0x60, "i", len(objects))
        self.put(LIST_PTR, "I", LIST)
        self.put(CAM_PTR, "I", VIEW + 0x100)
        self.put(CARS_PTR, "I", CARS)
        self.put(CARS + 0x274, "i", jump)
        mesh = MESH
        for i, (otype, ox, oy, oz, verts, faces) in enumerate(objects):
            pool = POOL + 0x40 * i
            self.put(LIST + 4 * i, "I", pool)
            self.put(pool, "I", pool)
            self.put(pool + 4, "I", mesh)
            self.put(pool + 0xC, "3i", ox, oy, oz)
            self.put(pool + 0x1C, "I", (otype & 0xFFF) << 16)  # split_plc_types leaves the class
            self.put(mesh, "2i", len(verts) // 3, len(faces))
            self.put(mesh + 8, "%di" % len(verts), *verts)
            face_base = mesh + 8 + 4 * len(verts)
            for f, (a, b, c) in enumerate(faces):
                self.put(face_base + 44 * f, "4i", 0, a, b, c)
            mesh = face_base + 44 * len(faces)
        self.stub_view = True
        try:
            self.run(0x3383C, RETURN, [(0x3383C, 0x33EE7), (0x55F9C, 0x55FC0)],
                     [(VIEW, VIEW + 0x100), (0x1FFCA8, 0x1FFD44)],
                     {"EAX": x, "EDX": y, "EBX": z, "ECX": 0})
        finally:
            self.stub_view = False
        return [self.get(RESULT, "i")[0]]


    def ground_list(self, probe, state, x, y, z, jump, objects):
        """sub_00032dc8 on a supplied engine object list (no car cache: ecx = -1)."""
        self.put(SCRATCH, "%dI" % (0x8000 // 4), *([0] * (0x8000 // 4)))
        self.put(VIEW_PTR, "I", VIEW)
        self.put(VIEW + 0x60, "i", len(objects))
        self.put(LIST_PTR, "I", LIST)
        self.put(CARS_PTR, "I", CARS)
        self.put(0x1FEFF0, "i", 0)
        self.put(CARS + 0x274, "i", jump)
        self.put(RESULT_G, "i", state)
        mesh = MESH
        for i, (otype, ox, oy, oz, verts, faces) in enumerate(objects):
            pool = POOL + 0x40 * i
            self.put(LIST + 4 * i, "I", pool)
            self.put(pool, "I", pool)
            self.put(pool + 4, "I", mesh)
            self.put(pool + 0xC, "3i", ox, oy, oz)
            self.put(pool + 0x1C, "I", (otype & 0xFFF) << 16)
            self.put(mesh, "2i", len(verts) // 3, len(faces))
            self.put(mesh + 8, "%di" % len(verts), *verts)
            face_base = mesh + 8 + 4 * len(verts)
            for f, (flags, a, b, c) in enumerate(faces):
                self.put(face_base + 44 * f, "Iiii", flags, a, b, c)
            mesh = face_base + 44 * len(faces)
        self.run(0x32DC8, RETURN, [(0x32DC8, 0x33839), (0x55F9C, 0x55FC0)],
                 [(0x1FF6F0, 0x1FFA10)], {"EAX": x, "EDX": y, "EBX": z, "ECX": 0xFFFFFFFF}, (probe,))
        out = list(self.get(RESULT_G, "13i"))
        return out


    def view(self, x, z, layer, objects):
        """sub_00058c1c with the view of sub_0003383c (850, 0) over a 200 x 200 grid built like LI_PLACEOBJECT."""
        self.put(SCRATCH, "%dI" % (0x8000 // 4), *([0] * (0x8000 // 4)))
        self.uc.mem_write(GRID, bytes(0x40000))
        self.put(VIEW_PTR, "I", VIEW)
        self.put(VIEW, "d", float(x))
        self.put(VIEW + 0x10, "d", float(z))
        self.put(VIEW + 0x18, "2d", 850.0, 0.0)
        self.put(LIST_PTR, "I", LIST)
        self.put(0x24C014, "i", 200)
        self.put(0x24C048, "I", GRID)
        self.put(0x24BFF0, "i", layer)
        order = [i for i, o in enumerate(objects) if (o[0] & 0xFFF) < 100] + \
                [i for i, o in enumerate(objects) if (o[0] & 0xFFF) >= 100]
        pool_of = {}
        for slot, i in enumerate(order):
            otype, ox, oz = objects[i]
            pool = POOL + 0x30 * slot
            pool_of[pool] = i
            cell = GRID + 4 * ((oz // 256) * 200 + ox // 256)
            head, = self.get(cell, "I")
            self.put(pool, "I", pool)
            self.put(pool + 0xC, "i", ox)
            self.put(pool + 0x14, "i", oz)
            self.put(pool + 0x20, "hhh", 0, -1, (otype >> 12) & 0xF)
            self.put(pool + 0x26, "I", head)
            self.put(cell, "I", pool)
        self.run(0x58C1C, RETURN, [(0x58C1C, 0x58EBA), (0x55F9C, 0x55FC0)],
                 [(VIEW + 0x60, VIEW + 0x68), (LIST, LIST + 0x800), (0x24C01C, 0x24C020), (0x24C05C, 0x24C060)], {})
        n, = self.get(VIEW + 0x60, "i")
        return [n] + [pool_of[p] for p in self.get(LIST, "%dI" % n)] if n else [0]


    def texanim(self, anims):
        """sub_00010a00 on supplied animations ([0xab5f0] list, [0xab5f4] count)."""
        self.put(SCRATCH, "%dI" % (0x8000 // 4), *([0] * (0x8000 // 4)))
        at = SCRATCH + 0x100
        heads, faces_of = [], []
        for n, cur, prev, frames in anims:
            head = at
            uvs, pages, lists = head + 0x18, head + 0x18 + 8 * n, head + 0x18 + 12 * n
            at = lists + 4 * n
            self.put(head, "3i3I", n, cur, prev, uvs, pages, lists)
            mine = []
            for k, (u, v, page, faces) in enumerate(frames):
                self.put(uvs + 8 * k, "2i", u, v)
                self.put(pages + 4 * k, "I", page)
                self.put(lists + 4 * k, "I", at)
                ptrs = at
                at += 4 * (len(faces) + 1)
                for f in faces:
                    self.put(ptrs, "I", at)
                    self.put(at + 0x10, "6iI", *f)
                    mine.append(at)
                    ptrs += 4
                    at += 44
                self.put(ptrs, "I", 0)
            heads.append(head)
            faces_of.append(mine)
        self.put(at, "%dI" % len(heads), *heads)
        self.put(0xAB5F0, "Ii", at, len(heads))
        self.run(0x10A00, RETURN, [(0x10A00, 0x10B2C)], [(SCRATCH, SCRATCH + 0x8000)], {})
        out = []
        for head, mine in zip(heads, faces_of):
            out += list(self.get(head + 4, "2i"))
            for f in mine:
                out += list(self.get(f + 0x10, "6iI"))
        return out


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
    # Special-object queries: up to 6 objects of 1-4 faces around the point, mixed types and heights.
    types = (0x12C, 0x12D, 0x12E, 0x12F, 0x130, 0x131, 0x15D, 0x15E, 0x12B, 0x112C, 0x7)
    for i in range(1500):
        x, y, z = rnd.randrange(1000, 50000), rnd.randrange(-2000, 2000), rnd.randrange(1000, 50000)
        objects = []
        for _ in range(rnd.randrange(1, 7)):
            span = rnd.choice((150, 600, 1200))
            reach = rnd.choice((1000, span // 2))
            ox, oz = x + rnd.randrange(-reach, reach), z + rnd.randrange(-reach, reach)
            oy = y + rnd.choice((rnd.randrange(-300, 300), rnd.randrange(-189, 190)))
            nv = rnd.randrange(3, 7)
            verts = [v for _ in range(nv) for v in (rnd.randrange(-span, span), rnd.randrange(-50, 50),
                                                    rnd.randrange(-span, span))]
            faces = [tuple(rnd.randrange(nv) for _ in range(3)) for _ in range(rnd.randrange(1, 5))]
            objects.append((rnd.choice(types), ox, oy, oz, verts, faces))
        out.append(("Q", x, y, z, rnd.randrange(2), objects))
    # List ground queries (sub_00032dc8): road objects (class < 100) and special objects around the point.
    for i in range(1500):
        x, y, z = rnd.randrange(1000, 50000), rnd.randrange(-2000, 2000), rnd.randrange(1000, 50000)
        objects = []
        for _ in range(rnd.randrange(1, 6)):
            road = rnd.random() < 0.7
            otype = rnd.choice((2, 3, 7, 9, 0x1002)) if road else rnd.choice((0x12C, 0x12D, 0x131, 0x112C, 0x200))
            span = rnd.choice((150, 400))
            ox, oz = x + rnd.randrange(-span // 2, span // 2), z + rnd.randrange(-span // 2, span // 2)
            oy = y + rnd.randrange(-250, 250)
            nv = rnd.randrange(3, 7)
            verts = [v for _ in range(nv) for v in (rnd.randrange(-span, span), rnd.randrange(-80, 80),
                                                    rnd.randrange(-span, span))]
            faces = [(rnd.choice((0x0, 0x10000, 0x30000, 0x12D0000, 0x5A0000)) | rnd.randrange(0x20),
                      rnd.randrange(nv), rnd.randrange(nv), rnd.randrange(nv)) for _ in range(rnd.randrange(1, 5))]
            objects.append((otype, ox, oy, oz, verts, faces))
        out.append(("G", rnd.choice((0, 50)), rnd.choice((-1, 0, 3, 0x12C, 0x15E)), x, y, z, rnd.randrange(2),
                    objects))
    # Texture animation steps (sub_00010a00): frames on 64 x 64 tiles, repeated tiles, pages and long counters.
    for i in range(500):
        anims = []
        for _ in range(rnd.randrange(1, 4)):
            n = rnd.randrange(1, 8)
            frames = []
            for _ in range(n):
                u, v = rnd.randrange(4) * 0x4000, rnd.randrange(4) * 0x4000
                page = rnd.randrange(12) << 16
                faces = [tuple(rnd.randrange(0x10000) for _ in range(6)) + (page,) for _ in range(rnd.randrange(4))]
                frames.append((u, v, page, faces))
            cur = rnd.randrange(1, 300)
            anims.append((n, cur, cur - 1, frames))
        out.append(("T", anims))
    # Engine view lists (sub_00058c1c): objects scattered over cells around and beyond the view, with layers.
    for i in range(300):
        x, z = rnd.randrange(5000, 46000), rnd.randrange(5000, 46000)
        objects = []
        for _ in range(rnd.randrange(1, 120)):
            otype = rnd.choice((2, 7, 0x96, 0x12C, 0x1002, 0x112C, 0x2007, 0x3200))
            objects.append((otype, x + rnd.randrange(-3200, 3200), z + rnd.randrange(-3200, 3200)))
        out.append(("V", x, z, rnd.choice((0, 0, 1, 2, 3)), objects))
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
    if kind == "T":
        parts = ["T %d" % len(case[1])]
        for n, cur, prev, frames in case[1]:
            parts.append("%d %d %d" % (n, cur, prev))
            for u, v, page, faces in frames:
                parts.append("%d %d %d %d" % (u, v, page, len(faces)))
                parts += [" ".join(str(x) for x in f) for f in faces]
        return " ".join(parts)
    if kind == "V":
        return "V %d %d %d %d " % (case[1], case[2], case[3], len(case[4])) + \
            " ".join("%d %d %d" % o for o in case[4])
    if kind == "G":
        parts = ["G %d %d %d %d %d %d %d" % (case[1], case[2], case[3], case[4], case[5], case[6], len(case[7]))]
        for otype, ox, oy, oz, verts, faces in case[7]:
            parts.append("%d %d %d %d %d %d" % (otype, ox, oy, oz, len(verts) // 3, len(faces)))
            parts.append(" ".join(str(v) for v in verts))
            parts.append(" ".join("%d %d %d %d" % f for f in faces))
        return " ".join(parts)
    if kind == "Q":
        parts = ["Q %d %d %d %d %d" % (case[1], case[2], case[3], case[4], len(case[5]))]
        for otype, ox, oy, oz, verts, faces in case[5]:
            parts.append("%d %d %d %d %d %d" % (otype, ox, oy, oz, len(verts) // 3, len(faces)))
            parts.append(" ".join(str(v) for v in verts))
            parts.append(" ".join("%d %d %d" % f for f in faces))
        return " ".join(parts)
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
    if kind == "Q":
        return orig.query(case[1], case[2], case[3], case[4], case[5])
    if kind == "T":
        return orig.texanim(case[1])
    if kind == "V":
        return orig.view(case[1], case[2], case[3], case[4])
    if kind == "G":
        # Road answers are compared with the normal and points; special objects by their class only.
        return orig.ground_list(*case[1:])
    return orig.bounce(case[1], case[2], case[3])


def close(a, b, rel, abs_tol):
    if math.isnan(a) or math.isnan(b):
        return math.isnan(a) and math.isnan(b)
    return a == b or abs(a - b) <= max(abs_tol, rel * max(abs(a), abs(b)))


# Port intermediates are binary64 where the x87 keeps 80 bits. sub_000346cc rounds jB0 = -1.25 * j0 (masses 10 and
# 2) to float32: the exact value can lie on a float midpoint, and the 80-bit rounding of 1 / (mA + mB) breaks the tie,
# so that intermediate may end one float ulp apart; "I" allows it and the exact cases are counted separately.
TOLERANCE = {"T": (0, 0), "V": (0, 0), "G": (0, 0), "Q": (0, 0), "P": (2.5e-7, 1e-4), "I": (1e-6, 1e-5), "R": (1e-12, 1e-10), "F": (1e-15, 1e-12), "B": (1e-12, 1e-9)}
EXACT = (1e-12, 1e-12)


def compare(kind, port, orig, tolerance=None):
    if kind in "VT":
        return [int(v) for v in port] == [int(v) for v in orig]
    if kind == "G":
        n = len(port)
        return [int(v) for v in port] == [int(v) for v in orig[:n]]
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
            s["contacts"] += int(kind in "PI" and want[0] != 0) + int(kind == "B" and want[0] == 0) + \
                int(kind in "QG" and want[0] >= 0)
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
