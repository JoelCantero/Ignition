#!/usr/bin/env python3
"""Compare complete original colour kernels, including all 65536 entries of the table builders.

No original callee is intercepted. Hooks outside the kernel range reject unexpected execution; hooks in
data/output memory reject writes outside the declared globals, scratch RGB and exact output extent.
"""
import argparse
from collections import Counter
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from le2elf import LE, convert

SHA256 = "179c654ba65281f08bbcda2b5943ab9f35c309ec718600f81104644d22db948c"
A, B, P, RGB, OUT = 0x400000, 0x400400, 0x400800, 0x401000, 0x410000
STACK, ESP, RETURN = 0x500000, 0x508000, 0x600000
ENTRIES = {
    "sqr_table_init": 0x10D34, "blend_table_avg": 0x10E04, "blend_table_avg_fast": 0x10EA4,
    "blend_table_lerp": 0x10F4C, "color_buckets_build": 0x11004, "rgb_avg3": 0x11200,
    "nearest_color_fast": 0x11228, "rgb_avg": 0x11384, "rgb_lerp": 0x113C0,
    "nearest_color": 0x11410, "overlay_table_build": 0x11494,
}
SQUARES, BUCKETS, RATIOS = 0xE93C0 - 299 * 4, 0xE73C0, 0xE83C0


class Original:
    def __init__(self, executable):
        import unicorn
        from unicorn import x86_const
        if unicorn.__version__ != "2.1.4":
            raise ValueError(f"Expected Unicorn 2.1.4, got {unicorn.__version__}")
        self.api, self.reg = unicorn, x86_const
        data = executable.read_bytes()
        if hashlib.sha256(data).hexdigest() != SHA256:
            raise ValueError("Unsupported original executable")
        le = LE(data)
        images, _ = convert(le)
        self.uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        end = 0
        for obj in le.objects:
            size = (obj["vsize"] + 4095) & ~4095
            self.uc.mem_map(obj["base"], size)
            self.uc.mem_write(obj["base"], bytes(images[obj["index"]]))
            end = max(end, obj["base"] + size)
        self.uc.mem_map(A, 0x21000)
        self.uc.mem_protect(A, 0x1000, unicorn.UC_PROT_READ)
        self.uc.mem_map(STACK, 0x10000)
        self.uc.mem_map(RETURN, 0x1000)
        for first, last in ((0x10000, 0x10D33), (0x10D80, 0x10E03), (0x114C9, 0x85C4F)):
            self.uc.hook_add(unicorn.UC_HOOK_CODE, self.unexpected_code, begin=first, end=last)
        self.entries = Counter()
        for entry in ENTRIES.values():
            self.uc.hook_add(unicorn.UC_HOOK_CODE, self.visit, begin=entry, end=entry)
        self.uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self.write, begin=0x10000, end=end - 1)
        self.uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self.write, begin=RGB, end=A + 0x21000 - 1)
        self.output_size = 0
        self.uc.mem_write(0xABC24, bytes(4))
        self.call(ENTRIES["sqr_table_init"])

    def unexpected_code(self, uc, address, size, _):
        raise ValueError(f"Unexpected original code 0x{address:x}")

    def visit(self, uc, address, size, _):
        self.entries[address] += 1

    def write(self, uc, access, address, size, value, _):
        ranges = ((0xABC24, 4), (SQUARES, 599 * 4), (BUCKETS, 1536 * 4),
                  (RGB, 12), (OUT, self.output_size))
        if not any(start <= address and address + size <= start + length for start, length in ranges):
            raise ValueError(f"Unexpected original write 0x{address:x}, size {size}")

    def call(self, entry, eax=0, edx=0, ebx=0, ecx=0, stack=()):
        self.uc.mem_write(ESP, struct.pack("<" + "I" * (len(stack) + 1), RETURN, *stack))
        for name, value in (("EAX", eax), ("EDX", edx), ("EBX", ebx), ("ECX", ecx),
                            ("ESI", 0x12345678), ("EDI", 0x34567890), ("EBP", 0x23456789)):
            self.uc.reg_write(getattr(self.reg, f"UC_X86_REG_{name}"), value & 0xFFFFFFFF)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, 0x127F)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.uc.emu_start(entry, RETURN, timeout=120_000_000, count=1_000_000_000)
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN:
            raise ValueError(f"Original 0x{entry:x} did not return within execution limits")
        if self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4 * (len(stack) + 1):
            raise ValueError(f"Incorrect stack cleanup at 0x{entry:x}")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != 0x127F:
            raise ValueError("Original changed the control word")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF:
            raise ValueError("Original leaked x87 stack values")
        return self.uc.reg_read(self.reg.UC_X86_REG_EAX)

    def buckets(self, palette):
        self.uc.mem_write(P, palette)
        self.uc.mem_write(BUCKETS, b"\xa5" * (1536 * 4))
        self.call(ENTRIES["color_buckets_build"], eax=P)

    def run(self, op, args):
        if op == "S":
            self.call(ENTRIES["sqr_table_init"])
            return " ".join(map(str, struct.unpack("<599i", self.uc.mem_read(SQUARES, 599 * 4))))
        if op == "K":
            self.buckets(args[0])
            values = struct.unpack("<1536i", self.uc.mem_read(BUCKETS, 1536 * 4))
            return " ".join(map(str, values))
        if op in ("C", "A", "L"):
            t, a, b = args
            self.uc.mem_write(A, bytes(a))
            self.uc.mem_write(B, bytes(b))
            if op == "C":
                return str(self.call(ENTRIES["rgb_avg3"], eax=A))
            self.call(ENTRIES["rgb_avg"] if op == "A" else ENTRIES["rgb_lerp"],
                      eax=A, edx=B, ebx=RGB, ecx=t)
            return " ".join(map(str, struct.unpack("<3i", self.uc.mem_read(RGB, 12))))
        if op == "N":
            fast, palette, rgb = args
            self.buckets(palette)
            self.uc.mem_write(RGB, struct.pack("<3i", *rgb))
            entry = ENTRIES["nearest_color_fast"] if fast else ENTRIES["nearest_color"]
            return str(self.call(entry, eax=RGB, edx=P, ebx=256))
        self.output_size = args[1] * args[2] if op == "M" else 65536
        self.uc.mem_write(OUT - 16, b"\xa5" * (65536 + 32))
        if op == "O":
            self.call(ENTRIES["overlay_table_build"], eax=OUT)
        elif op == "M":
            fast, na, nb, a, b, palette = args
            self.uc.mem_write(A, a)
            self.uc.mem_write(B, b)
            self.uc.mem_write(P, palette)
            self.uc.mem_write(BUCKETS, b"\xa5" * (1536 * 4))
            entry = ENTRIES["blend_table_avg_fast"] if fast else ENTRIES["blend_table_avg"]
            self.call(entry, eax=A, edx=na, ebx=B, ecx=nb, stack=(OUT, P, 256))
        elif op == "T":
            t, palette = args
            self.uc.mem_write(P, palette)
            self.call(ENTRIES["blend_table_lerp"], eax=P, edx=OUT, ebx=t)
        else:
            raise ValueError(f"Unknown op {op}")
        buffer = bytes(self.uc.mem_read(OUT - 16, 65536 + 32))
        if buffer[:16] != b"\xa5" * 16 or buffer[16 + self.output_size:] != b"\xa5" * (65552 - self.output_size):
            raise ValueError("Original modified output guard bytes")
        return buffer[16:16 + self.output_size].hex() if self.output_size else "-"


def palettes(data):
    yield "greys", bytes(c for c in range(256) for _ in range(3))
    yield "black", bytes(768)
    yield "brightness-edges", bytes(c for c in [0, 32, 33, 95, 96, 97, 159, 160, 161, 223, 224, 255] * 21 + [0] * 4
                                    for _ in range(3))
    rng = random.Random(0x11004)
    for limit in (64, 256):
        yield f"random-{limit}", bytes(rng.randrange(limit) for _ in range(768))
    for track in ("CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN"):
        raw = (data / "LEVELS" / track / f"{track}.COL").read_bytes()
        if len(raw) < 776:
            raise ValueError(f"Truncated palette for {track}")
        yield track, raw[8:776]


def cases(pals):
    rng = random.Random(0x11410)
    yield "squares", "S", ()
    for rgb in itertools.product((0, 1, 32, 96, 160, 255), repeat=3):
        yield "channel-mean", "C", (0, rgb, (0, 0, 0))
    for i in range(256):
        yield "pair-mean", "A", (0, (i, 255 - i, i), (255, i, i ^ 1))
    for _ in range(200):
        yield "pair-mean", "A", (0, tuple(rng.randrange(256) for _ in range(3)),
                                 tuple(rng.randrange(256) for _ in range(3)))
    for t in (-2147483648, -1073741825, -257, -256, -1, 0, 1, 127, 128, 129, 255, 256, 257, 65536, 2147483647):
        for _ in range(20):
            yield "rgb-lerp", "L", (t, tuple(rng.randrange(256) for _ in range(3)),
                                     tuple(rng.randrange(256) for _ in range(3)))
    yield "full-brightness-list-builder-only", "K", (bytes([255]) * 768,)
    for name, p in pals:
        yield "buckets", "K", (p,)
        for fast in (0, 1):
            for rgb in [(0, 0, 0), (255, 255, 255), (255, 0, 0), (0, 255, 0), (0, 0, 255)] + [
                    tuple(rng.randrange(256) for _ in range(3)) for _ in range(40)]:
                yield "nearest-fast" if fast else "nearest", "N", (fast, p, rgb)
    # Deliberately equal-distance duplicates: the first matching index must win.
    duplicate = bytes([100, 100, 100]) * 200 + bytes([200, 200, 200]) * 56
    for fast in (0, 1):
        yield "nearest-duplicate-ties", "N", (fast, duplicate, (150, 150, 150))
    for candidate in ((207, 178, 165), (196, 196, 164), (196, 196, 165)):
        threshold_palette = bytes((160, 160, 160) + candidate) + bytes(254 * 3)
        for fast in (0, 1):
            yield "fast-fallback-threshold", "N", (fast, threshold_palette, (164, 164, 164))
    for fast in (0, 1):
        for na, nb in ((0, 0), (0, 256), (256, 0), (1, 1), (1, 256), (256, 1), (3, 5), (7, 4)):
            yield "average-table", "M", (fast, na, nb, pals[3][1], pals[4][1], pals[0][1])
        yield "full-average-table", "M", (fast, 256, 256, pals[3][1], pals[4][1], pals[5][1])
    for t in (0, 128, 256):
        yield "full-lerp-table", "T", (t, pals[5][1])
    yield "full-overlay-table", "O", ()


def encode(op, args):
    if op == "S" or op == "O":
        return op
    if op == "K":
        return f"K {args[0].hex()}"
    if op in ("C", "A", "L"):
        t, a, b = args
        values = ((t,) if op == "L" else ()) + tuple(a) + (() if op == "C" else tuple(b))
        return op + " " + " ".join(map(str, values))
    if op == "N":
        fast, p, rgb = args
        return f"N {fast} {p.hex()} " + " ".join(map(str, rgb))
    if op == "M":
        fast, na, nb, a, b, p = args
        return f"M {fast} {na} {nb} {a.hex()} {b.hex()} {p.hex()}"
    t, p = args
    return f"T {t} {p.hex()}"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--original", type=Path, required=True)
    ap.add_argument("--runner", type=Path, required=True)
    ap.add_argument("--report", type=Path, required=True)
    args = ap.parse_args()
    original = Original(args.original)
    all_cases = list(cases(list(palettes(args.original.parent))))
    result = subprocess.run([str(args.runner.resolve()), "--runner"],
                            input="\n".join(encode(op, values) for _, op, values in all_cases) + "\n",
                            text=True, capture_output=True, check=True)
    native = result.stdout.splitlines()
    if len(native) != len(all_cases):
        raise ValueError(f"Runner returned {len(native)} cases, expected {len(all_cases)}")
    counts = Counter()
    output_digests = []
    for i, (category, op, values) in enumerate(all_cases):
        expected = original.run(op, values)
        if expected != native[i]:
            offset = next((j for j, (a, b) in enumerate(zip(expected, native[i])) if a != b), None)
            raise ValueError(f"Case {i} {category} ({op}) differs at output character {offset}")
        counts[category] += 1
        output_digests.append(hashlib.sha256(expected.encode("ascii")).hexdigest())
    if set(original.entries) != set(ENTRIES.values()):
        raise ValueError("Some original kernels were never executed")
    sources = ("src/game/palette.c", "src/game/palette.h", "src/render/lisa.c", "tests/palette_tables.c",
               "tests/palette_tables_original.py")
    report = {
        "original_sha256": SHA256, "unicorn": "2.1.4", "control_word": "0x127f",
        "cases": len(all_cases), "categories": dict(counts), "exact": True,
        "functions": {name: {"address": f"0x{entry:x}", "original_calls": original.entries[entry]}
                      for name, entry in ENTRIES.items()},
        "full_table_entries": 6 * 65536,
        "outputs_sha256": hashlib.sha256("".join(output_digests).encode("ascii")).hexdigest(),
        "runner_sha256": hashlib.sha256(args.runner.read_bytes()).hexdigest(),
        "sources_sha256": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources},
        "excluded": ["fast search on 256 colours all brighter than 160 (no DOS list terminator)",
                     "nearest RGB outside 0..255; table weights outside 0..256; counts outside 0..256"],
        "integration": "lisa_scene_init uses the verified overlay builder; existing SHD/TAB/PAN assets are not regenerated",
    }
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{len(all_cases)} exact colour-kernel cases; six full tables, 393216 entries; all 11 original functions executed")


if __name__ == "__main__":
    main()
