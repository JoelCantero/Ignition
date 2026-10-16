#!/usr/bin/env python3
"""Execute complete build_sections/load_tri/ai_find_section against all seven assets.

Only host allocation, file I/O, formatting, free and srand are intercepted. Geometry,
topology, selectors, pow/atan2 and __CHP execute relocated original instructions.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import unicorn

from physics_original import Original, ESP, STACK, RETURN, FPCW, ORIGINAL_SHA256

TRACKS = ("USA", "CANADA", "AUSTRIA", "BRAZIL", "CARIB", "ICELAND", "JAPAN")
# The runtime level ID is not the presentation order.
TRACK_IDS = dict(CANADA=0, USA=1, CARIB=2, BRAZIL=3, AUSTRIA=4, ICELAND=5, JAPAN=6)
HEAP = 0x700000


class TrackOriginal(Original):
    def __init__(self, executable, data, name):
        super().__init__(executable)
        self.uc.mem_map(HEAP, 0x1000000)
        self.top = HEAP
        self.name = name
        self.assets = data / "LEVELS" / name
        self.calls = {}
        self.phase = "build"
        plc = bytearray((self.assets / f"{name}.PLC").read_bytes())
        self.nobj = struct.unpack_from("<I", plc)[0]
        for i in range(self.nobj):
            offset = 8 + 20 * i
            struct.pack_into("<I", plc, offset, struct.unpack_from("<I", plc, offset)[0] & 0xFFF)
        self.plc = self.alloc(len(plc))
        self.uc.mem_write(self.plc, bytes(plc))
        msh = (self.assets / f"{name}.MSH").read_bytes()
        self.msh = self.alloc(len(msh))
        self.uc.mem_write(self.msh, msh)
        self.put(0x1FEFEC, self.plc)
        self.put(0x1FEFD4, self.msh)
        self.put(0x1FEFE4, 2)
        self.put(0x1FEFE0, 0x400000)
        self.put(0x1FEFD0, self.alloc(2 * 0x4C))  # zero-filled vehicle IDs: rival type 0
        self.put(0x1FF164, 0)  # difficulty 0 copies the first parameter without scaling
        self.put(0x1FEF10, TRACK_IDS[name])

    def alloc(self, size):
        result = self.top
        self.top += (size + 15) & ~15
        if self.top > HEAP + 0x1000000:
            raise ValueError("Oracle heap exhausted")
        return result

    def get(self, address, fmt="<I"):
        return struct.unpack(fmt, self.uc.mem_read(address, struct.calcsize(fmt)))[0]

    def put(self, address, value):
        self.uc.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def ret(self, result=None):
        esp = self.uc.reg_read(self.reg.UC_X86_REG_ESP)
        if result is not None:
            self.uc.reg_write(self.reg.UC_X86_REG_EAX, result)
        self.uc.reg_write(self.reg.UC_X86_REG_EIP, self.get(esp))
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, esp + 4)

    def code(self, uc, address, size, _):
        if address in (0x6509C, 0x6482C, 0x56320, 0x5629E, 0x64798, 0x62775, 0x60F40):
            self.calls[address] = self.calls.get(address, 0) + 1
            eax = uc.reg_read(self.reg.UC_X86_REG_EAX)
            edx = uc.reg_read(self.reg.UC_X86_REG_EDX)
            if address == 0x6509C:
                self.ret(self.alloc(edx))
            elif address == 0x6482C:
                self.ret(self.alloc(eax * edx))
            elif address == 0x56320:
                self.ret(self.alloc(eax))
            elif address == 0x60F40:
                raw = (self.assets / f"{self.name}.TRI").read_bytes()
                size = uc.reg_read(self.reg.UC_X86_REG_EBX)
                uc.mem_write(edx, raw[:size])
                self.ret(min(size, len(raw)))
            else:
                self.ret()
            return
        if not (0x28F08 <= address <= 0x2A40D or
                0x1CE30 <= address <= 0x1DFFB or
                0x647EF <= address <= 0x64803 or
                0x64C2A <= address <= 0x6509B or
                0x55F9C <= address <= 0x55FB8 or
                0x7D68F <= address <= 0x7D715):
            raise ValueError(f"Unexpected {self.phase} code 0x{address:x}")

    def write(self, uc, access, address, size, value, _):
        if not (STACK <= address and address + size <= STACK + 0x10000 or
                HEAP <= address and address + size <= self.top or
                0x90000 <= address and address + size <= 0x220000):
            raise ValueError(f"Unexpected original write 0x{address:x}")

    def run(self, entry, eax=0, edx=0, ebx=0):
        initial = dict(EAX=eax & 0xFFFFFFFF, EBX=ebx & 0xFFFFFFFF,
                       ECX=0x12340101, EDX=edx & 0xFFFFFFFF,
                       ESI=0x23450202, EDI=0x34560303, EBP=0x45670404)
        if entry != 0x1DECC:
            initial.update(EBX=0x56780505, EDX=0x67890606)
        for reg, value in initial.items():
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + reg), value)
        for reg, value in (("ESP", ESP), ("FPCW", FPCW), ("FPTAG", 0xFFFF)):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + reg), value & 0xFFFFFFFF)
        self.put(ESP, RETURN)
        self.uc.emu_start(entry, RETURN, timeout=180_000_000, count=100_000_000)
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN:
            raise ValueError(f"{self.phase} did not return: "
                             f"{self.uc.reg_read(self.reg.UC_X86_REG_EIP):x}")
        if self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4:
            raise ValueError(f"{self.phase} stack contract failed")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF:
            raise ValueError(f"{self.phase} leaked x87 values")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != FPCW:
            raise ValueError(f"{self.phase} changed x87 control word")
        preserved = ("ECX", "ESI", "EDI", "EBP") if entry == 0x1DECC else (
            "EBX", "ECX", "EDX", "ESI", "EDI", "EBP")
        for reg in preserved:
            if self.uc.reg_read(getattr(self.reg, "UC_X86_REG_" + reg)) != initial[reg]:
                raise ValueError(f"{self.phase} did not preserve {reg}")

    def execute(self):
        self.run(0x28F08)
        self.count = self.get(0x1FF004)
        table = self.get(0x1FEFF8)
        self.sections = [list(struct.unpack("<idi d".replace(" ", ""),
                         self.uc.mem_read(table + s * 24, 24))) for s in range(self.count)]
        mapping = self.get(0x1FEFD8)
        self.objects = [
            [self.get(mapping + 12 * o, "<i"),
             *struct.unpack("<3i", self.uc.mem_read(self.plc + 12 + 20 * o, 12))]
            for o in range(self.nobj)]
        self.vertices = []
        for o in range(self.nobj):
            mesh = self.msh + self.get(self.plc + 4 + 20 * o) * 4
            self.vertices.append(bytes(self.uc.mem_read(mesh + 8, self.get(mesh) * 12)))
        self.types = [self.get(self.plc + 8 + 20 * o) for o in range(self.nobj)]
        self.phase = "load_tri"
        self.run(0x1CE30)
        source_params = list(struct.unpack("<9d", self.uc.mem_read(0xBFB28, 0x48)))
        p = self.get(0xEB7C0) + 0x7C
        self.rival_params = {hex(off): self.get(p + off, "<d")
                             for off in (0, 8, 16, 24, 32, 40, 48, 56, 64)}
        self.rival_params["0x48_int"] = self.get(p + 0x48, "<i")
        expected = dict(zip(("0x0", "0x8", "0x10", "0x18", "0x20", "0x28"),
                            source_params[:6]))
        expected.update({"0x30": 0.0, "0x38": source_params[6], "0x40": source_params[7],
                         "0x48_int": int(source_params[8])})
        if self.rival_params != expected:
            raise ValueError(f"Rival parameter layout mismatch: {self.rival_params} != {expected}")
        tri_size = (self.assets / f"{self.name}.TRI").stat().st_size
        if any(4 + obj * 500 + 0x6E > tri_size for section in self.sections
               for obj in (section[0], section[2]) if obj >= 0):
            raise ValueError("Section record would rely on unread TRI allocation tail")
        self.routes = []
        pointers = self.get(0xEB7C4)
        for s in range(self.count):
            slots = []
            row = self.get(pointers + s * 4)
            for slot in range(2):
                pointer = self.get(row + slot * 4)
                slots.append(None if not pointer else bytes(self.uc.mem_read(
                    pointer, 1 if self.get(pointer, "<B") else 75)))
            self.routes.append(slots)
        self.phase = "ai_find_section"
        selectors = []
        for s in range(-1, self.count + 1):
            for slot in range(2):
                for step in (-1, 1):
                    self.run(0x1DECC, s, slot, step)
                    if (self.uc.reg_read(self.reg.UC_X86_REG_EAX) & 0xFFFF) != self.get(0xEB7F6, "<H"):
                        raise ValueError("selector AX differs from output object")
                    if self.uc.reg_read(self.reg.UC_X86_REG_EDX) != self.get(0xEB7F4, "<H"):
                        raise ValueError("selector EDX differs from output slot")
                    selectors.append([s, slot, step, 1,
                                      self.get(0xEB7F2, "<h"), self.get(0xEB7F4, "<h"),
                                      self.get(0xEB7F6, "<h")])
        return selectors


def verify(args):
    result = subprocess.run([str(args.runner.resolve()), str(args.data), "--original-oracle"],
                            capture_output=True, text=True, check=True)
    port = {}
    for line in result.stdout.splitlines():
        values = line.split()
        if values[0] == "T":
            name = values[1]
            port[name] = dict(count=int(values[2]), nobj=int(values[3]), O=[], P=[], V=[], S=[], R=[], Q=[])
        else:
            port[name][values[0]].append(values[1:])
    failures = []
    summary = []
    export = {}
    for name in args.tracks:
        original = TrackOriginal(args.original, args.data, name)
        selectors = original.execute()
        actual = port[name]
        errors = []
        if actual["count"] != original.count:
            errors.append(dict(kind="count", original=original.count, port=actual["count"]))
        for kind, expected in (("O", original.nobj), ("S", original.count),
                               ("P", original.nobj), ("V", original.nobj),
                               ("R", original.count * 2), ("Q", len(selectors))):
            if len(actual[kind]) != expected:
                errors.append(dict(kind="row_count", table=kind, original=expected,
                                   port=len(actual[kind])))
        for o, expected in enumerate(original.objects):
            if o >= len(actual["O"]):
                continue
            row = list(map(int, actual["O"][o]))
            if row[1] != expected[0]:
                errors.append(dict(kind="object_section", object=o, original=expected[0], port=row[1]))
            if original.types[o] <= 50 and row[2:] != expected[1:]:
                errors.append(dict(kind="recenter", object=o, original=expected[1:], port=row[2:]))
        for o, expected in enumerate(original.objects):
            if o >= len(actual["P"]) or o >= len(actual["V"]):
                continue
            row = list(map(int, actual["P"][o]))
            x, y, z = expected[1:]
            cell = int((z + 25600) / 256) * 200 + int((x + 25600) / 256)
            want = [o, x, -y, z, x, y, z, cell]
            if row != want:
                errors.append(dict(kind="model_origin_grid", object=o, original=want, port=row))
            vertex_row = actual["V"][o]
            if bytes.fromhex("".join(vertex_row[1:])) != original.vertices[o]:
                errors.append(dict(kind="mesh_vertices", object=o))
        for s, expected in enumerate(original.sections):
            if s >= len(actual["S"]):
                continue
            row = actual["S"][s]
            # The original retains angles, not successor identities, in its final table.
            if list(map(int, row[1:3])) != [expected[0], expected[2]]:
                errors.append(dict(kind="topology", section=s, original=expected, port=row))
            for path, index in ((0, 5), (1, 6)):
                if expected[path * 2] < 0:
                    continue
                angle = float(row[index])
                if not math.isclose(angle, expected[path * 2 + 1], abs_tol=3e-14):
                    errors.append(dict(kind="angle", section=s, path=path,
                                       original=expected[path * 2 + 1], port=angle))
        for s, slots in enumerate(original.routes):
            for slot, raw in enumerate(slots):
                if s * 2 + slot >= len(actual["R"]):
                    continue
                row = list(map(int, actual["R"][s * 2 + slot][2:]))
                if raw is None:
                    expected = [0]
                elif raw[0]:
                    expected = [1, 0]  # invalid one-byte placeholder: no geometry payload
                    row = row[:2]
                else:
                    expected = ([1, 1, struct.unpack_from("<b", raw, 1)[0], raw[74]] +
                                list(struct.unpack_from("<6i", raw, 2)) +
                                list(struct.unpack_from("<4i", raw, 26)) +
                                list(struct.unpack_from("<8I", raw, 42)))
                if row != expected:
                    errors.append(dict(kind="record", section=s, slot=slot,
                                       original=expected, port=row))
        actual_selectors = [list(map(int, row)) for row in actual["Q"]]
        for expected, row in zip(selectors, actual_selectors):
            if expected != row:
                errors.append(dict(kind="selector", original=expected, port=row))
        item = dict(track=name, objects=original.nobj,
                    vertices=sum(len(v) // 12 for v in original.vertices),
                    sections=original.count, records=sum(r is not None for slots in
                    original.routes for r in slots), selectors=len(selectors), failures=len(errors),
                    asset_sha256={suffix: hashlib.sha256(
                        (original.assets / f"{name}.{suffix}").read_bytes()).hexdigest()
                        for suffix in ("PLC", "MSH", "TRI")},
                    original_rival_params=original.rival_params,
                    intercepted_calls={hex(k): v for k, v in original.calls.items()})
        summary.append(item)
        failures += [dict(track=name, **error) for error in errors]
        export[name] = dict(track_id=TRACK_IDS[name], asset_sha256=item["asset_sha256"],
                            original_rival_params=original.rival_params,
                            sections=original.sections,
                            records=[[None if r is None else r.hex() for r in slots]
                                     for slots in original.routes])
        print(json.dumps(item), flush=True)
    report = dict(original_sha256=ORIGINAL_SHA256, complete_function_entries=["0x28f08", "0x1ce30",
                  "0x1decc"], runner_sha256=hashlib.sha256(args.runner.read_bytes()).hexdigest(),
                  contracts=["callee-saved registers", "selector AX/EDX and output globals",
                             "stack", "empty x87 stack and unchanged control word"],
                  limitations=[
                      "Host allocation, file I/O, sprintf, free and srand are modeled.",
                      "Rival parameters: original type 0/difficulty 0 verified; port AI parameters are outside this oracle.",
                      "ICELAND's TRI short-read tail is zero-filled, but every section PLC is proven inside file bytes.",
                      "Selector inputs cover [-1,count], both slots, steps +/-1.",
                      "Invalid records own one byte; only that byte is exported.",
                      "Original recentered PLC positions, mesh vertices and derived engine cells are compared; full rendered frames and malformed assets are not."],
                  summary=summary, failures=failures)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    if args.export:
        args.export.write_text(json.dumps(export) + "\n")
    if failures:
        print(json.dumps(failures[:12], indent=2))
        raise SystemExit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--runner", type=Path, default=Path("build/track_real_assets_test"))
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--report", type=Path, default=Path("build/track-original.json"))
    parser.add_argument("--export", type=Path)
    parser.add_argument("--tracks", nargs="+", choices=TRACKS, default=TRACKS)
    verify(parser.parse_args())
