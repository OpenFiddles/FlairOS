#!/usr/bin/env python3
"""FlairOS icon generator.

Draws every icon from simple shapes (rounded rectangles, circles, polygons,
lines, arcs), anti-aliased, and writes:
  <out>/<name>/anim_NN_<name>.bmp   one 32-bit BGRA BMP per animation frame
  <out>/<name>/<name>.bmp           the still image (one chosen frame)
  <out>/_packed/<NAME>.BMP          all frames side by side, 8.3 file name
  <out>/manifest.txt                name, frames, size, fps, still frame
Edit the scene functions below and re-run to change any icon.

Usage: python3 tools/make_icons.py [--out resources/icons] [--size 64]
                                   [--preview preview.json]
"""
import json, math, os, struct, sys

GRID = 64          # all scenes are drawn on a 64x64 design grid
FRAMES = 8         # animation frames per icon (seamless loop)
FPS = 10

# ---- shape primitives (stored as plain tuples so they can be exported) ----
def rrect(x, y, w, h, r, c, a=1.0): return ("r", x, y, w, h, r, c, a)
def circle(cx, cy, r, c, a=1.0): return ("c", cx, cy, r, c, a)
def poly(pts, c, a=1.0): return ("p", pts, c, a)
def line(x1, y1, x2, y2, w, c, a=1.0): return ("l", x1, y1, x2, y2, w, c, a)
def arc(cx, cy, r, w, a0, a1, c, a=1.0): return ("a", cx, cy, r, w, a0, a1, c, a)

def rot(x, y, cx, cy, deg):
    s, c = math.sin(math.radians(deg)), math.cos(math.radians(deg))
    return (cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c)

def star(cx, cy, ro, ri, rot_deg=0.0, n=4):
    pts = []
    for i in range(n * 2):
        ang = math.radians(-90 + rot_deg + i * 180.0 / n)
        r = ro if i % 2 == 0 else ri
        pts.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    return pts

def clamp(v, a, b): return a if v < a else b if v > b else v

def tile(color):
    return [rrect(2, 2, 60, 60, 14, color)]

# ---- the icons: each returns the shapes for frame t (0..FRAMES-1) ----
def welcome(t):
    ph = t / FRAMES
    s = 1.0 + 0.09 * math.sin(2 * math.pi * ph)
    tw1 = 4 + 3 * max(0.0, math.sin(2 * math.pi * (ph + 0.25)))
    tw2 = 3 + 3 * max(0.0, math.sin(2 * math.pi * (ph + 0.6)))
    return tile("#6750A4") + [
        poly(star(31, 32, 21 * s, 6 * s), "#FFFFFF"),
        poly(star(48, 16, tw1, tw1 * 0.32), "#EADDFF"),
        poly(star(16, 48, tw2, tw2 * 0.32), "#D0BCFF"),
    ]

def clock(t):
    mx, my = rot(32 + 16, 32, 32, 32, -90 + 45 * t)
    hx, hy = rot(32 + 10, 32, 32, 32, 300)
    shapes = tile("#3B6FD4") + [circle(32, 32, 23, "#FFFFFF")]
    for ang in (0, 90, 180, 270):
        x, y = rot(32 + 18.5, 32, 32, 32, ang)
        shapes.append(circle(x, y, 1.7, "#3B6FD4"))
    return shapes + [
        line(32, 32, hx, hy, 3.6, "#1D2B53"),
        line(32, 32, mx, my, 2.8, "#1D2B53"),
        circle(32, 32, 3.2, "#1D2B53"),
    ]

def terminal(t):
    typed = 4 * min(t, 4)
    cursor_on = t < 4 or t % 2 == 0
    shapes = [rrect(2, 2, 60, 60, 14, "#49454F"), rrect(4, 4, 56, 56, 12, "#1D1B20"),
              circle(14, 13, 2.2, "#E5534B"), circle(21, 13, 2.2, "#E6A23C"),
              circle(28, 13, 2.2, "#2E9E6B"),
              line(14, 28, 23, 35, 3.8, "#7CE0A6"), line(23, 35, 14, 42, 3.8, "#7CE0A6")]
    if typed:
        shapes.append(rrect(29, 33, typed, 4, 2, "#7CE0A6", 0.85))
    if cursor_on:
        shapes.append(rrect(30 + typed, 31, 6, 9, 1.5, "#FFFFFF"))
    return shapes

def files(t):
    s = math.sin(math.pi * t / (FRAMES - 1))
    return tile("#E6A23C") + [
        rrect(11, 17, 20, 12, 4, "#FFE3A8"),
        rrect(11, 21, 42, 29, 5, "#FFE3A8"),
        rrect(17, 25 - 9 * s, 30, 18, 2.5, "#FFFFFF"),
        rrect(17, 30 - 9 * s, 22, 2.4, 1.2, "#E6A23C", 0.7),
        rrect(11, 31 + 2.5 * s, 42, 19, 5, "#FFFFFF"),
    ]

def settings(t):
    base = 45.0 * t / FRAMES
    shapes = tile("#625B71") + [circle(32, 32, 17.5, "#F5EFF7")]
    for i in range(8):
        a = base + i * 45
        pts = [rot(32 + d, 32 + l, 32, 32, a)
               for d, l in ((15, -4.4), (24, -3.2), (24, 3.2), (15, 4.4))]
        shapes.append(poly(pts, "#F5EFF7"))
    return shapes + [circle(32, 32, 7.5, "#625B71")]

def notes(t):
    p = 4.0 * t / (FRAMES - 1)
    shapes = tile("#7D5260") + [rrect(15, 9, 34, 46, 5, "#FFFFFF")]
    for i in range(4):
        frac = clamp(p - i, 0.0, 1.0)
        y = 20 + 9 * i
        shapes.append(line(21, y, 43, y, 2.6, "#7D5260", 0.18))
        if frac > 0.02:
            shapes.append(line(21, y, 21 + 22 * frac, y, 2.6, "#7D5260", 0.85))
    return shapes

def python(t):
    xs = []
    for k in range(9):
        y = 14 + 4.5 * k
        xs.append((32 + 12 * math.cos(math.pi * (y - 14) / 18.0), y))
    shapes = tile("#2F6FA3")
    for k in range(8):
        w = 11.0 - 4.5 * (k / 7.0)
        shapes.append(line(xs[k][0], xs[k][1], xs[k + 1][0], xs[k + 1][1], w, "#FFD43B"))
    shapes += [circle(xs[0][0], xs[0][1], 7.2, "#FFD43B"),
               circle(xs[0][0] + 2.5, xs[0][1] - 2, 1.7, "#2F6FA3")]
    if 3 <= t <= 5:
        hx, hy = xs[0][0] + 6, xs[0][1] + 1
        shapes += [line(hx, hy, hx + 6, hy + 1, 1.6, "#E5534B"),
                   line(hx + 6, hy + 1, hx + 9, hy - 1, 1.4, "#E5534B"),
                   line(hx + 6, hy + 1, hx + 9, hy + 3, 1.4, "#E5534B")]
    return shapes

def battery(t):
    lv = 0.2 + 0.8 * (t / (FRAMES - 1))
    pulse = 0.55 + 0.45 * math.sin(2 * math.pi * t / FRAMES)
    return tile("#2E9E6B") + [
        rrect(9, 19, 42, 26, 6, "#FFFFFF"),
        rrect(12, 22, 36, 20, 3.5, "#2E9E6B"),
        rrect(13.5, 23.5, 33 * lv, 17, 2.5, "#B7F5D2"),
        rrect(52, 28, 4, 8, 2, "#FFFFFF"),
        poly([(33, 22), (25, 34), (31, 34), (29, 43), (39, 30), (33, 30)], "#14513A", pulse),
    ]

def wifi(t):
    lit = (0, 1, 1, 2, 2, 3, 3, 3)[t]
    shapes = tile("#0B7A8A")
    for i, r in enumerate((10, 19, 28)):
        on = i < lit
        shapes.append(arc(32, 47, r, 5.2, 225, 315, "#FFFFFF", 1.0 if on else 0.25))
    return shapes + [circle(32, 47, 3.4, "#FFFFFF")]

def ethernet(t):
    nodes = [(32, 24), (32, 32), (16, 32), (16, 40)]
    seg = [8.0, 16.0, 8.0]
    def at(d):
        for i, L in enumerate(seg):
            if d <= L or i == len(seg) - 1:
                f = clamp(d / L, 0, 1)
                (x1, y1), (x2, y2) = nodes[i], nodes[i + 1]
                return (x1 + (x2 - x1) * f, y1 + (y2 - y1) * f)
            d -= L
    d = 32.0 * t / (FRAMES - 1)
    px, py = at(d)
    tx, ty = at(max(0.0, d - 4.0))
    shapes = tile("#4A5BC4") + [
        rrect(23, 10, 18, 14, 3.5, "#FFFFFF"),
        rrect(7, 40, 18, 14, 3.5, "#FFFFFF"), rrect(39, 40, 18, 14, 3.5, "#FFFFFF"),
        line(32, 24, 32, 32, 3, "#FFFFFF", 0.9), line(16, 32, 48, 32, 3, "#FFFFFF", 0.9),
        line(16, 32, 16, 40, 3, "#FFFFFF", 0.9), line(48, 32, 48, 40, 3, "#FFFFFF", 0.9),
        circle(tx, ty, 3.0, "#FFE082", 0.45), circle(px, py, 3.6, "#FFE082"),
    ]
    return shapes

# name -> (scene function, still frame, 8.3 packed name)
ICONS = {
    "welcome": (welcome, 0, "WELCOME"), "clock": (clock, 0, "CLOCK"),
    "terminal": (terminal, 5, "TERMINAL"), "files": (files, 0, "FILES"),
    "settings": (settings, 0, "SETTINGS"), "notes": (notes, 7, "NOTES"),
    "python": (python, 0, "PYTHON"), "battery": (battery, 6, "BATTERY"),
    "wifi": (wifi, 7, "WIFI"), "ethernet": (ethernet, 3, "ETHERNET"),
}

# ---- rasteriser: 3x3 supersampling, painter's algorithm, straight alpha ----
def hexrgb(c): return tuple(float(int(c[i:i + 2], 16)) for i in (1, 3, 5))

def in_poly(x, y, pts):
    inside, j = False, len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]; xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside

def make_test(p):
    k = p[0]
    if k == "r":
        _, x, y, w, h, r, _, _ = p
        r = min(r, w / 2.0, h / 2.0)
        cx, cy, hw, hh = x + w / 2.0, y + h / 2.0, w / 2.0 - r, h / 2.0 - r
        def f(px, py):
            dx = max(abs(px - cx) - hw, 0.0); dy = max(abs(py - cy) - hh, 0.0)
            return dx * dx + dy * dy <= r * r
        return (x, y, x + w, y + h), f
    if k == "c":
        _, cx, cy, r, _, _ = p
        return (cx - r, cy - r, cx + r, cy + r), lambda px, py: (px - cx) ** 2 + (py - cy) ** 2 <= r * r
    if k == "p":
        pts = p[1]
        xs = [q[0] for q in pts]; ys = [q[1] for q in pts]
        return (min(xs), min(ys), max(xs), max(ys)), lambda px, py: in_poly(px, py, pts)
    if k == "l":
        _, x1, y1, x2, y2, w, _, _ = p
        hw = w / 2.0
        vx, vy = x2 - x1, y2 - y1
        ll = vx * vx + vy * vy
        def f(px, py):
            t = 0.0 if ll == 0 else clamp(((px - x1) * vx + (py - y1) * vy) / ll, 0.0, 1.0)
            dx, dy = px - (x1 + t * vx), py - (y1 + t * vy)
            return dx * dx + dy * dy <= hw * hw
        return (min(x1, x2) - hw, min(y1, y2) - hw, max(x1, x2) + hw, max(y1, y2) + hw), f
    _, cx, cy, r, w, a0, a1, _, _ = p
    span = (a1 - a0) % 360.0
    def f(px, py):
        dx, dy = px - cx, py - cy
        if abs(math.hypot(dx, dy) - r) > w / 2.0: return False
        return (math.degrees(math.atan2(dy, dx)) - a0) % 360.0 <= span
    e = r + w / 2.0
    return (cx - e, cy - e, cx + e, cy + e), f

def raster(shapes, size):
    n = size * size
    R = [0.0] * n; G = [0.0] * n; B = [0.0] * n; A = [0.0] * n
    sc = GRID / float(size)
    offs = (1 / 6.0, 3 / 6.0, 5 / 6.0)
    for p in shapes:
        color, alpha = (p[-2], p[-1])
        cr, cg, cb = hexrgb(color)
        (bx0, by0, bx1, by1), test = make_test(p)
        x0 = max(0, int(bx0 / sc) - 1); x1 = min(size - 1, int(bx1 / sc) + 1)
        y0 = max(0, int(by0 / sc) - 1); y1 = min(size - 1, int(by1 / sc) + 1)
        for py in range(y0, y1 + 1):
            for px in range(x0, x1 + 1):
                hit = 0
                for oy in offs:
                    for ox in offs:
                        if test((px + ox) * sc, (py + oy) * sc): hit += 1
                if not hit: continue
                sa = hit / 9.0 * alpha
                i = py * size + px
                da = A[i]
                oa = sa + da * (1 - sa)
                R[i] = (cr * sa + R[i] * da * (1 - sa)) / oa
                G[i] = (cg * sa + G[i] * da * (1 - sa)) / oa
                B[i] = (cb * sa + B[i] * da * (1 - sa)) / oa
                A[i] = oa
    return R, G, B, A

def write_bmp(path, frames, size):
    """frames: list of raster() results laid out left to right."""
    w, h = size * len(frames), size
    rows = bytearray()
    for y in range(h - 1, -1, -1):                      # bottom-up
        for f in frames:
            R, G, B, A = f
            for x in range(size):
                i = y * size + x
                rows += bytes((int(B[i] + 0.5), int(G[i] + 0.5), int(R[i] + 0.5), int(A[i] * 255 + 0.5)))
    hdr = b"BM" + struct.pack("<IHHI", 54 + len(rows), 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 32, 0, len(rows), 2835, 2835, 0, 0)
    with open(path, "wb") as fh:
        fh.write(hdr + rows)

def main():
    args = sys.argv[1:]
    def opt(name, default):
        return args[args.index(name) + 1] if name in args else default
    out = opt("--out", "resources/icons")
    size = int(opt("--size", "64"))
    preview = opt("--preview", "")
    os.makedirs(os.path.join(out, "_packed"), exist_ok=True)
    manifest = ["# name frames=N fps=F size=S still=NN packed=8.3-name"]
    pv = {}
    for name, (scene, still, packed) in ICONS.items():
        d = os.path.join(out, name)
        os.makedirs(d, exist_ok=True)
        rasters = []
        for t in range(FRAMES):
            r = raster(scene(t), size)
            rasters.append(r)
            write_bmp(os.path.join(d, "anim_%02d_%s.bmp" % (t, name)), [r], size)
        write_bmp(os.path.join(d, name + ".bmp"), [rasters[still]], size)
        write_bmp(os.path.join(out, "_packed", packed + ".BMP"), rasters, size)
        manifest.append("%s frames=%d fps=%d size=%d still=%02d packed=%s.BMP" %
                        (name, FRAMES, FPS, size, still, packed))
        pv[name] = {"still": scene(still), "frames": [scene(t) for t in (0, 2, 4, 6)]}
        print("wrote", name, flush=True)
    with open(os.path.join(out, "manifest.txt"), "w") as fh:
        fh.write("\n".join(manifest) + "\n")
    if preview:
        def rnd(o):
            if isinstance(o, float): return round(o, 1)
            if isinstance(o, (list, tuple)): return [rnd(v) for v in o]
            return o
        with open(preview, "w") as fh:
            json.dump({k: {"still": rnd(v["still"]), "frames": rnd(v["frames"])} for k, v in pv.items()},
                      fh, separators=(",", ":"))

if __name__ == "__main__":
    main()
