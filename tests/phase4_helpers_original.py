#!/usr/bin/env python3
"""Compare native message/PFM/layout/rebuild helpers with original LE instructions under Unicorn 2.1.4.

Also observe original menubox/font/sprite/shared-cleanup release inventories. External allocator, sprite,
stack-probe, CD and logger boundaries are intercepted explicitly; no claim is made about DOS devices.
Run: .venv/bin/python tests/phase4_helpers_original.py --native build/phase4_helpers_test
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess

from hud_original import ESP, WORK, Original


def signed(value):
    return value - 0x100000000 if value & 0x80000000 else value


def hash_bytes(data):
    value = 1469598103934665603
    for b in data:
        value = ((value ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


class Helpers(Original):
    def __init__(self, executable):
        super().__init__(executable)
        import unicorn
        self.observations = []
        self.sprite_descriptors = []
        self.freed = []
        self.released = []
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self.boundary)

    def u32(self, address):
        return struct.unpack("<I", self.uc.mem_read(address, 4))[0]

    def put32(self, address, value):
        self.uc.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def boundary(self, uc, address, _size, _user):
        boundaries = {0x7708C, 0x55970, 0x5629E, 0x55644, 0x64391, 0x644C5, 0x63C74, 0x5499C, 0x20D14, 0x651C8}
        if address not in boundaries:
            return
        eax = uc.reg_read(self.reg.UC_X86_REG_EAX)
        edx = uc.reg_read(self.reg.UC_X86_REG_EDX)
        self.observations.append(address)
        if address == 0x55970:
            if eax:
                words = struct.unpack("<8I", uc.mem_read(eax, 32))
                self.sprite_descriptors.append((words[4], words[1], words[2], words[5]))
            else:
                self.released.append(edx)
            uc.reg_write(self.reg.UC_X86_REG_EAX, 0x10000 + len(self.sprite_descriptors))
        elif address == 0x5629E:
            self.freed.append(eax)
        sp = uc.reg_read(self.reg.UC_X86_REG_ESP)
        uc.reg_write(self.reg.UC_X86_REG_EIP, self.u32(sp))
        uc.reg_write(self.reg.UC_X86_REG_ESP, sp + (8 if address == 0x7708C else 4))

    def message(self, mask):
        durations = [(-i - 1 if i & 1 else i + 1) if mask & (1 << i) else -0.0 for i in range(10)]
        if mask == 1023:
            durations[0] = float("nan")
        self.uc.mem_write(0xEA77C, struct.pack("<10d", *durations))
        self.uc.mem_write(0xEA7CC, b"".join(b"occupied" + bytes(248) for _ in range(10)))
        self.uc.mem_write(WORK, b"new\0")
        self.uc.mem_write(ESP + 4, struct.pack("<d", 3.25))
        self.call(0x15254, eax=WORK)
        return hash_bytes(self.uc.mem_read(0xEA77C, 80 + 2560))

    def sprite_vectors(self, layout):
        # Map original picture pointers to symbolic banks; sprite creation is intercepted before dereference.
        pointers = ((0x1FEF74, 0), (0x1FEF68, 1), (0x1FEF70, 2), (0x1FEF6C, 3),
                    (0x1FEF3C, 4), (0x1FEF34, 4), (0x1FEF30, 4))
        for address, bank in pointers:
            self.put32(address, 0x10000000 + bank * 0x100000)
        self.put32(0x1FEEAC, layout)
        self.sprite_descriptors = []
        self.call(0x28A84)
        result = []
        for pixels, w, h, stride in self.sprite_descriptors:
            bank, offset = divmod(pixels - 0x10000000 - 0x34E, 0x100000)
            result.append((bank, offset, w, h, stride))
        return result

    def releases(self):
        # 0x542ac frees tail-first, unlike the native stack-safe iterative equivalent.
        nodes = [WORK + i * 32 for i in range(4)]
        for i, node in enumerate(nodes):
            self.put32(node + 0x14, nodes[i + 1] if i + 1 < len(nodes) else 0)
        self.freed = []
        self.call(0x542AC, eax=nodes[0])
        assert self.freed == nodes[::-1]
        self.call(0x542AC, eax=0)
        assert self.freed == nodes[::-1]

        self.put32(0xD7C60, 0)
        self.call(0x61319)
        assert self.uc.reg_read(self.reg.UC_X86_REG_EAX) == 0x3FC
        self.uc.mem_write(0x24C214, bytes(30 * 0x63E))
        expected = []
        for slot in (0, 7, 29):
            base = 0x24C214 + slot * 0x63E
            self.put32(base, 1)
            for glyph in (0, 83, 223):
                self.uc.mem_write(base + 0x1E + glyph, b"\x01")
                handle = 1000 + slot * 224 + glyph
                self.put32(base + 0xFE + glyph * 4, handle)
                expected.append(handle)
        self.put32(0xD7C60, 1)
        self.released = []
        self.observations = []
        self.call(0x61319)
        assert self.released == expected
        assert self.observations.count(0x55644) == 1
        assert self.u32(0xD7C60) == 0 and self.uc.reg_read(self.reg.UC_X86_REG_EAX) == 1
        assert all(self.u32(0x24C214 + slot * 0x63E) == 0 for slot in range(30))

        groups = [(0x1FE9C8, 5), (0x1FE9DC, 9), (0x1FEA00, 3), (0x1FEA3C, 4), (0x1FEA4C, 16),
                  (0x1FEADC, 6), (0x1FEAF4, 12), (0x1FEB24, 10), (0x1FEB4C, 12), (0x1FEBD4, 2),
                  (0x1FEBDC, 1), (0x1FEB7C, 1), (0x1FEB80, 3), (0x1FEBD0, 1), (0x1FEBA0, 12)]
        expected = [base + 4 * i for base, count in groups for i in range(count)]
        for address in expected:
            self.put32(address, address)
        self.released = []
        self.call(0x2B51C)
        assert self.released == expected

        for address in [0x1AC478 + 4 * i for i in range(4)] + [0x1FED98]:
            self.put32(address, address)
        for count in (0, 5):
            self.put32(0xC0B1C, count)
            self.put32(0xC0B18, 180)
            self.released = []
            self.observations = []
            self.call(0x215C8)
            assert self.uc.reg_read(self.reg.UC_X86_REG_EAX) == 0
            assert self.u32(0xC0B1C) == max(count, 1)
            assert self.released == [0x1AC478 + 4 * i for i in range(4)] + [0x1FED98]
            assert self.observations == [0x64391, 0x644C5, 0x63C74, 0x5499C] + [0x55970] * 5 + [0x20D14, 0x651C8]
        return {"menubox_nodes": 4, "font_slots": 30, "occupied_fonts": 3, "glyph_releases": 9,
                "race_sprite_releases": len(expected), "shared_sprite_releases": 5}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, default=Path("build/phase4_helpers_test"))
    parser.add_argument("--executable", type=Path,
                        default=Path("/Users/Shared/Epic Games/Ignition/ignition/MAINDOS.EXE"))
    args = parser.parse_args()
    original = Helpers(args.executable)
    # Execute the real runtime table stores, excluding the preceding allocator free.
    original.run(0x249F1, 0x24A9D, edi=1, edx=2)
    sprites = {layout: original.sprite_vectors(layout) for layout in range(3)}
    counts = {"workspace": 0, "layout": 0, "message": 0, "sprite": 0}
    for line in subprocess.check_output([str(args.native.resolve()), "--vectors"], text=True).splitlines():
        words = line.split()
        kind = words[0]
        counts[kind] += 1
        if kind == "workspace":
            length, maximum, a, b = map(int, words[1:])
            original.uc.mem_write(WORK, b"li00" + struct.pack("<I", length))
            original.put32(0x2087C8, maximum)
            original.put32(0x2087D8, maximum)
            original.call(0x54BB8, eax=WORK)
            original.call(0x54BD8, eax=WORK)
            assert (a, b) == (signed(original.u32(0x2087C8)), signed(original.u32(0x2087D8))), line
        elif kind == "layout":
            mode, view, split, layout = map(int, words[1:])
            for address, value in ((0x1FEFCC, mode), (0x1FEFDC, view), (0x1FEF14, split)):
                original.put32(address, value)
            original.call(0x28A14)
            assert original.u32(0x1FEEAC) == layout, line
            assert original.u32(0x1FEEB0) == 6 + bool(mode), line
            assert original.u32(0x1FEF1C) == 0xC3CE8 + 32 * layout, line
        elif kind == "message":
            assert original.message(int(words[1])) == words[2], line
        elif kind == "sprite":
            layout, index, *desc = map(int, words[1:])
            assert tuple(desc) == sprites[layout][index], (line, sprites[layout][index])
    assert counts == {"workspace": 36, "layout": 24, "message": 95, "sprite": 111}, counts
    print(json.dumps({"native_original_vectors": counts, "original_release_observations": original.releases(),
                      "sha256": "179c654ba65281f08bbcda2b5943ab9f35c309ec718600f81104644d22db948c"}))


if __name__ == "__main__":
    main()
