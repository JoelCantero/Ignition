#!/usr/bin/env python3
"""Recursive-descent disassembler for MAINDOS.EXE (LE) using Capstone.

  disasm.py funcs [--strings]          list discovered functions
  disasm.py coverage                   report function states (incl. auto-detected library) and coverage
  disasm.py roadmap [-o FILE]           export every discovered function and its roadmap disposition as TSV
  disasm.py catalog [--refresh]         verify catalog against the executable, or refresh measured coverage
  disasm.py dis ADDR                   disassemble one function
  disasm.py sweep                      linear sweep of uncovered code; writes tools/gap_seeds.txt
  disasm.py export ghidra|ida [-o F]   Ghidra/IDA script with function names, comments and structures
  disasm.py all                        disassemble every function (grep the output instead of looping `dis`)
  disasm.py xref TEXT                  functions referencing strings containing TEXT
  disasm.py callers ADDR               functions that call ADDR
  disasm.py refs ADDR                  instructions referencing address ADDR
  disasm.py mem ADDR [-f d|i|I|h|p|s] [-n N]   read N values (double, int32, uint32, int16, pointer, string)

Functions are discovered from the entry point, code pointers stored in data (LE fixups), code addresses used as
push/mov immediates, and Watcom switch jump tables.
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

sys.path.insert(0, os.path.dirname(__file__))
from le2elf import LE, convert  # noqa: E402
from function_catalog import (CATALOG as CATALOG_PATH, COVERAGE_FIELDS, FUNCTION_STATUSES,
                              STATUS_ORDER, inventory_fingerprint, load_catalog, validate_catalog)

DEFAULT_EXE = "/Users/Shared/Epic Games/Ignition/ignition/MAINDOS.EXE"
GAP_SEEDS = os.path.join(os.path.dirname(__file__), "gap_seeds.txt")
END_MNEMONICS = {"ret", "retf", "iret", "iretd", "jmp", "hlt"}


CATALOG = load_catalog(verify_snapshot=False)


def load_symbols():
    names = {}
    statuses = {}
    roadmaps = {}
    for symbol in CATALOG["symbols"]:
        addr = int(symbol["address"], 16)
        names[addr] = symbol["name"]
        if symbol["kind"] == "function":
            statuses[addr] = symbol["status"]
            if symbol["roadmap"] != "unmapped":
                roadmaps[addr] = symbol["roadmap"]
    return names, statuses, roadmaps


NAMES, STATUSES, ROADMAPS = load_symbols()


def load_gap_seeds():
    if not os.path.exists(GAP_SEEDS):
        return []
    with open(GAP_SEEDS) as f:
        return [int(line, 16) for line in f if line.strip() and not line.startswith("#")]


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


def analyze(img, pointer_seeds=True, extra_seeds=(), follow_calls=True):
    """Return {func_addr: {"insns": {addr: ins}, "calls": set, "refs": set}}.

    Seeds: entry point, code addresses stored in data (function pointer tables) and code addresses used as
    immediates (callbacks). Jump-table targets stay inside their function.
    """
    funcs = {}
    pending = [img.entry] + list(extra_seeds)
    if pointer_seeds:
        pending += sorted({t for s, t in img.fixups.items() if img.is_code(t) and not img.is_code(s)})
    case_labels = set()
    while pending:
        start = pending.pop()
        if start in funcs or start in case_labels or not img.is_code(start):
            continue
        f = funcs[start] = {"insns": {}, "calls": set(), "refs": set(), "bad": 0}
        blocks = [start]
        while blocks:
            a = blocks.pop()
            while a not in f["insns"] and img.is_code(a):
                ins = img.insn(a)
                if ins is None:
                    f["bad"] += 1
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
                        if follow_calls:
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


# Watcom/DOS4G runtime evidence (no FLIRT database exists for Watcom 10.x, so the library is recognised
# from the call graph and the data it touches).
LIB_ANCHORS = (0x55f9c, 0x62775, 0x64774, 0x647ef, 0x64c2a, 0x65367, 0x652fa, 0x653d3, 0x65645, 0x65682, 0x6577d, 0x65782)
LIB_DATA = ((0xd7d00, 0xd8100), (0xe6000, 0xe7400), (0x20a000, 0x20b000), (0x25a000, 0x26f640))
LIB_MIN_ADDR = 0x60000


def classify_library(img, funcs):
    """Functions of the C runtime / DOS4G: callees of known runtime anchors, plus functions at or above
    LIB_MIN_ADDR that only touch runtime data, reference no string and call only runtime functions."""

    def in_lib_data(v):
        return any(a <= v < b for a, b in LIB_DATA)

    lib = {a for a in LIB_ANCHORS if a in funcs}
    changed = True
    while changed:
        changed = False
        for a in list(lib):
            for c in funcs[a]["calls"]:
                if c in funcs and c not in lib:
                    lib.add(c)
                    changed = True
        for a, f in funcs.items():
            if a in lib or a < LIB_MIN_ADDR or a == img.entry:
                continue
            data = [v for v in f["refs"] if 0x90000 <= v < 0x300000]
            if (all(in_lib_data(v) for v in data) and not any(img.string_at(v) for v in f["refs"])
                    and all(c in lib or c not in funcs for c in f["calls"])):
                lib.add(a)
                changed = True
    return lib


PADDING = (b"\x8d\x40\x00", b"\x8d\x49\x00", b"\x8b\xc0", b"\x8b\xc9", b"\x90", b"\xcc", b"\x00")
PROLOGUE = re.compile(rb"(?:[\x50-\x57]){1,6}(?:\x83\xec.|\x81\xec....|\x8b\xec|[\x89\x8b][\xc0-\xff])", re.S)


def code_gaps(img, funcs):
    base, data, _ = img.objs[0]
    cov = bytearray(len(data))
    for f in funcs.values():
        for a, i in f["insns"].items():
            cov[a - base:a - base + i.size] = b"\1" * i.size
    gaps, i = [], 0
    while i < len(cov):
        if cov[i]:
            i += 1
            continue
        j = i
        while j < len(cov) and not cov[j]:
            j += 1
        gaps.append((base + i, j - i))
        i = j
    return gaps


def skip_padding(data, base, a, end):
    while a < end:
        for pad in PADDING:
            if data[a - base:a - base + len(pad)] == pad:
                a += len(pad)
                break
        else:
            return a
    return a


def is_table(img, a, n):
    """A run of 32-bit relocated code pointers (switch jump table) or ASCII text."""
    if n >= 4 and all((a + k) in img.fixups and img.is_code(img.fixups[a + k]) for k in range(0, n - n % 4, 4)):
        return "jumptable"
    return None


IMPLAUSIBLE = {"aaa", "aas", "daa", "das", "aam", "aad", "bound", "arpl", "into", "les", "lds", "enter", "salc"}


def plausible(f):
    """Reject data that happens to decode: BCD/segment-load opcodes or runs of `add [eax], al`."""
    zero_run = 0
    for _, ins in sorted(f["insns"].items()):
        if ins.mnemonic in IMPLAUSIBLE:
            return False
        zero_run = zero_run + 1 if ins.bytes == b"\0\0" else 0
        if zero_run >= 2:
            return False
    return True


def relocations_valid(img, f):
    """Require every code relocation overlapping a decoded instruction to occupy its encoded operand."""
    for a, ins in f["insns"].items():
        disp_start = ins.disp_offset
        disp_end = disp_start + ins.disp_size
        imm_start = ins.imm_offset
        imm_end = imm_start + ins.imm_size
        for site in range(a, a + ins.size):
            if site not in img.fixups:
                continue
            offset = site - a
            if not (disp_start <= offset < disp_end or imm_start <= offset < imm_end):
                return False
    return True


def sweep_gaps(img, funcs):
    """Linear sweep of the uncovered code bytes: validate Watcom function candidates at gap starts (after
    padding) and at prologue patterns, iterating until no new clean function appears. Candidates must not
    overlap an accepted instruction and every LE relocation in their instructions must align to an operand."""
    base, data, _ = img.objs[0]
    added = set()
    for _round in range(12):
        cands = set()
        for a, n in code_gaps(img, funcs):
            end = a + n
            s = skip_padding(data, base, a, end)
            if s < end and not is_table(img, s, end - s):
                cands.add(s)
            for m in PROLOGUE.finditer(data, s - base, end - base):
                cands.add(base + m.start())
        cands -= set(funcs)
        trial = analyze(img, extra_seeds=sorted(cands))
        new = []
        claimed = funcs_insn_set(funcs)
        for c in sorted(cands):
            f = trial.get(c)
            last = max(f["insns"], default=0) if f else 0
            ends = f and f["insns"] and f["insns"][last].mnemonic in END_MNEMONICS
            overlaps = f and any(a in claimed for a in f["insns"])
            if (f and not f["bad"] and ends and len(f["insns"]) >= 3 and not overlaps
                    and plausible(f) and relocations_valid(img, f)):
                new.append(c)
                claimed.update(f["insns"])
        if not new:
            break
        for c in new:
            funcs[c] = trial[c]
            added.add(c)
        # pull in the callees of the accepted functions
        more = analyze(img, extra_seeds=sorted(funcs))
        claimed = funcs_insn_set(funcs)
        for a, f in more.items():
            if (a not in funcs and not f["bad"] and not any(i in claimed for i in f["insns"])
                    and relocations_valid(img, f)):
                funcs[a] = f
                claimed.update(f["insns"])
    return added


_INSN_CACHE = {}


def funcs_insn_set(funcs):
    key = id(funcs), len(funcs)
    if key not in _INSN_CACHE:
        _INSN_CACHE.clear()
        _INSN_CACHE[key] = {a for f in funcs.values() for a in f["insns"]}
    return _INSN_CACHE[key]


STRUCTS = os.path.join(os.path.dirname(__file__), "structs.txt")


def load_symbol_notes():
    """Names and comments from the catalog, including data, labels and all historical notes."""
    out = {}
    for symbol in CATALOG["symbols"]:
        notes = list(symbol["notes"])
        for field in ("status", "roadmap"):
            if field in symbol:
                notes.append(f"{field}={symbol[field]}")
        if "classification" in symbol:
            classification = symbol["classification"]
            notes.append("area confidence=" + classification["confidence"])
            notes.append(classification["rationale"])
        if symbol["aliases"]:
            notes.append("aliases: " + ", ".join(symbol["aliases"]))
        for review in symbol["review"]:
            notes.append(f"REVIEW {review['field']}: {review['legacy_value']} ({review['reason']})")
        out[int(symbol["address"], 16)] = (symbol["name"], "\n".join(notes))
    return out


def load_structs():
    structs, cur = [], None
    with open(STRUCTS) as f:
        for line in f:
            line = line.split("#", 1)[0].rstrip()
            parts = line.split(None, 3)
            if not parts:
                continue
            if parts[0] == "struct":
                cur = {"name": parts[1], "size": int(parts[2], 16), "fields": []}
                structs.append(cur)
            else:
                cur["fields"].append((int(parts[0], 16), parts[1], parts[2], parts[3] if len(parts) > 3 else ""))
    return structs


def export_script(kind, funcs):
    """Ghidra (Jython) or IDAPython script: function starts, symbol names/comments and structures."""
    notes = load_symbol_notes()
    sizes = {"double": 8, "int": 4, "uint": 4, "short": 2, "byte": 1, "ptr": 4}
    out = ["# generated by tools/disasm.py export %s -- addresses are the LE linear addresses of MAINDOS.EXE" % kind,
           "# set BASE if the loader relocated the image", "BASE = 0"]
    if kind == "ghidra":
        out += ["from ghidra.program.model.data import StructureDataType, DoubleDataType, IntegerDataType, UnsignedIntegerDataType, ShortDataType, ByteDataType, PointerDataType",
                "from ghidra.program.model.symbol import SourceType",
                "def A(a): return toAddr(BASE + a)",
                "T = {'double': DoubleDataType.dataType, 'int': IntegerDataType.dataType, 'uint': UnsignedIntegerDataType.dataType,",
                "     'short': ShortDataType.dataType, 'byte': ByteDataType.dataType, 'ptr': PointerDataType.dataType}",
                "dtm = currentProgram.getDataTypeManager()"]
    else:
        out += ["import idc, ida_name, ida_funcs, ida_bytes", "def A(a): return BASE + a"]
    for fa in sorted(funcs):
        nm, note = notes.get(fa, ("sub_%08x" % fa, "%s%d instructions" % (
            "library, " if function_status(fa) == "library" else "", len(funcs[fa]["insns"]))))
        if kind == "ghidra":
            out.append("createFunction(A(0x%x), %r)" % (fa, nm))
            out.append("setEOLComment(A(0x%x), %r)" % (fa, note)) if note else None
        else:
            out.append("ida_funcs.add_func(A(0x%x)); idc.set_name(A(0x%x), %r, ida_name.SN_NOWARN)" % (fa, fa, nm))
            out.append("idc.set_func_cmt(A(0x%x), %r, 1)" % (fa, note)) if note else None
    for a, (nm, note) in sorted(notes.items()):
        if a in funcs:
            continue
        if kind == "ghidra":
            out.append("createLabel(A(0x%x), %r, True)" % (a, nm))
            out.append("setEOLComment(A(0x%x), %r)" % (a, note)) if note else None
        else:
            out.append("idc.set_name(A(0x%x), %r, ida_name.SN_NOWARN)" % (a, nm))
            out.append("idc.set_cmt(A(0x%x), %r, 1)" % (a, note)) if note else None
    for st in load_structs():
        if kind == "ghidra":
            out.append("s = StructureDataType(%r, 0x%x)" % (st["name"], st["size"]))
            for off, ty, nm, cm in st["fields"]:
                out.append("s.replaceAtOffset(0x%x, T[%r], %d, %r, %r)" % (off, ty, sizes[ty], nm, cm))
            out.append("dtm.addDataType(s, None)")
        else:
            out.append("sid = idc.add_struc(-1, %r, 0)" % st["name"])
            fl = {"double": "idc.FF_QWORD", "int": "idc.FF_DWORD", "uint": "idc.FF_DWORD", "short": "idc.FF_WORD",
                  "byte": "idc.FF_BYTE", "ptr": "idc.FF_DWORD"}
            for off, ty, nm, cm in st["fields"]:
                out.append("idc.add_struc_member(sid, %r, 0x%x, %s | idc.FF_DATA, -1, %d)" % (nm, off, fl[ty], sizes[ty]))
    return "\n".join(l for l in out if l) + "\n"


def owner(funcs, addr):
    for fa, f in funcs.items():
        if addr in f["insns"]:
            return fa
    return None


def call_graph(img, funcs):
    """Static edges between discovered starts: direct `call imm` and function starts used as operands
    (callback/pointer registration). Indirect calls through registers or tables are not resolved; instead a
    start is an indirect root when it is referenced from outside the decoded instructions: a fixup in data or
    in an undecoded code table, or an E8/E9 rel32 in undecoded code bytes (conservative: may be noise)."""
    decoded = set()
    for f in funcs.values():
        for a, ins in f["insns"].items():
            decoded.update(range(a, a + ins.size))
    roots = {t for s, t in img.fixups.items() if t in funcs and s not in decoded}
    for base, data, flags in img.objs:
        if flags & 4:
            for i in range(len(data) - 4):
                if data[i] in (0xE8, 0xE9) and base + i not in decoded:
                    t = (base + i + 5 + struct.unpack_from("<i", data, i + 1)[0]) & 0xFFFFFFFF
                    if t in funcs:
                        roots.add(t)
    graph = {}
    for fa, f in funcs.items():
        calls = sorted(c for c in f["calls"] if c in funcs and c != fa)
        refs = sorted(r for r in f["refs"] if r in funcs and r != fa and r not in f["calls"])
        graph[fa] = (["%08x" % c for c in calls], ["%08x" % r for r in refs], fa in roots)
    return graph


def coverage_metrics(img, funcs):
    code_objects = [(base, data) for base, data, flags in img.objs if flags & 4]
    covered = {base: bytearray(len(data)) for base, data in code_objects}
    unique_insns = set()
    insns_by_status = {status: set() for status in FUNCTION_STATUSES}
    for fa, function in funcs.items():
        insns_by_status[function_status(fa)].update(function["insns"])
        for addr, ins in function["insns"].items():
            unique_insns.add(addr)
            for base, data in code_objects:
                off = addr - base
                if 0 <= off < len(data):
                    size = min(ins.size, len(data) - off)
                    covered[base][off:off + size] = b"\1" * size
                    break
    game_insns = unique_insns - insns_by_status["library"]
    return {
        "unique_instructions": len(unique_insns),
        "library_instructions": len(insns_by_status["library"]),
        "ported_instructions": len((insns_by_status["ported"] | insns_by_status["verified"]) & game_insns),
        "partial_instructions": len(insns_by_status["partial"]),
        "covered_code_bytes": sum(sum(bits) for bits in covered.values()),
        "code_bytes": sum(len(data) for _, data in code_objects),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=DEFAULT_EXE)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("funcs")
    p.add_argument("--strings", action="store_true")
    sub.add_parser("coverage")
    sub.add_parser("catalog").add_argument("--refresh", action="store_true",
                                         help="update measured counts/coverage, preserving names, states and notes")
    p = sub.add_parser("roadmap")
    p.add_argument("-o", "--output")
    sub.add_parser("dis").add_argument("addr")
    sub.add_parser("all")
    sub.add_parser("sweep")
    p = sub.add_parser("export")
    p.add_argument("kind", choices=["ghidra", "ida"])
    p.add_argument("-o", "--output")
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
    if args.cmd == "sweep":
        funcs = analyze(img, extra_seeds=[])
        original_starts = set(funcs)
        gap_added = sweep_gaps(img, funcs)
        added = gap_added | (set(funcs) - original_starts)
        with open(GAP_SEEDS, "w") as out:
            out.write("# validated function starts added by `disasm.py sweep` (do not recursively reseed their calls)\n")
            for a in sorted(added):
                out.write("%08x\n" % a)
        print("%d functions, %d gap candidates accepted, %d total new starts persisted, %d uncovered gaps left" % (
            len(funcs), len(gap_added), len(added), len(code_gaps(img, funcs))))
        return
    funcs = analyze(img)
    gap_seeds = load_gap_seeds()
    if gap_seeds:
        additions = analyze(img, pointer_seeds=False, extra_seeds=gap_seeds, follow_calls=False)
        for fa, f in additions.items():
            if fa not in funcs:
                funcs[fa] = f
    for fa in classify_library(img, funcs):
        STATUSES.setdefault(fa, "library")

    if args.cmd == "catalog":
        if hashlib.sha256(open(args.exe, "rb").read()).hexdigest() != CATALOG["executable"]["sha256"]:
            ap.error("Executable SHA-256 differs from docs/functions.json")
        entries = {int(s["address"], 16): s for s in CATALOG["symbols"] if s["kind"] == "function"}
        if set(entries) != set(funcs):
            ap.error("Discovered function addresses differ from the catalog; review additions/removals explicitly")
        counts = {fa: len(f["insns"]) for fa, f in funcs.items()}
        graph = call_graph(img, funcs)
        metrics = coverage_metrics(img, funcs)
        if args.refresh:
            CATALOG["executable"]["entry"] = "%08x" % img.entry
            for fa, entry in entries.items():
                entry["instructions"] = counts[fa]
                entry.pop("data_referenced", None)
                entry["calls"], entry["pointer_refs"], entry["indirect_root"] = graph[fa]
            CATALOG["coverage"] = {**metrics, "inventory_sha256": inventory_fingerprint(CATALOG)}
            validate_catalog(CATALOG)
            CATALOG_PATH.write_text(json.dumps(CATALOG, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            print(f"Updated {CATALOG_PATH}: {len(entries)} functions; names, states and notes preserved")
        else:
            validate_catalog(CATALOG)
            if any(entry["instructions"] != counts[fa] for fa, entry in entries.items()):
                ap.error("Catalog instruction counts differ; use catalog --refresh")
            if any((entry.get("calls"), entry.get("pointer_refs"), entry.get("indirect_root")) != graph[fa]
                   for fa, entry in entries.items()) or CATALOG["executable"].get("entry") != "%08x" % img.entry:
                ap.error("Catalog call graph differs; use catalog --refresh")
            if any(CATALOG["coverage"][key] != metrics[key] for key in COVERAGE_FIELDS):
                ap.error("Catalog unique coverage differs; use catalog --refresh")
            print(f"Catalog matches executable: {len(entries)} functions, all counts and unique coverage")
        return
    if args.cmd == "export":
        text = export_script(args.kind, funcs)
        if args.output:
            with open(args.output, "w") as out:
                out.write(text)
            print("wrote %s (%d functions)" % (args.output, len(funcs)))
        else:
            sys.stdout.write(text)
        return
    if args.cmd == "roadmap":
        rows = ["address\tfunction\tstatus\tinstructions\troadmap"]
        for fa in sorted(funcs):
            rows.append("%08x\t%s\t%s\t%d\t%s" % (
                fa, name(fa), function_status(fa), len(funcs[fa]["insns"]),
                ROADMAPS.get(fa, "unmapped"),
            ))
        text = "\n".join(rows) + "\n"
        if args.output:
            with open(args.output, "w") as out:
                out.write(text)
            print("wrote %s (%d functions)" % (args.output, len(funcs)))
        else:
            sys.stdout.write(text)
        return
    if args.cmd == "coverage":
        metrics = coverage_metrics(img, funcs)
        total_bytes = metrics["code_bytes"]
        covered_bytes = metrics["covered_code_bytes"]
        totals = {status: 0 for status in FUNCTION_STATUSES}
        for fa in funcs:
            status = function_status(fa)
            totals[status] += 1
        print("functions: %d (%s)" % (
            len(funcs),
            ", ".join("%s=%d" % (s, totals[s]) for s in
                      STATUS_ORDER),
        ))
        print("library (C runtime / DOS4G) unique instructions: %d, to exclude from game review" % metrics["library_instructions"])
        print("unique instructions: %d" % metrics["unique_instructions"])
        game_count = metrics["unique_instructions"] - metrics["library_instructions"]
        print("game instructions marked ported/verified: %d/%d (%.1f%%)" % (
            metrics["ported_instructions"], game_count,
            100 * metrics["ported_instructions"] / game_count if game_count else 0,
        ))
        print("game instructions marked partial: %d (excluded from ported/verified)" % metrics["partial_instructions"])
        mapped_game = sum(1 for fa in funcs
                          if fa in ROADMAPS and function_status(fa) != "library")
        game_functions = sum(1 for fa in funcs if function_status(fa) != "library")
        mapped_all = sum(1 for fa in funcs if fa in ROADMAPS)
        print("game functions linked to roadmap tasks: %d/%d (%.1f%%)" % (
            mapped_game, game_functions,
            100 * mapped_game / game_functions if game_functions else 0,
        ))
        print("all functions linked to roadmap tasks (including library): %d/%d" % (
            mapped_all, len(funcs),
        ))
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
