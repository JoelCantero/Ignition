#!/usr/bin/env python3
"""HUD primitives against the unmodified LE bytes: the flat triangle 0x0f (0x729f3, run through the raster
dispatcher 0x60e3d after the raster init 0x60dc4) and the matrix sprite blit (0x82950 through 0x62184) compared with
src/render/sprite.c via hud_assets_test."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from le2elf import LE, convert  # noqa: E402

ORIGINAL_SHA256 = "179c654ba65281f08bbcda2b5943ab9f35c309ec718600f81104644d22db948c"
WORK, STACK, RETURN = 0x400000, 0x500000, 0x600000
ESP = STACK + 0x8000
FB, SHADE, PAGE, LUT = WORK, WORK + 0x10000, WORK + 0x20000, WORK + 0x30000
STATE, LIST, PRIM, SPRITE, EDGES = WORK + 0x40000, WORK + 0x40100, WORK + 0x40200, WORK + 0x40400, WORK + 0x50000
W, H = 320, 200


def mix(v):
    v ^= v >> 15
    v = (v * 0x2C1B3C6D) & 0xFFFFFFFF
    v ^= v >> 12
    v = (v * 0x297A2D39) & 0xFFFFFFFF
    v ^= v >> 15
    return v & 0xFF


class Original:
    def __init__(self, executable):
        import unicorn
        from unicorn import x86_const
        if unicorn.__version__ != "2.1.4":
            raise ValueError(f"Expected Unicorn 2.1.4, found {unicorn.__version__}")
        self.reg = x86_const
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
        self.uc.mem_map(WORK, 0x60000)
        self.uc.mem_map(STACK, 0x10000)
        self.uc.mem_map(RETURN, 0x1000)
        # Shade table: row a is all a, so the triangle colour comes out unchanged.
        self.uc.mem_write(SHADE, bytes(a for a in range(256) for _ in range(256)))
        # sub_00060dc4(pitch 320, 200 rows), only the parts the triangle needs: the pitch store, the reciprocal
        # table 0xc9168 (0x60de0..0x60e02) and the row offsets 0xc8804 (0x60e1c..0x60e32); then the triangle's own
        # init callback 0x729e4 (reciprocal base 0xe3f38). The other callbacks of 0xd7c2c are not run.
        self.run(0x60DD4, 0x60DDF, eax=W, ebx=H)
        self.run(0x60DE0, 0x60E02)
        self.run(0x60E1C, 0x60E32, eax=W)
        self.call(0x729E4)
        # Matrix sprites: frame base/pitch, screen limits for sub_00062040 and the three 0x20d8-byte edge buffers of
        # sub_000782b0.
        for address, value in ((0x257EB4, FB), (0x257EBC, W), (0x257E68, W), (0x257E64, H),
                               (0x268AB4, EDGES), (0x268A98, EDGES + 0x2100), (0x268A9C, EDGES + 0x4200)):
            self.uc.mem_write(address, struct.pack("<I", value))

    def sprite(self, case):
        u0, v0, w, h, m0, m1, m2, m3, x, y, cx0, cy0, cx1, cy1, seed = case
        page = bytearray(65536)
        lut = bytearray(65536)
        for i in range(65536):
            page[i] = mix((i * 2654435761 + seed) & 0xFFFFFFFF)
            if page[i] < 40:
                page[i] = 0
            lut[i] = i & 0xFF if i >> 8 == 0 else mix((i + seed * 7919) & 0xFFFFFFFF)
        self.uc.mem_write(PAGE, bytes(page))
        self.uc.mem_write(LUT, bytes(lut))
        self.uc.mem_write(FB, bytes(((i % W) * 7 + (i // W) * 13) & 0xFF for i in range(W * H)))
        self.call(0x62040, eax=cx0, edx=cy0, ebx=cx1, ecx=cy1)
        if self.uc.reg_read(self.reg.UC_X86_REG_EAX) != 1:
            raise ValueError("clip window refused")
        # Temporary handle of sub_00062310: hotspot, u0/v0/u1/v1 (8.8), page and blend table; then the
        # {?, handle, matrix, x, y} block read by sub_00082950.
        handle = struct.pack("<8I", 0, 0, u0 << 8, v0 << 8, (u0 + w) << 8, (v0 + h) << 8, PAGE, LUT)
        self.uc.mem_write(SPRITE, handle)
        self.uc.mem_write(SPRITE + 0x40, struct.pack("<4i", m0, m1, m2, m3))
        self.uc.mem_write(SPRITE + 0x80, struct.pack("<I I I i i", 0, SPRITE, SPRITE + 0x40, x, y))
        self.call(0x82950, esi=SPRITE + 0x80)
        value = 1469598103934665603
        for b in self.uc.mem_read(FB, W * H):
            value = ((value ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        return f"{value:016x}"

    def run(self, start, stop, **regs):
        for name in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + name), regs.get(name.lower(), 0))
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.emu_start(start, stop, timeout=5_000_000, count=5_000_000)
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != stop:
            raise ValueError(f"0x{start:x} did not reach 0x{stop:x}")

    def call(self, entry, **regs):
        for name in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + name), regs.get(name.lower(), 0))
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.mem_write(ESP, struct.pack("<I", RETURN))
        self.uc.emu_start(entry, RETURN, timeout=5_000_000, count=5_000_000)
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN:
            raise ValueError(f"0x{entry:x} did not return")

    def triangle(self, tri, clip):
        self.uc.mem_write(FB, bytes(W * H))
        x0, y0, x1, y1 = clip
        # Raster state (0xc4840 layout): list, framebuffer x2, shade table, inclusive clip window.
        self.uc.mem_write(STATE, struct.pack("<8I", LIST, FB, FB, SHADE, x0, y0, x1 - 1, y1 - 1))
        self.uc.mem_write(LIST, struct.pack("<II", PRIM, 0))
        words = [0x0F] + [v for i in range(3) for v in (tri[2 * i] << 8 & 0xFFFFFFFF,
                                                         tri[2 * i + 1] << 8 & 0xFFFFFFFF, 0)] + [1, 0]
        self.uc.mem_write(PRIM, struct.pack("<12I", *words))
        self.call(0x60E3D, esi=STATE)
        fb = self.uc.mem_read(FB, W * H)
        out = []
        for y in range(H):
            row = fb[y * W:(y + 1) * W]
            xs = [x for x in range(W) if row[x]]
            if xs:
                out.append(f"{y}:{xs[0]}-{xs[-1]}")
                if len(xs) != xs[-1] - xs[0] + 1:
                    raise ValueError("Original span with a hole")
        return " ".join(out)


def cases(seed, count):
    rng = random.Random(seed)
    fixed = [
        ((10, 10, 10, 20, 20, 10), (0, 0, W, H)), ((10, 10, 30, 10, 20, 40), (0, 0, W, H)),
        ((20, 5, 5, 30, 35, 30), (0, 0, W, H)), ((50, 50, 50, 50, 50, 50), (0, 0, W, H)),
        ((5, 100, 300, 100, 40, 100), (0, 0, W, H)), ((100, 10, 100, 90, 100, 50), (0, 0, W, H)),
        ((-50, -20, 400, 120, 30, 260), (0, 0, W, H)), ((10, 10, 200, 150, 60, 180), (40, 30, 200, 120)),
        # HUD shapes: needle at several angles and the turbo quad halves.
        ((582, 440, 584, 469, 587, 443), (0, 0, W, H)), ((621, 401, 621, 375, 633, 401), (0, 0, 640, 480)),
    ]
    for tri, clip in fixed:
        if clip[2] <= W and clip[3] <= H:
            yield tri, clip
    for _ in range(count):
        span = rng.choice((8, 40, 200, 600))
        tri = tuple(rng.randint(-span // 4, W + span // 4) if i % 2 == 0 else rng.randint(-span // 4, H + span // 4)
                    for i in range(6))
        if rng.random() < 0.3:  # shared rows or columns
            i, j = rng.sample(range(3), 2)
            k = rng.choice((0, 1))
            tri = list(tri)
            tri[2 * j + k] = tri[2 * i + k]
            tri = tuple(tri)
        if rng.random() < 0.5:
            clip = (0, 0, W, H)
        else:
            cx0, cy0 = rng.randint(0, W - 2), rng.randint(0, H - 2)
            clip = (cx0, cy0, rng.randint(cx0 + 1, W), rng.randint(cy0 + 1, H))
        yield tri, clip


def sprite_cases(seed, count):
    import math
    rng = random.Random(seed + 1)
    # The HUD's own use: the finishing places (89 x 84) growing at the centre of 320x200 frames.
    for zoom in range(1, 25):
        s = int(struct.unpack("<f", struct.pack("<f", zoom * 0.04))[0] * 65536.0)
        yield (0, 0, 89, 84, s, 0, 0, s, 160 << 8, 100 << 8, 0, 0, W, H, zoom)
    for _ in range(count):
        w, h = rng.randint(1, 120), rng.randint(1, 120)
        u0, v0 = rng.randint(0, 256 - w), rng.randint(0, 256 - h)
        scale = rng.choice((rng.uniform(0.05, 1.0), rng.uniform(1.0, 2.5)))
        angle = rng.choice((0.0, 0.0, rng.uniform(0, 2 * math.pi)))
        c, sn = int(math.cos(angle) * scale * 65536), int(math.sin(angle) * scale * 65536)
        x, y = rng.randint(-80 << 8, (W + 20) << 8), rng.randint(-80 << 8, (H + 20) << 8)
        if rng.random() < 0.5:
            clip = (0, 0, W, H)
        else:
            cx0, cy0 = rng.randint(0, W - 40), rng.randint(0, H - 40)
            clip = (cx0, cy0, rng.randint(cx0 + 20, W), rng.randint(cy0 + 20, H))
        yield (u0, v0, w, h, c, sn, sn, c, x, y) + clip + (rng.randint(0, 1 << 20),)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--original", required=True, type=Path)
    ap.add_argument("--runner", required=True, help="hud_assets_test")
    ap.add_argument("--count", type=int, default=4000)
    ap.add_argument("--seed", type=int, default=1997)
    ap.add_argument("--report")
    args = ap.parse_args()
    original = Original(args.original)
    todo = list(cases(args.seed, args.count))
    lines = "".join(" ".join(map(str, tri + clip)) + "\n" for tri, clip in todo)
    port = subprocess.run([args.runner, "--triangles"], input=lines, capture_output=True, text=True,
                          check=True).stdout.splitlines()
    failures = []
    for (tri, clip), got in zip(todo, port):
        want = original.triangle(tri, clip)
        if want != got.strip():
            failures.append({"triangle": tri, "clip": clip, "original": want, "port": got.strip()})
    if len(port) != len(todo):
        failures.append({"error": f"runner answered {len(port)} of {len(todo)} cases"})
    sprites = list(sprite_cases(args.seed, args.count // 10))
    port = subprocess.run([args.runner, "--sprites"], input="".join(" ".join(map(str, c)) + "\n" for c in sprites),
                          capture_output=True, text=True, check=True).stdout.split()
    sprite_failures = [c for c, got in zip(sprites, port) if original.sprite(c) != got]
    if len(port) != len(sprites):
        sprite_failures.append(("runner answered", len(port)))
    print(f"matrix sprite 0x82950: {len(sprites) - len(sprite_failures)}/{len(sprites)} frames identical")
    for f in sprite_failures[:5]:
        print("sprite", f)
    failures += [{"sprite": list(f)} for f in sprite_failures]
    result = {"cases": len(todo), "sprite_cases": len(sprites), "failures": len(failures), "first": failures[:5]}
    if args.report:
        Path(args.report).write_text(json.dumps(result, indent=1) + "\n")
    print(f"triangle 0x729f3: {len(todo) - len(failures)}/{len(todo)} cases identical")
    for f in failures[:5]:
        print(json.dumps(f))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
