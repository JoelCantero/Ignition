#!/usr/bin/env python3
"""Texture-animation construction, tile helpers, stepping and teardown against unmodified LE code."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from physics_original import Original, ORIGINAL_SHA256, ESP, STACK, RETURN, ROOT, FPCW

HEAP, HEAP_SIZE = 0x700000, 0x1000000
FACE = struct.Struct("<I9iI")
ENTRIES = {
    "anim_free_all": 0x10390, "anim_free_all2": 0x1044C, "texanim_build": 0x10508,
    "anim_count_faces": 0x1098C, "texanim_step": 0x10A00, "anim_collect": 0x10B30,
    "anim_count_matches": 0x10BC8, "face_in_tile": 0x10C5C, "uv_in_tile": 0x10CF8,
}
ENTRY_NAMES = {entry: name for name, entry in ENTRIES.items()}


class AnimationOriginal(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.uc.mem_map(HEAP, HEAP_SIZE)
        self.calls = dict.fromkeys(ENTRIES, 0)
        self.brk = HEAP
        self.live = {}
        self.phase = "setup"
        self.writes_allowed = []

    def allocate(self, size, owned=False):
        address = (self.brk + 15) & ~15
        self.brk = address + max(size, 1)
        if self.brk > HEAP + HEAP_SIZE:
            raise ValueError("Animation oracle heap exhausted")
        self.uc.mem_write(address, b"\xa5" * max(size, 1))
        if owned:
            self.live[address] = size
        return address

    def code(self, uc, address, size, _):
        if address in (0x56320, 0x5629E):
            eax = uc.reg_read(self.reg.UC_X86_REG_EAX)
            if address == 0x56320:
                uc.reg_write(self.reg.UC_X86_REG_EAX, self.allocate(eax, owned=True))
            elif eax:
                if eax not in self.live:
                    raise ValueError(f"Invalid/double original free: 0x{eax:x}")
                del self.live[eax]
            sp = uc.reg_read(self.reg.UC_X86_REG_ESP)
            ret, = struct.unpack("<I", uc.mem_read(sp, 4))
            uc.reg_write(self.reg.UC_X86_REG_ESP, sp + 4)
            uc.reg_write(self.reg.UC_X86_REG_EIP, ret)
            return
        if not 0x10390 <= address <= 0x10D23:
            raise ValueError(f"Unexpected animation instruction 0x{address:x}")
        name = ENTRY_NAMES.get(address)
        if name:
            self.calls[name] += 1

    def write(self, uc, access, address, size, value, _):
        ranges = [(STACK, STACK + 0x10000), (0xAB5F0, 0xAB5F8)] + self.writes_allowed
        if not any(lo <= address and address + size <= hi for lo, hi in ranges):
            raise ValueError(f"Unexpected {self.phase} write: 0x{address:x}")

    def put(self, address, fmt, *values):
        self.uc.mem_write(address, struct.pack("<" + fmt, *values))

    def get(self, address, fmt="I"):
        return struct.unpack("<" + fmt, self.uc.mem_read(address, struct.calcsize("<" + fmt)))

    def call(self, entry, eax=0, edx=0):
        for register in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + register), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_EAX, eax)
        self.uc.reg_write(self.reg.UC_X86_REG_EDX, edx)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, FPCW)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.put(ESP, "I", RETURN)
        self.uc.emu_start(entry, RETURN, timeout=30_000_000, count=30_000_000)
        if (self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN or
                self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4 or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != FPCW):
            end = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
            raise ValueError(f"Animation return/stack/x87/execution-limit contract failed at 0x{entry:x}, end=0x{end:x}")

    def replay(self, case, destructor):
        self.brk, self.live = HEAP, {}
        self.put(0xAB5F0, "2I", 0, 0)
        self.phase = "build/step"
        self.writes_allowed = [(HEAP, HEAP + HEAP_SIZE)]
        meshes, face_ids = [], {}
        for faces in case["meshes"]:
            mesh = self.allocate(8 + FACE.size * len(faces))
            self.put(mesh, "2i", 0, len(faces))
            for f, values in enumerate(faces):
                address = mesh + 8 + f * FACE.size
                self.uc.mem_write(address, FACE.pack(*values))
                face_ids[address] = len(face_ids)
            meshes.append(mesh)
        pool = self.allocate(42 * len(case["objects"]) + 4)
        table = self.allocate(4 * (len(case["objects"]) + 1))
        self.put(0x24C02C, "I", table)
        # Same world-creation ordering as the renderer. Shared MSH pointers stay shared.
        order = sorted(range(len(case["objects"])), key=lambda o: (case["objects"][o][0] & 0xFFF) >= 100)
        for i, o in enumerate(order):
            self.put(table + i * 4, "I", pool + i * 42)
            self.put(pool + i * 42, "3I", 1, meshes[case["objects"][o][1]], 0)
        self.put(table + len(order) * 4, "i", -1)
        for kind, mesh in case["objects"]:
            if kind & 0xFFF == 0xFA:
                self.call(0x10508, meshes[mesh])
        for _ in range(case["steps"]):
            self.call(0x10A00)
        count, = self.get(0xAB5F4)
        array, = self.get(0xAB5F0)
        animations = []
        for i in range(count):
            descriptor, = self.get(array + i * 4)
            n, cur, prev, uv, page, targets = self.get(descriptor, "6I")
            animation = dict(n=n, cur=cur, prev=prev, tiles=[], targets=[])
            for k in range(n):
                animation["tiles"].append([*self.get(uv + k * 8, "2i"), *self.get(page + k * 4)])
                row, = self.get(targets + k * 4)
                ids = []
                while True:
                    face, = self.get(row + 4 * len(ids))
                    if not face:
                        break
                    if face not in face_ids or len(ids) > 65536:
                        raise ValueError("Original collected an invalid face pointer")
                    ids.append(face_ids[face])
                animation["targets"].append(sorted(ids))
            animations.append(animation)
        faces = [list(FACE.unpack(self.uc.mem_read(address, FACE.size))) for address in face_ids]
        self.phase = "teardown"
        self.writes_allowed = []
        self.call(destructor)
        if self.get(0xAB5F4)[0] != 0 or count and self.get(0xAB5F0)[0] != 0:
            raise ValueError("Original teardown did not clear the animation registry")
        # The original leaks +0x14 containers and zero-frame allocations. Native teardown deliberately fixes this.
        self.call(destructor)
        return dict(animations=animations, faces=faces, cleared=True)


def face(u=0, v=0, page=0, kind=0x11):
    return [kind, 0, 0, 0, u, v, u, v, u, v, page]


def fixtures(data):
    cases = []
    def add(name, meshes, objects, steps):
        cases.append(dict(name=name, meshes=meshes, objects=objects, steps=steps))
    for steps in (0, 1, 2, 7, 32):
        add(f"empty-{steps}", [], [], steps)
        add(f"one-frame-{steps}", [[face()]], [(0xFA, 0), (0, 0)], steps)
        add(f"repeated-shared-{steps}", [[face(), face(0x4000), face()], [face(), face(kind=0x14), face(0x4000)]],
            [(0xFA, 0), (0, 1), (1, 1), (0x100FA, 0)], steps)
    for vertex in range(3):
        for axis in range(2):
            for value in (-1, 0, 1, 0x3FFF, 0x4000, 0x4001):
                target = face()
                target[4 + vertex * 2 + axis] = value
                add(f"bound-{vertex}-{axis}-{value}", [[face(), face(0x8000)], [target]], [(0xFA, 0), (0, 1)], 3)
    rng = random.Random(0x10390)
    for i in range(160):
        meshes = []
        for _ in range(rng.randrange(1, 7)):
            faces = []
            for _ in range(rng.randrange(10)):
                u = rng.choice((-0x80000000, -0x4000, 0, 0x4000, 0x10000, 0x7FFFC000))
                v = rng.choice((-0x80000000, -0x4000, 0, 0x4000, 0x7FFFC000))
                page = rng.choice((0, 0x10000, 0x20000, 0x10001))
                faces.append(face(u, v, page, rng.choice((0x11, 0x12, 0x13, 0x14, 0x10, 0xFF))))
            meshes.append(faces)
        objects = [(rng.choice((0, 10, 0xFA, 0x1FA, 0x100FA)), rng.randrange(len(meshes)))
                   for _ in range(rng.randrange(1, 9))]
        add(f"random-{i}", meshes, objects, rng.choice((0, 1, 3, 8, 32)))
    for name in ("USA", "CANADA", "AUSTRIA", "BRAZIL", "CARIB", "ICELAND", "JAPAN"):
        directory = data / "LEVELS" / name
        plc = (directory / f"{name}.PLC").read_bytes()
        msh = (directory / f"{name}.MSH").read_bytes()
        meshes, objects, offsets = [], [], {}
        for o in range(struct.unpack_from("<I", plc)[0]):
            offset, kind = struct.unpack_from("<iI", plc, 4 + o * 20)
            if offset not in offsets:
                offsets[offset] = len(meshes)
                nv, nf = struct.unpack_from("<2i", msh, offset * 4)
                start = offset * 4 + 8 + nv * 12
                meshes.append([list(FACE.unpack_from(msh, start + f * FACE.size)) for f in range(nf)])
            objects.append((kind, offsets[offset]))
        add(f"{name}-8", meshes, objects, 8)
    return cases


def encode(case):
    values = [case["steps"], len(case["meshes"]), len(case["objects"])]
    for mesh in case["meshes"]:
        values.append(len(mesh))
        values.extend(v for f in mesh for v in f)
    values.extend(v for obj in case["objects"] for v in obj)
    return " ".join(map(str, values)) + "\n"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--original", type=Path, required=True)
    p.add_argument("--data", type=Path, required=True)
    p.add_argument("--runner", type=Path, required=True)
    p.add_argument("--report", type=Path, required=True)
    args = p.parse_args()
    report = dict(status="running", original_sha256=ORIGINAL_SHA256,
                  runner_sha256=hashlib.sha256(args.runner.read_bytes()).hexdigest(),
                  timestamp=datetime.now(timezone.utc).isoformat(), cases=0,
                  scope="Unmodified texture-animation constructor, helpers, stepping and both teardown entries; "
                        "only Watcom malloc/free intercepted. Collections compare as multisets, including aliases. "
                        "Native fixes original teardown leaks and resets its owned container.")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    try:
        cases = fixtures(args.data)
        oracle = AnimationOriginal(args.original)
        payload = "".join(encode(case) for case in cases)
        result = subprocess.run([str(args.runner), "--runner"], input=payload, text=True, capture_output=True, timeout=90)
        if result.returncode:
            raise ValueError(f"Native animation runner failed: {result.stderr}")
        lines = result.stdout.splitlines()
        if len(lines) != len(cases):
            raise ValueError("Native animation runner returned an incorrect number of cases")
        for i, (case, line) in enumerate(zip(cases, lines)):
            native = json.loads(line)
            for animation in native["animations"]:
                animation["targets"] = [sorted(row) for row in animation["targets"]]
            expected = oracle.replay(case, (0x10390, 0x1044C)[i % 2])
            if native != expected:
                raise ValueError(f"{case['name']}: native/original animation mismatch; "
                                 f"animations={native['animations'][:1]} expected={expected['animations'][:1]}")
            report["cases"] += 1
        for malformed in ("x\n", "-1 0 0\n", "0 -1 0\n", "0 0 1 250 0\n", "0 1 1 -1\n", "0 1 1 1\n"):
            failed = subprocess.run([str(args.runner), "--runner"], input=malformed, text=True, capture_output=True, timeout=5)
            if failed.returncode == 0 or not failed.stderr:
                raise ValueError("Native runner accepted malformed input or failed silently")
        if not all(oracle.calls.values()):
            raise ValueError(f"Missing original helper executions: {oracle.calls}")
        report.update(status="pass", calls=oracle.calls, fixture_sha256=hashlib.sha256(payload.encode()).hexdigest(),
                      sources={str(path.resolve().relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                               for path in (ROOT / "src/game/pos.c", ROOT / "src/game/pos.h",
                                            ROOT / "tests/texanim.c", Path(__file__))})
        print(f"texanim_original: {len(cases)} cases, nine original entries exercised, exact descriptors and face bytes")
    except Exception as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        args.report.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
