#!/usr/bin/env python3
"""IGN_DOS.BTZ default image against the original menu code (optional, needs Unicorn).

Runs the unpatched bytes of sub_0001a864 (menu defaults) on a poisoned copy of the menu buffer 0xe9bfc, then
applies what menu_load_gfx does next on a fresh start: the option widget values (+0x14 of the widgets listed at
0x125ae..0x126b8, copied to +0x507..+0x567 by 0x126c5) and the factory records 0xbf628 (0x126dc). Bytes the
original never writes are BSS (zero after _cstart_). The result must equal `save_btz_test --dump-default`.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from physics_original import ORIGINAL_SHA256  # noqa: E402

BUF, SIZE, POISON = 0xE9BFC, 0x597, 0xEE
STACK, RETURN = 0x3F0000, 0x3FFFF0
# menu_load_gfx 0x125ae..0x126b8: widget pointers stored at 0xea47c + 4 * k (k = 0..24), in source order.
WIDGET_SETUP = (0x125AE, 0x126BE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--original", required=True)
    ap.add_argument("--runner", required=True, help="save_btz_test")
    a = ap.parse_args()
    exe = Path(a.original).read_bytes()
    if hashlib.sha256(exe).hexdigest() != ORIGINAL_SHA256:
        print("unsupported MAINDOS.EXE (sha256 mismatch)", file=sys.stderr)
        return 1
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import UC_X86_REG_ESP
    import disasm

    img = disasm.Image(a.original)

    def read(addr, n):
        base, data, _ = img.find(addr)
        return bytes(data[addr - base:addr - base + n])

    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0, 0x400000)
    for base, data, _ in img.objs:
        uc.mem_write(base, data)
    uc.mem_write(BUF, bytes([POISON]) * SIZE)
    outside = []

    def on_write(_uc, _access, address, size, _value, _data):
        if not (BUF <= address and address + size <= BUF + SIZE):
            outside.append(address)

    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    uc.mem_write(STACK, struct.pack("<I", RETURN))
    uc.reg_write(UC_X86_REG_ESP, STACK)
    uc.emu_start(0x1A864, RETURN, count=10000)
    image = bytearray(b if b != POISON else 0 for b in uc.mem_read(BUF, SIZE))
    if [x for x in outside if not STACK - 64 <= x < STACK + 4]:
        print("sub_0001a864 wrote outside the BTZ buffer", file=sys.stderr)
        return 1

    # Widget pointer table: reproduce the register moves of 0x125ae..0x126b8 from the disassembly.
    regs, table = {}, {}
    for ins in img.md.disasm(read(WIDGET_SETUP[0], WIDGET_SETUP[1] - WIDGET_SETUP[0]), WIDGET_SETUP[0]):
        m = re.fullmatch(r"(e[a-z]{2}), (0x[0-9a-f]+)", ins.op_str)
        if ins.mnemonic == "mov" and m:
            regs[m.group(1)] = int(m.group(2), 16)
        m = re.fullmatch(r"dword ptr \[(0x[0-9a-f]+)\], (e[a-z]{2})", ins.op_str)
        if ins.mnemonic == "mov" and m:
            table[int(m.group(1), 16)] = regs[m.group(2)]
    for k in range(25):
        image[0x507 + 4 * k:0x50B + 4 * k] = read(table[0xEA47C + 4 * k] + 0x14, 4)
    image[0x1BF:0x1BF + 7 * 0x78] = read(0xBF628, 7 * 0x78)

    port = subprocess.run([a.runner, "-", "-", "--dump-default"], capture_output=True, check=True).stdout
    diff = [i for i in range(SIZE) if i >= len(port) or port[i] != image[i]]
    if diff:
        print("default image differs at %s" % ", ".join("+0x%03x" % i for i in diff[:16]), file=sys.stderr)
        return 1
    print("btz_defaults_original: %d bytes identical to sub_0001a864 + widgets + 0xbf628" % SIZE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
