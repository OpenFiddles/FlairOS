#!/usr/bin/env python3
"""Generate 4-frame animated 24x24 BMP icon sequences under resources/images/icons/<name>/anim_<f>_<name>.bmp
and pack them into include/anim_icons_gen.h for 64-bit FlairOS."""
import math, os, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS_DIR = os.path.join(ROOT, "resources", "images", "icons")
OUT_HDR = os.path.join(ROOT, "include", "anim_icons_gen.h")

ICONS = [
    ("launcher", (236, 72, 153),  (99, 102, 241)),
    ("finder",   (56, 132, 255),  (99, 102, 241)),
    ("terminal", (34, 42, 62),    (45, 212, 191)),
    ("notes",    (249, 115, 22),  (236, 72, 153)),
    ("calc",     (245, 158, 11),  (99, 102, 241)),
    ("browser",  (20, 184, 166),  (56, 132, 255)),
    ("apphost",  (34, 197, 94),   (20, 184, 166)),
    ("settings", (99, 102, 241),  (56, 132, 255)),
]

SZ = 24
FRAMES = 4


def write_bmp24(path, pixels):
    stride = ((SZ * 24 + 31) // 32) * 4
    img_sz = stride * SZ
    hdr = struct.pack("<2sIHHI", b"BM", 54 + img_sz, 0, 0, 54)
    dib = struct.pack("<IIIHHIIIIII", 40, SZ, SZ, 1, 24, 0, img_sz, 2835, 2835, 0, 0)
    rows = bytearray()
    for y in range(SZ - 1, -1, -1):
        row = bytearray(stride)
        for x in range(SZ):
            r, g, b = pixels[y * SZ + x]
            row[x * 3 + 0] = b
            row[x * 3 + 1] = g
            row[x * 3 + 2] = r
        rows.extend(row)
    with open(path, "wb") as f:
        f.write(hdr + dib + rows)


def render_frame(c1, c2, frame):
    px = [(255, 0, 255)] * (SZ * SZ)
    pulse = [0.0, 0.35, 0.7, 0.35][frame]
    cx, cy = (SZ - 1) / 2.0, (SZ - 1) / 2.0
    rad = 9.5 + pulse * 1.4
    for y in range(SZ):
        for x in range(SZ):
            dx, dy = abs(x - cx), abs(y - cy)
            # Squircle distance
            d = (dx ** 3 + dy ** 3) ** (1.0 / 3.0)
            if d <= rad:
                t = y / (SZ - 1)
                r = int(min(255, c1[0] * (1 - t) + c2[0] * t + pulse * 28))
                g = int(min(255, c1[1] * (1 - t) + c2[1] * t + pulse * 28))
                b = int(min(255, c1[2] * (1 - t) + c2[2] * t + pulse * 28))
                # Inner animated ring highlight
                ring = abs(d - (5.2 + pulse * 1.6))
                if ring < 0.9:
                    r = min(255, r + 65)
                    g = min(255, g + 65)
                    b = min(255, b + 65)
                px[y * SZ + x] = (r, g, b)
    return px


def main():
    lines = [
        "#ifndef FLAIROS_ANIM_ICONS_GEN_H",
        "#define FLAIROS_ANIM_ICONS_GEN_H",
        "#include <stdint.h>",
        "#define ANIM_ICON_COUNT 8",
        "#define ANIM_ICON_FRAMES 4",
        "",
    ]
    ptr_table = []
    sz_table = []
    for name, c1, c2 in ICONS:
        idir = os.path.join(ICONS_DIR, name)
        os.makedirs(idir, exist_ok=True)
        row_ptrs = []
        row_szs = []
        for f in range(FRAMES):
            fname = f"anim_{f}_{name}.bmp"
            fpath = os.path.join(idir, fname)
            write_bmp24(fpath, render_frame(c1, c2, f))
            raw = open(fpath, "rb").read()
            sym = f"anim_{name}_{f}_bmp"
            lines.append(f"static const uint8_t {sym}[] = {{")
            for off in range(0, len(raw), 16):
                lines.append("    " + ", ".join(f"0x{b:02X}u" for b in raw[off:off + 16]) + ",")
            lines.append("};")
            row_ptrs.append(sym)
            row_szs.append(f"(uint32_t)sizeof({sym})")
        ptr_table.append("    { " + ", ".join(row_ptrs) + " }")
        sz_table.append("    { " + ", ".join(row_szs) + " }")

    lines.append("static const uint8_t * const anim_icon_bmps[ANIM_ICON_COUNT][ANIM_ICON_FRAMES] = {")
    lines.extend(p + "," for p in ptr_table)
    lines.append("};")
    lines.append("static const uint32_t anim_icon_sizes[ANIM_ICON_COUNT][ANIM_ICON_FRAMES] = {")
    lines.extend(s + "," for s in sz_table)
    lines.append("};")
    lines.append("#endif")
    with open(OUT_HDR, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"[OK] Generated {len(ICONS) * FRAMES} animated BMP icons + {OUT_HDR}")


if __name__ == "__main__":
    main()
