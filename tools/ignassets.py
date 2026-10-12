#!/usr/bin/env python3
"""Decode Ignition data files into PNG images for inspection."""
import argparse
import os
import struct
import sys
import zlib

COL_SIZE = 0x308
PIC_HEADER = 0x40
PIC_BLOCK = 6


def write_png(path, width, height, pixels, palette):
    """pixels: bytes of 8-bit indices; palette: list of 256 (r, g, b)."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += pixels[y * width:(y + 1) * width]
    plte = b"".join(bytes(c) for c in palette)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)))
        f.write(chunk(b"PLTE", plte))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def parse_col(data):
    """COL block: u32 size (0x308), u32 unknown, 256 RGB triplets."""
    size = struct.unpack_from("<I", data, 0)[0]
    if size != COL_SIZE:
        raise ValueError("unexpected COL size 0x%x" % size)
    rgb = data[8:8 + 768]
    # VGA DAC palettes are 6-bit; scale up when no component exceeds 63.
    scale = 4 if max(rgb) < 64 else 1
    return [tuple(min(255, v * scale) for v in rgb[i * 3:i * 3 + 3]) for i in range(256)]


def parse_pic(data):
    size, _, magic, width, height, hx, hy = struct.unpack_from("<IBBHHHH", data, 0)
    if magic != 0x95:
        raise ValueError("not a PIC file (magic 0x%02x)" % magic)
    palette = parse_col(data[PIC_HEADER:PIC_HEADER + COL_SIZE])
    # Pixel chunk: u32 size (header included), u16 type, then width*height indices.
    chunk = PIC_HEADER + COL_SIZE
    chunk_size, chunk_type = struct.unpack_from("<IH", data, chunk)
    if chunk_size != width * height + PIC_BLOCK:
        raise ValueError("unexpected pixel chunk size %d (type %d)" % (chunk_size, chunk_type))
    start = chunk + PIC_BLOCK
    pixels = data[start:start + width * height]
    if len(pixels) != width * height:
        raise ValueError("truncated pixel data")
    return dict(size=size, width=width, height=height, hx=hx, hy=hy,
                block=data[chunk:start], palette=palette, pixels=pixels)


def grey_palette():
    return [(i, i, i) for i in range(256)]


def cmd_pic(args):
    for path in args.files:
        with open(path, "rb") as f:
            pic = parse_pic(f.read())
        out = os.path.join(args.out, os.path.basename(path) + ".png")
        write_png(out, pic["width"], pic["height"], pic["pixels"], pic["palette"])
        print("%s: %dx%d extra=(%d,%d) block=%s -> %s" % (
            path, pic["width"], pic["height"], pic["hx"], pic["hy"], pic["block"].hex(), out))


def cmd_raw(args):
    palette = grey_palette()
    if args.col:
        with open(args.col, "rb") as f:
            palette = parse_col(f.read())
    for path in args.files:
        with open(path, "rb") as f:
            data = f.read()[args.skip:]
        height = len(data) // args.width
        out = os.path.join(args.out, os.path.basename(path) + ".png")
        write_png(out, args.width, height, data[:args.width * height], palette)
        print("%s: %dx%d (%d trailing bytes) -> %s" % (
            path, args.width, height, len(data) - args.width * height, out))


def load_level_objects(plc_path, msh_path):
    """PLC: u32 n + n*(i32 msh_dword_offset, u32 type, i32 x, i32 y, i32 z).
    MSH object: i32 nv, i32 nf, nv*(i32 x,y,z), nf*(u32 flags, 3*i32 vertex, 6*i32 uv 8.8, u32 ?)."""
    with open(plc_path, "rb") as f:
        plc = f.read()
    with open(msh_path, "rb") as f:
        msh = f.read()
    objs = []
    for i in range(struct.unpack_from("<I", plc, 0)[0]):
        off, typ, px, py, pz = struct.unpack_from("<iIiii", plc, 4 + i * 20)
        nv, nf = struct.unpack_from("<2i", msh, off * 4)
        vb = (off + 2) * 4
        verts = [struct.unpack_from("<3i", msh, vb + k * 12) for k in range(nv)]
        fb = vb + nv * 12
        faces = [struct.unpack_from("<I3i6iI", msh, fb + k * 44) for k in range(nf)]
        objs.append(dict(type=typ, pos=(px, py, pz), verts=verts, faces=faces))
    return objs


def cmd_level(args):
    objs = load_level_objects(args.prefix + ".PLC", args.prefix + ".MSH")
    pts = [(v[0] + o["pos"][0], v[2] + o["pos"][2]) for o in objs for v in o["verts"]]
    minx, maxx = min(p[0] for p in pts), max(p[0] for p in pts)
    minz, maxz = min(p[1] for p in pts), max(p[1] for p in pts)
    size = args.size
    scale = (size - 1) / max(maxx - minx, maxz - minz, 1)
    img = bytearray(size * size)

    def plot_line(a, b, color):
        (x0, y0), (x1, y1) = a, b
        steps = max(abs(x1 - x0), abs(y1 - y0), 1)
        for s in range(steps + 1):
            x = x0 + (x1 - x0) * s // steps
            y = y0 + (y1 - y0) * s // steps
            if 0 <= x < size and 0 <= y < size:
                img[y * size + x] = color

    for o in objs:
        px, _, pz = o["pos"]
        proj = [(int((v[0] + px - minx) * scale), size - 1 - int((v[2] + pz - minz) * scale))
                for v in o["verts"]]
        color = 1 + (o["type"] * 37) % 254
        for face in o["faces"]:
            a, b, c = face[1:4]
            plot_line(proj[a], proj[b], color)
            plot_line(proj[b], proj[c], color)
            plot_line(proj[c], proj[a], color)

    palette = [(0, 0, 0)] + [((i * 97) % 200 + 55, (i * 57) % 200 + 55, (i * 31) % 200 + 55)
                             for i in range(1, 256)]
    out = os.path.join(args.out, os.path.basename(args.prefix) + "_top.png")
    write_png(out, size, size, img, palette)
    print("%d objects, %d vertices, x=[%d,%d] z=[%d,%d] -> %s" % (
        len(objs), len(pts), minx, maxx, minz, maxz, out))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("pic", help="convert .PIC files to PNG")
    p.add_argument("files", nargs="+")
    p.add_argument("-o", "--out", default=".")
    p.set_defaults(func=cmd_pic)

    p = sub.add_parser("raw", help="dump headerless 8-bit images (.TEX, .DAT, tables)")
    p.add_argument("files", nargs="+")
    p.add_argument("-o", "--out", default=".")
    p.add_argument("-w", "--width", type=int, default=256)
    p.add_argument("--skip", type=int, default=0, help="bytes to skip at file start")
    p.add_argument("--col", help=".COL palette to apply")
    p.set_defaults(func=cmd_raw)

    p = sub.add_parser("level", help="top-down wireframe of a PLC+MSH pair")
    p.add_argument("prefix", help="path without extension, e.g. .../LEVELS/USA/USA")
    p.add_argument("-o", "--out", default=".")
    p.add_argument("--size", type=int, default=1024)
    p.set_defaults(func=cmd_level)

    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    try:
        args.func(args)
    except ValueError as e:
        sys.exit("error: %s" % e)


if __name__ == "__main__":
    main()
