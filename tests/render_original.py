#!/usr/bin/env python3
"""Reference frames from the unmodified Lisa 2 engine and rasterizer of MAINDOS.EXE (Unicorn).

The level's static objects are created with the original LI_CREATEOBJECT (0x56b28) and LI_PLACEOBJECT (0x56c74)
in the order of the level loader (sub_0002489c), then render_scene (0x56410) builds the painter's list and the
rasterizer (0x60e3d) draws it into the 0xebb60 framebuffer. Only the allocator (0x6482c) is replaced. This is a
renderer oracle, not a DOS/game emulator: cars, effects and per-frame game state are not set up.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from le2elf import LE, convert  # noqa: E402

ORIGINAL_SHA256 = "179c654ba65281f08bbcda2b5943ab9f35c309ec718600f81104644d22db948c"
HEAP, HEAP_SIZE = 0x01000000, 0x04000000
STACK, STACK_SIZE = 0x00F00000, 0x00080000
RETURN = 0x00E00000
ESP = STACK + STACK_SIZE - 0x1000
ALLOC, MALLOC, FREE = 0x6482C, 0x56320, 0x5629E
FRAMEBUFFER = 0xEBB60
LIST_LIMIT = 0x36B0
TRACK_IDS = {"CANADA": 0, "USA": 1, "CARIB": 2, "BRAZIL": 3, "AUSTRIA": 4, "ICELAND": 5, "JAPAN": 6}


class RenderOriginal:
    def __init__(self, executable):
        import unicorn
        from unicorn import x86_const
        self.unicorn, self.reg = unicorn, x86_const
        data = Path(executable).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != ORIGINAL_SHA256:
            raise ValueError(f"Unsupported original SHA-256: {digest}")
        le = LE(data)
        images, _ = convert(le)
        self.uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        for obj in le.objects:
            self.uc.mem_map(obj["base"], (obj["vsize"] + 4095) & ~4095)
            self.uc.mem_write(obj["base"], bytes(images[obj["index"]]))
        self.uc.mem_map(HEAP, HEAP_SIZE)
        self.uc.mem_map(STACK, STACK_SIZE)
        self.uc.mem_map(RETURN, 0x1000)
        self.brk = HEAP
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self._alloc_hook, begin=ALLOC, end=ALLOC)
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self._malloc_hook, begin=MALLOC, end=MALLOC)
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self._free_hook, begin=FREE, end=FREE)
        self.stray_reads = set()
        self.uc.hook_add(unicorn.UC_HOOK_MEM_READ_UNMAPPED, self._stray_read)

    # memory helpers
    def put(self, address, fmt, *values):
        self.uc.mem_write(address, struct.pack("<" + fmt, *values))

    def get(self, address, fmt):
        return struct.unpack("<" + fmt, self.uc.mem_read(address, struct.calcsize("<" + fmt)))

    def alloc(self, size, align=16):
        self.brk = (self.brk + align - 1) & ~(align - 1)
        address = self.brk
        self.brk += size
        if self.brk > HEAP + HEAP_SIZE:
            raise MemoryError("oracle heap exhausted")
        self.uc.mem_write(address, bytes(size))
        return address

    def blob(self, data, align=16):
        address = self.alloc(len(data), align)
        self.uc.mem_write(address, bytes(data))
        return address

    def _alloc_hook(self, uc, address, size, _):
        # sub_0006482c(count = eax, size = edx): zeroed block (calloc); replaced so no DOS heap is needed.
        count = uc.reg_read(self.reg.UC_X86_REG_EAX)
        unit = uc.reg_read(self.reg.UC_X86_REG_EDX)
        uc.reg_write(self.reg.UC_X86_REG_EAX, self.alloc(count * unit))
        self._return(uc)

    def _malloc_hook(self, uc, address, size, _):
        # Watcom malloc (0x56320, size = eax) reaches DOS/4GW services; serve it from the oracle heap.
        uc.reg_write(self.reg.UC_X86_REG_EAX, self.alloc(uc.reg_read(self.reg.UC_X86_REG_EAX)))
        self._return(uc)

    def _stray_read(self, uc, access, address, size, value, _):
        # The perspective rasterizer touches [esi + ebx] only to prefetch (0x7696f); the value is not used.
        eip = uc.reg_read(self.reg.UC_X86_REG_EIP)
        if eip != 0x7696F:
            return False
        self.stray_reads.add(address & ~0xFFF)
        uc.mem_map(address & ~0xFFF, 0x1000)
        return True

    def _free_hook(self, uc, address, size, _):
        self._return(uc)

    def _return(self, uc):
        esp = uc.reg_read(self.reg.UC_X86_REG_ESP)
        uc.reg_write(self.reg.UC_X86_REG_EIP, self.get(esp, "I")[0])
        uc.reg_write(self.reg.UC_X86_REG_ESP, esp + 4)

    def call(self, entry, eax=0, ebx=0, ecx=0, edx=0, esi=0, edi=0, stack=()):
        sp = ESP - 4 * len(stack) - 4
        self.put(sp, "I" * (len(stack) + 1), RETURN, *stack)
        for name, value in (("EAX", eax), ("EBX", ebx), ("ECX", ecx), ("EDX", edx), ("ESI", esi),
                            ("EDI", edi), ("EBP", 0)):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + name), value & 0xFFFFFFFF)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, sp)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, 0x037F)
        try:
            self.uc.emu_start(entry, RETURN, timeout=600_000_000, count=400_000_000)
        except self.unicorn.UcError as error:
            eip = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
            raise RuntimeError(f"original 0x{entry:x} failed at 0x{eip:x}: {error}") from error
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN:
            raise RuntimeError(f"original 0x{entry:x} did not return")
        return self.uc.reg_read(self.reg.UC_X86_REG_EAX)

    # scene
    def load_level(self, data_dir, name, width=320, height=200, mip=False):
        level = Path(data_dir) / "LEVELS" / name
        read = lambda ext: (level / f"{name}.{ext}").read_bytes()
        plc, msh, tex = read("PLC"), read("MSH"), read("TEX")
        self.track = TRACK_IDS[name]
        self.call(0x564D8)  # sub_000564d8: engine buffers, layer flags, sine table
        count = struct.unpack_from("<I", plc)[0]
        self.call(0x569EC, eax=0xC800, edx=0xC800, ebx=count + 64)  # as sub_0002489c 0x24acd
        msh_base = self.blob(msh)
        # Texture slot 0 (sub_0002489c 0x2490b): the TEX on a 64 KB boundary.
        tex_base = self.alloc(len(tex) + 0x10000, 0x10000)
        self.uc.mem_write(tex_base, tex)
        # sub_000570d8 with no MIP levels: the slot points past 5 level bases (level 0 at -5 dwords), all the TEX.
        header = self.alloc(6 * 4)
        self.put(header, "6I", *([tex_base] * 6))
        slots = self.get(0x24C03C, "I")[0]
        self.put(slots, "I", header + 0x14)
        if mip:
            raise NotImplementedError("MIP MAPPING (sub_000570d8) is not set up by the oracle")
        for table, ext in ((0x24BFA8, "SHD"), (0xC484C, "TAB"), (0x24BFEC, "PAN")):
            self.put(table, "I", self.blob(read(ext), 0x10000))
        # sub_00024840 / sub_00011494: keyed blend table, [a * 256 + b] = a ? a : b.
        keyed = bytes(a if a else b for a in range(256) for b in range(256))
        self.put(0x24C004, "I", self.blob(keyed, 0x10000))
        records = [struct.unpack_from("<iIiii", plc, 4 + i * 20) for i in range(count)]
        handles = self.alloc(count * 0x20)
        # sub_0002489c: classes < 100 first, then the rest (all detail options on: [0x1fed64] = 1).
        order = [i for i, r in enumerate(records) if (r[1] & 0xFFF) < 100]
        order += [i for i, r in enumerate(records) if (r[1] & 0xFFF) >= 100]
        self.engine_order = order
        for i in order:
            offset, kind, x, y, z = records[i]
            handle = handles + i * 0x20
            zone = (kind >> 12) & 0xF
            self.call(0x56B28, eax=handle, edx=msh_base + offset * 4, ebx=0, ecx=kind & 0xFFF,
                      stack=(0, 0xFFFFFFFF, zone))
            self.put(handle + 4, "iiiiiii", x + 0x6400, y, z + 0x6400, 0, 0, 0, 0)
            self.call(0x56C74, eax=handle)
        self.width, self.height = width, height
        self.call(0x60DC4, eax=width, ebx=height)  # rasterizer tables (reciprocals, row offsets)

    def render(self, x, y, z, pitch, yaw, roll=0.0, zone=0, reach=0, fx=200, fy=164, near=25, clear=0):
        """Engine coordinates (x/z + 25600, y up); angles in tenths of a degree, as config +0x18/+0x20/+0x28."""
        config = self.get(0x24C05C, "I")[0]
        self.put(config, "ddddddd", x, y, z, pitch, yaw, roll, 1.0)
        self.put(config + 0x38, "i", 0)
        self.put(config + 0x80, "iii", fx, fy, near)
        self.put(config + 0x9C, "iii", self.width // 2, self.height // 2, reach)
        self.put(0xC4850, "iiii", 0, 0, self.width - 1, self.height - 1)
        self.put(0x24BFF0, "i", zone)
        self.uc.mem_write(FRAMEBUFFER, bytes([clear]) * (self.width * self.height))
        self.call(0x56410)
        self.put(0xC4840, "II", self.get(0x24BFFC, "I")[0], FRAMEBUFFER)
        self.put(0xC4848, "I", FRAMEBUFFER)
        self.call(0x60E3D, esi=0xC4840)
        return bytes(self.uc.mem_read(FRAMEBUFFER, self.width * self.height))

    def texture_base(self):
        return self.get(self.get(self.get(0x24C03C, "I")[0], "I")[0] - 0x14, "I")[0]

    def primitives(self):
        """Sorted list of the last render: one text line per primitive (type, 8.8 vertices, 0x14 light)."""
        listing, lines = self.get(0x24BFFC, "I")[0], []
        for i in range(LIST_LIMIT):
            record = self.get(listing + 4 * i, "I")[0]
            if not record:
                break
            words = self.get(record, "18I")
            signed = [w - (1 << 32) if w >= 1 << 31 else w for w in words]
            luts = {self.get(0x24C004, "I")[0]: 1, self.get(0x24BFA8, "I")[0]: 2}
            if words[0] in (0x14, 0x15):
                xy, light = [signed[k] for k in (2, 3, 5, 6, 8, 9)], [signed[k] for k in (4, 7, 10)]
                line = f"{words[0]:x} " + " ".join(map(str, xy)) + " L " + " ".join(map(str, light))
                tex, lut = words[1], luts.get(words[17], 0) if words[0] == 0x15 else 0
            else:
                line = f"{words[0]:x} " + " ".join(map(str, signed[1:7]))
                tex, lut = words[8], luts.get(words[9], 0) if words[0] == 0x12 else 0
            lines.append(line + f" P {(tex - self.texture_base()) >> 16} T {lut}")
        return lines

    # primitive tests: records in the format of tests/lisa_prims.c
    def setup_raster(self, width, height, tex, shd=None):
        self.width, self.height = width, height
        self.call(0x60DC4, eax=width, ebx=height)
        self.tex_base = self.alloc(len(tex) + 0x10000, 0x10000)
        self.uc.mem_write(self.tex_base, tex)
        self.tex_size = len(tex)
        keyed = bytes(a if a else b for a in range(256) for b in range(256))
        self.luts = {1: self.blob(keyed, 0x10000)}
        if shd:
            self.luts[2] = self.blob(shd, 0x10000)

    def raster_records(self, prims):
        header = struct.unpack_from("<6i", prims)
        count = (len(prims) // 4 - 6) // 32
        mark = self.brk
        pointers = []
        for i in range(count):
            rec = list(struct.unpack_from("<32i", prims, 24 + i * 128))
            uv = self.blob(struct.pack("<6i", *rec[24:30]))
            page = self.tex_base + ((rec[30] << 16) % self.tex_size)
            words = rec[:24]
            lut = self.luts.get(rec[31], 0)
            if rec[0] in (0x11, 0x12):
                words[7], words[8], words[9] = uv, page, lut
            elif rec[0] in (0x14, 0x15):
                words[1] = page
                words[11:17] = rec[24:30]
                words[17] = lut
            else:
                raise ValueError(f"primitive 0x{rec[0]:x} not supported")
            pointers.append(self.blob(struct.pack("<24I", *[w & 0xFFFFFFFF for w in words])))
        listing = self.blob(struct.pack(f"<{len(pointers) + 1}I", *pointers, 0))
        size = self.width * self.height
        self.uc.mem_write(FRAMEBUFFER, bytes(size))
        self.put(0xC4840, "III", listing, FRAMEBUFFER, FRAMEBUFFER)
        self.put(0xC4850, "iiii", *header[2:6])
        self.call(0x60E3D, esi=0xC4840)
        self.brk = mark
        return bytes(self.uc.mem_read(FRAMEBUFFER, size))


def write_ppm(path, pixels, width, height, palette):
    out = bytearray()
    for index in pixels:
        out += palette[index]
    Path(path).write_bytes(b"P6\n%d %d\n255\n" % (width, height) + bytes(out))


def read_palette(data_dir, name):
    col = (Path(data_dir) / "LEVELS" / name / f"{name}.COL").read_bytes()[8:8 + 768]
    scale = 4 if max(col) < 64 else 1
    return [bytes(min(255, c * scale) for c in col[i * 3:i * 3 + 3]) for i in range(256)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("level", nargs="?")
    ap.add_argument("--data", default="/Users/Shared/Epic Games/Ignition/ignition")
    ap.add_argument("--cam", nargs=5, type=float, metavar=("X", "Y", "Z", "YAW", "PITCH"), 
                    help="port camera: world x, y (down), z, yaw and pitch in radians (as --cam of ./ignition)")
    ap.add_argument("--zone", type=int, default=0)
    ap.add_argument("--reach", type=int, default=0, help="config +0xa4: square cell region instead of the cone")
    ap.add_argument("--ppm")
    ap.add_argument("--raw", help="write the 8-bit indices")
    ap.add_argument("--list", help="write the sorted primitive list")
    ap.add_argument("--prims", nargs=3, metavar=("TEX", "PRIMS", "OUT"),
                    help="draw a tests/lisa_prims.c record file instead of a level frame")
    args = ap.parse_args()
    oracle = RenderOriginal(Path(args.data) / "MAINDOS.EXE")
    if args.prims:
        prims = Path(args.prims[1]).read_bytes()
        width, height = struct.unpack_from("<2i", prims)
        shd = Path(args.prims[0]).with_suffix(".SHD")
        oracle.setup_raster(width, height, Path(args.prims[0]).read_bytes(), shd.read_bytes() if shd.exists() else None)
        Path(args.prims[2]).write_bytes(oracle.raster_records(prims))
        return
    oracle.load_level(args.data, args.level)
    import math
    x, y, z, yaw, pitch = args.cam
    tenths = 1800.0 / math.pi
    pixels = oracle.render(x + 25600, -y, z + 25600, pitch * tenths, yaw * tenths, zone=args.zone, reach=args.reach)
    if args.raw:
        Path(args.raw).write_bytes(pixels)
    if args.list:
        Path(args.list).write_text("\n".join(oracle.primitives()) + "\n")
    if args.ppm:
        write_ppm(args.ppm, pixels, oracle.width, oracle.height, read_palette(args.data, args.level))


if __name__ == "__main__":
    main()
