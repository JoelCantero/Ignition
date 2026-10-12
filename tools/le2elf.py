#!/usr/bin/env python3
"""Convert a DOS/4GW LE executable into a static ELF32 i386 image for disassemblers.

Objects are placed at their LE relocation base addresses and all internal
fixups are applied, so the resulting ELF is a flat 32-bit memory image.
"""
import argparse
import json
import struct
import sys

PF_X, PF_W, PF_R = 1, 2, 4
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 1, 2, 4

# Placeholder selector written for 16-bit selector fixups (flat model).
FLAT_SELECTOR = 0x0010


def u8(d, o):
    return d[o]


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def s16(d, o):
    return struct.unpack_from("<h", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


class LE:
    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not an MZ executable")
        h = u32(data, 0x3C)
        if data[h:h + 2] != b"LE":
            raise ValueError("no LE header at 0x%x" % h)
        self.h = h
        self.num_pages = u32(data, h + 0x14)
        self.eip_obj = u32(data, h + 0x18)
        self.eip = u32(data, h + 0x1C)
        self.esp_obj = u32(data, h + 0x20)
        self.esp = u32(data, h + 0x24)
        self.page_size = u32(data, h + 0x28)
        self.last_page_size = u32(data, h + 0x2C)
        obj_tab = h + u32(data, h + 0x40)
        num_objs = u32(data, h + 0x44)
        self.page_map = h + u32(data, h + 0x48)
        self.fixup_pages = h + u32(data, h + 0x68)
        self.fixup_records = h + u32(data, h + 0x6C)
        self.data_pages = u32(data, h + 0x80)
        if u32(data, h + 0x74):
            print("warning: module has imports; they are ignored", file=sys.stderr)

        self.objects = []
        for i in range(num_objs):
            o = obj_tab + i * 24
            vsize, base, flags, pidx, pcount = struct.unpack_from("<IIIII", data, o)
            self.objects.append(dict(index=i + 1, vsize=vsize, base=base, flags=flags,
                                     page_index=pidx, page_count=pcount))

    def page_file_offset(self, logical_page):
        """logical_page is 1-based, as stored in object table entries."""
        e = self.page_map + (logical_page - 1) * 4
        b = self.data[e:e + 4]
        num = (b[0] << 16) | (b[1] << 8) | b[2]
        if b[3] != 0:
            raise ValueError("unsupported page type %d for page %d" % (b[3], logical_page))
        return self.data_pages + (num - 1) * self.page_size

    def load_object(self, obj):
        img = bytearray(obj["vsize"])
        for k in range(obj["page_count"]):
            lp = obj["page_index"] + k
            size = self.last_page_size if lp == self.num_pages else self.page_size
            off = self.page_file_offset(lp)
            chunk = self.data[off:off + size]
            dst = k * self.page_size
            n = max(0, min(len(chunk), len(img) - dst))
            img[dst:dst + n] = chunk[:n]
        return img

    def fixups_for_page(self, logical_page):
        d = self.data
        start = self.fixup_records + u32(d, self.fixup_pages + (logical_page - 1) * 4)
        end = self.fixup_records + u32(d, self.fixup_pages + logical_page * 4)
        p = start
        while p < end:
            src = u8(d, p)
            flg = u8(d, p + 1)
            p += 2
            stype = src & 0x0F
            has_list = bool(src & 0x20)
            if has_list:
                count = u8(d, p)
                p += 1
            else:
                srcoff = s16(d, p)
                p += 2
            if (flg & 0x03) != 0:
                raise ValueError("unsupported fixup target type %d" % (flg & 0x03))
            if flg & 0x40:
                tobj = u16(d, p)
                p += 2
            else:
                tobj = u8(d, p)
                p += 1
            toff = 0
            if stype != 0x02:
                if flg & 0x10:
                    toff = u32(d, p)
                    p += 4
                else:
                    toff = u16(d, p)
                    p += 2
            if flg & 0x04:
                if flg & 0x20:
                    toff += u32(d, p)
                    p += 4
                else:
                    toff += u16(d, p)
                    p += 2
            offs = []
            if has_list:
                for _ in range(count):
                    offs.append(s16(d, p))
                    p += 2
            else:
                offs.append(srcoff)
            for so in offs:
                yield stype, so, tobj, toff


def convert(le):
    images = {o["index"]: le.load_object(o) for o in le.objects}
    bases = {o["index"]: o["base"] for o in le.objects}
    stats = {}

    def write(obj_index, off, fmt, value):
        img = images[obj_index]
        n = struct.calcsize(fmt)
        if 0 <= off and off + n <= len(img):
            struct.pack_into(fmt, img, off, value)
            return
        # Fixup straddles an object page boundary: write byte by byte where it fits.
        raw = struct.pack(fmt, value)
        for i, b in enumerate(raw):
            if 0 <= off + i < len(img):
                img[off + i] = b

    for o in le.objects:
        for k in range(o["page_count"]):
            lp = o["page_index"] + k
            page_off = k * le.page_size
            for stype, so, tobj, toff in le.fixups_for_page(lp):
                target = bases[tobj] + toff
                off = page_off + so
                stats[stype] = stats.get(stype, 0) + 1
                if stype == 0x07:
                    write(o["index"], off, "<I", target & 0xFFFFFFFF)
                elif stype == 0x08:
                    write(o["index"], off, "<i", target - (o["base"] + off + 4))
                elif stype == 0x05:
                    write(o["index"], off, "<H", target & 0xFFFF)
                elif stype == 0x06:
                    write(o["index"], off, "<I", target & 0xFFFFFFFF)
                    write(o["index"], off + 4, "<H", FLAT_SELECTOR)
                elif stype == 0x02:
                    write(o["index"], off, "<H", FLAT_SELECTOR)
                elif stype == 0x00:
                    write(o["index"], off, "<B", target & 0xFF)
                else:
                    raise ValueError("unsupported fixup source type 0x%x" % stype)
    return images, stats


def build_elf(le, images):
    objs = le.objects
    phnum = len(objs)
    ehsize, phentsize, shentsize = 52, 32, 40
    data_off = (ehsize + phnum * phentsize + 0xFFF) & ~0xFFF

    out = bytearray(data_off)
    segs = []
    for o in objs:
        img = images[o["index"]]
        # Trim trailing zeros into memsz so BSS stays out of the file.
        filesz = len(img.rstrip(b"\0"))
        # Keep file offset congruent to vaddr modulo page size.
        pad = (o["base"] - len(out)) % 0x1000
        out += b"\0" * pad
        segs.append((o, len(out), filesz))
        out += img[:filesz]

    names = [b""] + [b".obj%d" % o["index"] for o in objs] + [b".shstrtab"]
    shstr = bytearray()
    name_offs = []
    for n in names:
        name_offs.append(len(shstr))
        shstr += n + b"\0"
    shstr_off = len(out)
    out += shstr
    while len(out) % 4:
        out += b"\0"
    shoff = len(out)

    entry = le.objects[le.eip_obj - 1]["base"] + le.eip
    shnum = len(names)
    struct.pack_into("<16sHHIIIIIHHHHHH", out, 0,
                     b"\x7fELF\x01\x01\x01" + b"\0" * 9,
                     2, 3, 1, entry, ehsize, shoff, 0,
                     ehsize, phentsize, phnum, shentsize, shnum, shnum - 1)

    for i, (o, off, filesz) in enumerate(segs):
        f = o["flags"]
        pflags = (PF_R if f & 1 else 0) | (PF_W if f & 2 else 0) | (PF_X if f & 4 else 0)
        struct.pack_into("<IIIIIIII", out, ehsize + i * phentsize,
                         1, off, o["base"], o["base"], filesz, o["vsize"], pflags, 0x1000)

    out += b"\0" * shentsize
    for i, (o, off, filesz) in enumerate(segs):
        f = o["flags"]
        shflags = SHF_ALLOC | (SHF_WRITE if f & 2 else 0) | (SHF_EXECINSTR if f & 4 else 0)
        # SHT_NOBITS-style tail is represented by the program header memsz.
        out += struct.pack("<IIIIIIIIII", name_offs[i + 1], 1, shflags, o["base"], off,
                           filesz, 0, 0, 16, 0)
    out += struct.pack("<IIIIIIIIII", name_offs[-1], 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0)
    return bytes(out), entry


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", help="LE executable (e.g. MAINDOS.EXE)")
    ap.add_argument("output", help="ELF32 image to write")
    ap.add_argument("--map", help="optional JSON file describing objects and entry point")
    args = ap.parse_args()

    with open(args.input, "rb") as f:
        le = LE(f.read())
    images, stats = convert(le)
    elf, entry = build_elf(le, images)
    with open(args.output, "wb") as f:
        f.write(elf)

    info = {
        "entry": "0x%08x" % entry,
        "stack": "0x%08x" % (le.objects[le.esp_obj - 1]["base"] + le.esp),
        "objects": [{"index": o["index"], "base": "0x%08x" % o["base"],
                     "size": "0x%x" % o["vsize"], "flags": "0x%x" % o["flags"]}
                    for o in le.objects],
        "fixups_by_source_type": {"0x%02x" % k: v for k, v in sorted(stats.items())},
    }
    print(json.dumps(info, indent=2))
    if args.map:
        with open(args.map, "w") as f:
            json.dump(info, f, indent=2)


if __name__ == "__main__":
    main()
