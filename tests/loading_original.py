#!/usr/bin/env python3
"""Compare loading construction/progress pixels with the unmodified LE in all six languages/seven tracks.

Execute original boxes, font layout/width, PAN compositing and progress. Intercept allocator, strlen/sprintf("%s"),
palette/presentation, target selection and sprite-device calls; sprite texels use the real PIC/LFT bytes.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess

import unicorn

from hud_original import ESP, Original, RETURN, ORIGINAL_SHA256


PIC, PAN, IDENTITY, OVERLAY, BOXES, HEAP = 0x700000, 0x720000, 0x730000, 0x740000, 0x7C0000, 0x7D0000
FB = 0xEBB60
LEVELS = ("CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN")
PERCENTAGES = (*range(101), 0)


def hash_bytes(data):
    value = 1469598103934665603
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


class Loading(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.uc.mem_map(PIC, 0x100000)
        self.uc.mem_write(IDENTITY, bytes(s for s in range(256) for _ in range(256)))
        self.uc.mem_write(BOXES, (executable.parent / "MENUBKG.DAT").read_bytes())
        remap = bytearray(range(256))
        for src, dst in ((0xC1, 0xF5), (0xC3, 0xBD), (0xC4, 0xBE), (0xD5, 0xB4),
                         (0xD6, 0xBB), (0xFD, 0xBC), (0xC0, 0xBD), (0xC2, 0xF4)):
            remap[src] = dst
        self.uc.mem_write(0x208660, bytes(remap))
        self.sprites = {}
        raw = (executable.parent / "N_SYSGFX.PIC").read_bytes()
        for i in range(4):
            pixels = b"".join(raw[0x34E + i * 20 * 256 + 236 + row * 256:
                                 0x34E + i * 20 * 256 + 256 + row * 256] for row in range(20))
            self.sprites[i + 1] = (pixels, 20, 20)
            self.put32(0x1AC478 + i * 4, i + 1)
        pixels = b"".join(raw[0xC1E7 + row * 256:0xC1E7 + row * 256 + 61] for row in range(58))
        self.sprites[5] = (pixels, 61, 58)
        self.put32(0x1FED98, 5)
        for slot, name in ((1, "RED_LITE"), (3, "BLUELITE")):
            data = (executable.parent / "BALTAZAR" / "DATA" / f"{name}.LFT").read_bytes()
            base = 0x24C214 + slot * 0x63E
            self.uc.mem_write(base, bytes(0x63E))
            for offset, value in ((0, 1), (0xC, 1), (0x10, 1)):
                self.put32(base + offset, value)
            self.uc.mem_write(base + 0x18, data[6:12])
            for glyph in range(224):
                offset = struct.unpack_from("<I", data, 0xC + glyph * 4)[0]
                width = struct.unpack_from("<h", data, 0x68C + glyph * 2)[0]
                height = struct.unpack_from("<h", data, 10)[0]
                self.uc.mem_write(base + 0x47E + glyph * 2, struct.pack("<h", width))
                if offset != 0xFFFFFFFF:
                    self.uc.mem_write(base + 0x1E + glyph, b"\x01")
                    handle = 1000 + slot * 224 + glyph
                    self.put32(base + 0xFE + glyph * 4, handle)
                    self.sprites[handle] = (data[0x84C + offset:0x84C + offset + max(width, 0) * height],
                                            max(width, 0), height)
        for address in (0x7708C, 0x77171, 0x6482C, 0x5629E, 0x62775, 0x55910, 0x55948, 0x55968, 0x519C8, 0x22238):
            self.uc.hook_add(unicorn.UC_HOOK_CODE, self.boundary, begin=address, end=address)
        self.capture_build = False
        self.heap = HEAP
        self.target = (FB, 320, 320, 200)

    def put32(self, address, value):
        self.uc.mem_write(address, struct.pack("<I", value))

    def u32(self, address):
        return struct.unpack("<I", self.uc.mem_read(address, 4))[0]

    def boundary(self, uc, address, _size, _user):
        if address == 0x22238:
            if self.capture_build:
                self.built = [hash_bytes(uc.mem_read(FB, 64000)), hash_bytes(uc.mem_read(OVERLAY, 320 * 480))]
                self.capture_build = False
            return
        eax = uc.reg_read(self.reg.UC_X86_REG_EAX)
        edx = uc.reg_read(self.reg.UC_X86_REG_EDX)
        sp = uc.reg_read(self.reg.UC_X86_REG_ESP)
        popped = 4
        if address == 0x7708C:
            popped = 8
        elif address == 0x77171:
            text = bytes(uc.mem_read(eax, 64)).split(b"\0", 1)[0]
            uc.reg_write(self.reg.UC_X86_REG_EAX, len(text))
        elif address == 0x6482C:
            size = eax * edx
            if self.heap + size > 0x7E0000:
                raise AssertionError("Loading allocation exceeded the test heap")
            uc.mem_write(self.heap, bytes(size))
            uc.reg_write(self.reg.UC_X86_REG_EAX, self.heap)
            self.heap += (size + 15) & ~15
        elif address == 0x62775:
            dest, fmt, source = (self.u32(sp + offset) for offset in (4, 8, 12))
            if bytes(uc.mem_read(fmt, 3)) != b"%s\0":
                raise AssertionError("Unexpected loading format string")
            text = bytes(uc.mem_read(source, 64)).split(b"\0", 1)[0]
            uc.mem_write(dest, text + b"\0")
            uc.reg_write(self.reg.UC_X86_REG_EAX, len(text))
        elif address == 0x55948:
            self.target = (eax, edx, uc.reg_read(self.reg.UC_X86_REG_EBX), uc.reg_read(self.reg.UC_X86_REG_ECX))
            popped = 8
        elif address == 0x55968:
            if uc.reg_read(self.reg.UC_X86_REG_EBX):
                raise AssertionError("Unexpected transformed loading sprite")
            x, y = struct.unpack("<ii", uc.mem_read(edx, 8))
            x, y = x >> 8, y >> 8
            pixels, width, height = self.sprites[eax]
            dest, pitch, screen_w, screen_h = self.target
            frame = bytearray(uc.mem_read(dest, pitch * screen_h))
            lut = uc.mem_read(self.u32(0x257E48), 65536)
            for sy in range(height):
                if not 0 <= y + sy < screen_h:
                    continue
                for sx in range(width):
                    if 0 <= x + sx < screen_w:
                        at = (y + sy) * pitch + x + sx
                        frame[at] = lut[pixels[sy * width + sx] * 256 + frame[at]]
            uc.mem_write(dest, bytes(frame))
        uc.reg_write(self.reg.UC_X86_REG_EIP, self.u32(sp))
        uc.reg_write(self.reg.UC_X86_REG_ESP, sp + popped)

    def execute(self, entry, value=0):
        for name in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + name), value if name == "EAX" else 0)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.put32(ESP, RETURN)
        self.uc.emu_start(entry, RETURN, timeout=10_000_000, count=20_000_000)
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN:
            raise AssertionError(f"Loading 0x{entry:x} did not return")

    def screen(self, language, track, data_dir):
        self.uc.mem_write(PIC, (data_dir / "LEVELS" / LEVELS[track] / f"{LEVELS[track]}.PIC").read_bytes())
        self.uc.mem_write(PAN, (data_dir / "LEVELS" / LEVELS[track] / f"{LEVELS[track]}.PAN").read_bytes())
        for address, value in ((0x1FF000, 320), (0x1FEFFC, 200), (0x1FEF80, PIC), (0x1FEF38, PAN),
                               (0x1FEBFC, OVERLAY), (0x1FEC0C, IDENTITY), (0x20876C, BOXES),
                               (0x1AC4AC, 1), (0x1AC4B4, 3), (0x1FF6EB, language), (0x1FEF10, track),
                               (0x1FEF2C, PIC), (0xC57FC, 0), (0x208778, 0xC57FC)):
            self.put32(address, value)
        # The corner dimensions live in a sprite descriptor, not in the PIC header.
        self.put32(0x1FEF2C, HEAP + 0x8000)
        self.put32(HEAP + 0x8008, 20)
        self.put32(HEAP + 0x800C, 20)
        self.heap = HEAP
        self.capture_build = True
        self.execute(0x21D68)
        hashes = self.built + [hash_bytes(self.uc.mem_read(FB, 64000))]
        for percent in PERCENTAGES[1:]:
            self.execute(0x22238, percent)
            hashes.append(hash_bytes(self.uc.mem_read(FB, 64000)))
        return hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    original = Loading(args.original)
    output = subprocess.check_output([str(args.runner.resolve()), "--loading", str(args.original.parent)], text=True)
    lines = output.splitlines()
    if len(lines) != 42:
        raise AssertionError(f"Expected 42 native loading screens, found {len(lines)}")
    failures = []
    for index, line in enumerate(lines):
        language, track = divmod(index, 7)
        words = line.split()
        if words[:2] != [str(language), str(track)] or len(words) != 4 + len(PERCENTAGES):
            raise AssertionError(f"Malformed native loading result: {line}")
        expected = original.screen(language, track, args.original.parent)
        if words[2:] != expected:
            failures.append({"language": language, "track": LEVELS[track], "native": words[2:], "original": expected})
    args.report.write_text(json.dumps({"original_sha256": ORIGINAL_SHA256, "screens": 42,
                                      "progress_frames": 42 * len(PERCENTAGES), "failures": failures}, indent=2) + "\n")
    print(f"loading original: 42 screens, {42 * len(PERCENTAGES)} progress frames, {len(failures)} failures")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
