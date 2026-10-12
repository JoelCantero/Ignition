#!/usr/bin/env python3
"""Recursive-descent disassembler for MAINDOS.EXE (LE) using Capstone.

  disasm.py funcs [--strings]          list discovered functions
  disasm.py coverage                   report function states and code coverage
  disasm.py dis ADDR                   disassemble one function
  disasm.py all                        disassemble every function (grep the output instead of looping `dis`)
  disasm.py xref TEXT                  functions referencing strings containing TEXT
  disasm.py callers ADDR               functions that call ADDR
  disasm.py refs ADDR                  instructions referencing address ADDR
  disasm.py mem ADDR [-f d|i|I|h|p|s] [-n N]   read N values (double, int32, uint32, int16, pointer, string)

Functions are discovered from the entry point, code pointers stored in data (LE fixups), code addresses used as
push/mov immediates, and Watcom switch jump tables.
"""
import argparse
import os
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

sys.path.insert(0, os.path.dirname(__file__))
from le2elf import LE, convert  # noqa: E402

DEFAULT_EXE = "/Users/Shared/Epic Games/Ignition/ignition/MAINDOS.EXE"
SYMBOLS = os.path.join(os.path.dirname(__file__), "symbols.txt")
END_MNEMONICS = {"ret", "retf", "iret", "iretd", "jmp", "hlt"}


FUNCTION_STATUSES = {"unknown", "identified", "ported", "verified"}


def load_symbols():
    names = {}
    statuses = {}
    with open(SYMBOLS) as f:
        for line in f:
            parts = line.split()
            if parts and not line.lstrip().startswith("#"):
                addr = int(parts[0], 16)
                names[addr] = parts[1]
                for field in parts[2:]:
                    if field.startswith("status="):
                        status = field.partition("=")[2]
                        if status not in FUNCTION_STATUSES:
                            raise ValueError("invalid function status %r at 0x%x" % (status, addr))
                        statuses[addr] = status
    return names, statuses


NAMES, STATUSES = load_symbols()


def name(addr):
    return NAMES.get(addr, "sub_%08x" % addr)


def function_status(addr):
    return STATUSES.get(addr, "identified" if addr in NAMES else "unknown")


class Image:
    def __init__(self, exe):
        with open(exe, "rb") as f:
            le = LE(f.read())
        images, _ = convert(le)
        self.objs = [(o["base"], bytes(images[o["index"]]), o["flags"]) for o in le.objects]
        self.entry = le.objects[le.eip_obj - 1]["base"] + le.eip
        bases = {o["index"]: o["base"] for o in le.objects}
        self.fixups = {}  # 32-bit offset fixups: source address -> target address
        for o in le.objects:
            for k in range(o["page_count"]):
                for stype, so, tobj, toff in le.fixups_for_page(o["page_index"] + k):
                    if stype == 0x07:
                        self.fixups[o["base"] + k * le.page_size + so] = bases[tobj] + toff
        self.fixup_targets = set(self.fixups.values())
        self.md = Cs(CS_ARCH_X86, CS_MODE_32)
        self.md.detail = True

    def find(self, addr):
        for base, data, flags in self.objs:
            if base <= addr < base + len(data):
                return base, data, flags
        return None

    def is_code(self, addr):
        o = self.find(addr)
        return o is not None and bool(o[2] & 4)

    def string_at(self, addr, minlen=4):
        o = self.find(addr)
        if not o:
            return None
        base, data, flags = o
        if flags & 4:
            return None
        i = addr - base
        end = data.find(b"\0", i, i + 256)
        if end - i < minlen:
            return None
        s = data[i:end]
        if all(32 <= c < 127 or c in (9, 10, 13) for c in s):
            return s.decode("ascii")
        return None

    def insn(self, addr):
        base, data, _ = self.find(addr)
        off = addr - base
        return next(self.md.disasm(data[off:off + 16], addr, 1), None)


def operand_values(ins):
    for op in ins.operands:
        if op.type == X86_OP_IMM:
            yield op.imm & 0xFFFFFFFF
        elif op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.disp:
            yield op.mem.disp & 0xFFFFFFFF


def branch_target(ins):
    if ins.operands and ins.operands[0].type == X86_OP_IMM:
        return ins.operands[0].imm & 0xFFFFFFFF
    return None


def jump_table(img, ins):
    """Targets of a Watcom switch `jmp [reg*4 + table]` (table entries are fixups)."""
    if not ins.operands or ins.operands[0].type != X86_OP_MEM:
        return []
    mem = ins.operands[0].mem
    if mem.base != 0 or mem.index == 0 or mem.scale != 4:
        return []
    out, a = [], mem.disp & 0xFFFFFFFF
    while a in img.fixups and img.is_code(img.fixups[a]):
        out.append(img.fixups[a])
        a += 4
    return out


def analyze(img, pointer_seeds=True):
    """Return {func_addr: {"insns": {addr: ins}, "calls": set, "refs": set}}.

    Seeds: entry point, code addresses stored in data (function pointer tables) and code addresses used as
    immediates (callbacks). Jump-table targets stay inside their function.
    """
    funcs = {}
    pending = [img.entry]
    if pointer_seeds:
        pending += sorted({t for s, t in img.fixups.items() if img.is_code(t) and not img.is_code(s)})
    case_labels = set()
    while pending:
        start = pending.pop()
        if start in funcs or start in case_labels or not img.is_code(start):
            continue
        f = funcs[start] = {"insns": {}, "calls": set(), "refs": set()}
        blocks = [start]
        while blocks:
            a = blocks.pop()
            while a not in f["insns"] and img.is_code(a):
                ins = img.insn(a)
                if ins is None:
                    break
                f["insns"][a] = ins
                m = ins.mnemonic
                for v in operand_values(ins):
                    f["refs"].add(v)
                    if pointer_seeds and m in ("push", "mov") and img.is_code(v) and v in img.fixup_targets:
                        pending.append(v)
                if m == "call":
                    t = branch_target(ins)
                    if t is not None:
                        f["calls"].add(t)
                        pending.append(t)
                elif m.startswith("j"):
                    t = branch_target(ins)
                    if t is not None:
                        blocks.append(t)
                    elif m == "jmp":
                        targets = jump_table(img, ins)
                        case_labels.update(targets)
                        blocks.extend(targets)
                if m in END_MNEMONICS:
                    break
                a += ins.size
    for c in case_labels:
        funcs.pop(c, None)
    return funcs


def func_strings(img, f):
    out = []
    for r in sorted(f["refs"]):
        s = img.string_at(r)
        if s:
            out.append((r, s))
    return out


def owner(funcs, addr):
    for fa, f in funcs.items():
        if addr in f["insns"]:
            return fa
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=DEFAULT_EXE)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("funcs")
    p.add_argument("--strings", action="store_true")
    sub.add_parser("coverage")
    sub.add_parser("dis").add_argument("addr")
    sub.add_parser("all")
    sub.add_parser("xref").add_argument("text")
    sub.add_parser("callers").add_argument("addr")
    sub.add_parser("refs").add_argument("addr")
    p = sub.add_parser("mem")
    p.add_argument("addr")
    p.add_argument("-f", "--fmt", default="d", choices=list("diIhps"))
    p.add_argument("-n", "--count", type=int, default=1)
    args = ap.parse_args()

    img = Image(args.exe)
    if args.cmd == "mem":
        a = int(args.addr, 16)
        for k in range(args.count):
            o = img.find(a)
            if o is None:
                sys.exit("0x%x is outside the image" % a)
            base, data, _ = o
            if args.fmt == "s":
                s = img.string_at(a, 1)
                print("%08x  %r" % (a, s))
                a += len(s or "") + 1
                continue
            code = {"d": "<d", "i": "<i", "I": "<I", "h": "<h", "p": "<I"}[args.fmt]
            v = struct.unpack_from(code, data, a - base)[0]
            note = ""
            if args.fmt == "p":
                s = img.string_at(v)
                note = "  ; %r" % s if s else "  ; %s" % name(v) if img.is_code(v) else ""
                v = "0x%08x" % v
            print("%08x  %s%s" % (a, v, note))
            a += struct.calcsize(code)
        return
    funcs = analyze(img)

    if args.cmd == "coverage":
        code_objects = [(base, data) for base, data, flags in img.objs if flags & 4]
        covered = {base: bytearray(len(data)) for base, data in code_objects}
        unique_insns = {}
        for f in funcs.values():
            for addr, ins in f["insns"].items():
                unique_insns.setdefault(addr, ins)
                for base, data in code_objects:
                    off = addr - base
                    if 0 <= off < len(data):
                        covered[base][off:min(off + ins.size, len(data))] = b"\1" * min(
                            ins.size, len(data) - off
                        )
                        break

        total_bytes = sum(len(data) for _, data in code_objects)
        covered_bytes = sum(sum(bits) for bits in covered.values())
        totals = {status: 0 for status in FUNCTION_STATUSES}
        for fa in funcs:
            totals[function_status(fa)] += 1

        print("functions: %d (%s)" % (
            len(funcs),
            ", ".join("%s=%d" % (s, totals[s]) for s in
                      ("unknown", "identified", "ported", "verified")),
        ))
        print("unique instructions: %d" % len(unique_insns))
        print("code bytes covered: %d/%d (%.1f%%)" % (
            covered_bytes, total_bytes, 100 * covered_bytes / total_bytes if total_bytes else 0,
        ))
        print("address  status      instructions  function")
        for fa in sorted(funcs):
            print("%08x  %-10s  %-12d  %s" % (
                fa, function_status(fa), len(funcs[fa]["insns"]), name(fa),
            ))
    elif args.cmd == "funcs":
        print("; entry 0x%08x, %d functions" % (img.entry, len(funcs)))
        for fa in sorted(funcs):
            f = funcs[fa]
            print("%08x %-20s insns=%-5d calls=%d" % (fa, name(fa), len(f["insns"]), len(f["calls"])))
            if args.strings:
                for r, s in func_strings(img, f):
                    print("    0x%08x %r" % (r, s[:80]))
    elif args.cmd in ("dis", "all"):
        if args.cmd == "all":
            targets = sorted(funcs)
        else:
            a = int(args.addr, 16)
            fa = a if a in funcs else owner(funcs, a)
            if fa is None:
                sys.exit("no function at 0x%x" % a)
            targets = [fa]
        for fa in targets:
            if args.cmd == "all":
                print("; ---- %s" % name(fa))
            for ia, ins in sorted(funcs[fa]["insns"].items()):
                note = ""
                for v in operand_values(ins):
                    s = img.string_at(v)
                    if s:
                        note = "  ; %r" % s[:60]
                    elif v in funcs or v in NAMES:
                        note = "  ; %s" % name(v)
                print("%08x  %-8s %s%s" % (ia, ins.mnemonic, ins.op_str, note))
    elif args.cmd == "xref":
        for fa in sorted(funcs):
            hits = [(r, s) for r, s in func_strings(img, funcs[fa]) if args.text.lower() in s.lower()]
            for r, s in hits:
                print("%s  0x%08x %r" % (name(fa), r, s))
    elif args.cmd == "callers":
        t = int(args.addr, 16)
        for fa in sorted(funcs):
            if t in funcs[fa]["calls"]:
                print(name(fa))
    elif args.cmd == "refs":
        t = int(args.addr, 16)
        for fa in sorted(funcs):
            for ia, ins in sorted(funcs[fa]["insns"].items()):
                if t in operand_values(ins):
                    print("%s  %08x  %s %s" % (name(fa), ia, ins.mnemonic, ins.op_str))


if __name__ == "__main__":
    main()
