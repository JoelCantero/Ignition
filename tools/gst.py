#!/usr/bin/env python3
"""Inspect Ignition ghost files (GHOSTS/<name>.GST) and plot them over the track.

  tools/gst.py info FILE...              header, validation, active samples and lap times
  tools/gst.py plot FILE [-o out.png]    path (red) over the level wireframe (grey)

Layout (reader 0x2adf8, recorder 0x51204, writer 0x2ccc8): int16 car, int16 track, int16 race time in centiseconds,
char[4] name, 21600 x 7 int16 {x, y, z, roll, heading, pitch, progress} at 72 Hz, 2 zero bytes = 302412 bytes.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ignassets  # noqa: E402

SIZE = 0x49D4C
SAMPLES = 21600
HZ = 72.0
LEVELS = ["CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN"]
DATA = os.environ.get("IGNITION_DATA", "/Users/Shared/Epic Games/Ignition/ignition")


def parse(path):
    data = open(path, "rb").read()
    if len(data) != SIZE:
        raise ValueError("%s: %d bytes, expected %d" % (path, len(data), SIZE))
    car, track, time_cs = struct.unpack_from("<hhh", data, 0)
    name = data[6:10].split(b"\0")[0].decode("latin-1")
    samples = [struct.unpack_from("<7h", data, 10 + i * 14) for i in range(SAMPLES)]
    errors = []
    if data[-2:] != b"\0\0":
        errors.append("trailer is not zero")
    if not 0 <= track < len(LEVELS):
        errors.append("track id %d out of range" % track)
    bad = sum(1 for s in samples if not all(0 <= a < 6434 for a in s[3:6]))
    if bad:
        errors.append("%d samples with angles outside [0, 2pi*1024)" % bad)
    return dict(car=car, track=track, time_cs=time_cs, name=name, samples=samples, errors=errors)


def active_count(samples):
    """Samples before the padded tail (the writer repeats the last pose)."""
    n = len(samples)
    while n > 1 and samples[n - 1][:6] == samples[n - 2][:6]:
        n -= 1
    return n


def lap_times(samples, n, sections):
    """Seconds between the ticks where progress // (sections + 10) changes."""
    stride = sections + 10
    marks, lap = [0], 0
    for i in range(n):
        cur = samples[i][6] // stride
        if cur > lap:
            marks.append(i)
            lap = cur
    return [(b - a) / HZ for a, b in zip(marks, marks[1:])]


def cmd_info(args):
    for path in args.files:
        g = parse(path)
        n = active_count(g["samples"])
        print("%s: car %d, track %d (%s), %.2f s, name %r, %d active samples (%.2f s at 72 Hz)" % (
            path, g["car"], g["track"], LEVELS[g["track"]] if 0 <= g["track"] < 7 else "?",
            g["time_cs"] / 100.0, g["name"], n, n / HZ))
        if args.sections:
            laps = lap_times(g["samples"], n, args.sections)
            print("  laps: %s" % ", ".join("%.2f" % t for t in laps))
        xs = [s[0] for s in g["samples"][:n]]
        zs = [s[2] for s in g["samples"][:n]]
        print("  x %d..%d, z %d..%d" % (min(xs), max(xs), min(zs), max(zs)))
        for e in g["errors"]:
            print("  ERROR: " + e)


def cmd_plot(args):
    g = parse(args.file)
    level = args.level or LEVELS[g["track"]]
    prefix = os.path.join(DATA, "LEVELS", level, level)
    objs = ignassets.load_level_objects(prefix + ".PLC", prefix + ".MSH")
    pts = [(v[0] + o["pos"][0], v[2] + o["pos"][2]) for o in objs for v in o["verts"]]
    minx, maxx = min(p[0] for p in pts), max(p[0] for p in pts)
    minz, maxz = min(p[1] for p in pts), max(p[1] for p in pts)
    size = args.size
    scale = (size - 1) / max(maxx - minx, maxz - minz, 1)
    img = bytearray(size * size)

    def to_px(x, z):
        return int((x - minx) * scale), size - 1 - int((z - minz) * scale)

    def line(a, b, color):
        (x0, y0), (x1, y1) = a, b
        steps = max(abs(x1 - x0), abs(y1 - y0), 1)
        for s in range(steps + 1):
            x, y = x0 + (x1 - x0) * s // steps, y0 + (y1 - y0) * s // steps
            if 0 <= x < size and 0 <= y < size:
                img[y * size + x] = color

    for o in objs:
        px, _, pz = o["pos"]
        proj = [to_px(v[0] + px, v[2] + pz) for v in o["verts"]]
        for face in o["faces"]:
            a, b, c = face[1:4]
            line(proj[a], proj[b], 1)
            line(proj[b], proj[c], 1)
            line(proj[c], proj[a], 1)
    n = active_count(g["samples"])
    path = [to_px(s[0], s[2]) for s in g["samples"][:n]]
    for a, b in zip(path, path[1:]):
        line(a, b, 2)
    inside = sum(1 for s in g["samples"][:n] if minx <= s[0] <= maxx and minz <= s[2] <= maxz)
    out = args.out or os.path.splitext(os.path.basename(args.file))[0] + "_ghost.png"
    palette = [(0, 0, 0), (90, 90, 90), (255, 40, 40)] + [(0, 0, 0)] * 253
    ignassets.write_png(out, size, size, img, palette)
    print("%s: %d/%d samples inside %s -> %s" % (args.file, inside, n, level, out))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info")
    p.add_argument("files", nargs="+")
    p.add_argument("--sections", type=int, help="track section count (USA 139, CANADA 175...) for lap times")
    p.set_defaults(func=cmd_info)
    p = sub.add_parser("plot")
    p.add_argument("file")
    p.add_argument("--level", help="LEVELS directory (default: from the header track id)")
    p.add_argument("-o", "--out")
    p.add_argument("--size", type=int, default=1024)
    p.set_defaults(func=cmd_plot)
    args = ap.parse_args()
    try:
        args.func(args)
    except ValueError as e:
        sys.exit("error: %s" % e)


if __name__ == "__main__":
    main()
