#!/usr/bin/env python3
"""Interactive VNC RFB Client for FlairOS (Screenshot, Mouse, Drag, Type, Demo).

Pure Python stdlib (socket + struct + zlib) — zero external dependencies.
Supports QEMU VNC servers on ports 5900 (i386), 5901 (x86_64),
5902 (aarch64 desktop), 5903 (aarch64 mobile), 5904 (aarch64 tv).
"""
import argparse
import os
import socket
import struct
import time
import json
import zlib


def recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("VNC connection closed")
        buf.extend(chunk)
    return bytes(buf)


def write_png(path, width, height, rgb_bytes):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw.extend(rgb_bytes[y * stride:(y + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


class VNCClient:
    def __init__(self, host="127.0.0.1", port=5902):
        self.sock = socket.create_connection((host, port), timeout=10.0)
        ver = recv_exact(self.sock, 12)
        self.sock.sendall(ver[:12] if ver.startswith(b"RFB ") else b"RFB 003.008\n")
        nsec = recv_exact(self.sock, 1)[0]
        if nsec == 0:
            raise RuntimeError("VNC handshake failed")
        secs = recv_exact(self.sock, nsec)
        if 1 not in secs:
            raise RuntimeError(f"VNC requires auth (types={list(secs)})")
        self.sock.sendall(b"\x01")
        if ver >= b"RFB 003.008":
            res = struct.unpack(">I", recv_exact(self.sock, 4))[0]
            if res != 0:
                raise RuntimeError("VNC security result failed")
        self.sock.sendall(b"\x01")  # shared=1
        sinit = recv_exact(self.sock, 24)
        self.w, self.h = struct.unpack(">HH", sinit[:4])
        nlen = struct.unpack(">I", sinit[20:24])[0]
        self.name = recv_exact(self.sock, nlen).decode("utf-8", errors="replace")

        # Request 32bpp little-endian RGBX (SetPixelFormat=0)
        pf = struct.pack(">BBBBHHHBBB3x", 32, 24, 0, 1, 255, 255, 255, 16, 8, 0)
        self.sock.sendall(struct.pack(">B3x", 0) + pf)
        # SetEncodings=2: RAW (0) only for universal compatibility
        self.sock.sendall(struct.pack(">BxHi", 2, 1, 0))
        self.fb = bytearray(self.w * self.h * 3)

    def close(self):
        self.sock.close()

    def pointer(self, x, y, mask=0):
        x = max(0, min(self.w - 1, int(x)))
        y = max(0, min(self.h - 1, int(y)))
        self.sock.sendall(struct.pack(">BBHH", 5, mask, x, y))

    def move(self, x, y):
        self.pointer(x, y, 0)

    def click(self, x, y, button=1, hold=0.06):
        self.pointer(x, y, 0)
        time.sleep(0.03)
        self.pointer(x, y, button)
        time.sleep(hold)
        self.pointer(x, y, 0)

    def drag(self, x1, y1, x2, y2, steps=8):
        self.pointer(x1, y1, 0)
        time.sleep(0.03)
        self.pointer(x1, y1, 1)
        for i in range(1, steps + 1):
            nx = x1 + (x2 - x1) * i // steps
            ny = y1 + (y2 - y1) * i // steps
            self.pointer(nx, ny, 1)
            time.sleep(0.02)
        self.pointer(x2, y2, 0)

    def key(self, keysym):
        self.sock.sendall(struct.pack(">BB2xI", 4, 1, keysym))
        time.sleep(0.02)
        self.sock.sendall(struct.pack(">BB2xI", 4, 0, keysym))

    def type_text(self, text):
        special = {"\n": 0xFF0D, "\r": 0xFF0D, "\b": 0xFF08, "\t": 0xFF09}
        for ch in text:
            ks = special.get(ch, ord(ch))
            self.key(ks)
            time.sleep(0.02)

    def capture(self, out_png, wait=0.25):
        time.sleep(wait)
        self.sock.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, self.w, self.h))
        while True:
            hdr = recv_exact(self.sock, 1)
            mtype = hdr[0]
            if mtype == 0:  # FramebufferUpdate
                _, nrects = struct.unpack(">BH", recv_exact(self.sock, 3))
                for _ in range(nrects):
                    rx, ry, rw, rh, enc = struct.unpack(">HHHHi", recv_exact(self.sock, 12))
                    if enc != 0:
                        raise RuntimeError(f"Unsupported encoding {enc}")
                    raw = recv_exact(self.sock, rw * rh * 4)
                    for row in range(rh):
                        src_off = row * rw * 4
                        dst_off = ((ry + row) * self.w + rx) * 3
                        for col in range(rw):
                            b, g, r = raw[src_off + col * 4], raw[src_off + col * 4 + 1], raw[src_off + col * 4 + 2]
                            self.fb[dst_off + col * 3] = r
                            self.fb[dst_off + col * 3 + 1] = g
                            self.fb[dst_off + col * 3 + 2] = b
                break
            elif mtype == 1:  # SetColourMapEntries
                _, _, ncolors = struct.unpack(">BHH", recv_exact(self.sock, 5))
                recv_exact(self.sock, ncolors * 6)
            elif mtype == 2:  # Bell
                pass
            elif mtype == 3:  # ServerCutText
                _, slen = struct.unpack(">3xI", recv_exact(self.sock, 7))
                recv_exact(self.sock, slen)
        write_png(out_png, self.w, self.h, bytes(self.fb))
        print(f"[OK] Captured {self.w}x{self.h} ({self.name}) -> {out_png}")



def run_qmp(sock_path, execute_cmd):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(5.0)
    s.connect(sock_path)
    f = s.makefile("rwb", buffering=0)
    f.readline()  # greeting
    f.write(b'{"execute":"qmp_capabilities"}\n')
    f.readline()
    f.write((json.dumps({"execute": execute_cmd}) + "\n").encode("utf-8"))
    resp = f.readline().decode("utf-8", errors="replace").strip()
    s.close()
    print(resp)

def main():
    ap = argparse.ArgumentParser(description="FlairOS Interactive VNC Controller")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_sc = sub.add_parser("screenshot")
    p_sc.add_argument("port", type=int)
    p_sc.add_argument("out")

    p_mv = sub.add_parser("move")
    p_mv.add_argument("port", type=int)
    p_mv.add_argument("x", type=int)
    p_mv.add_argument("y", type=int)

    p_cl = sub.add_parser("click")
    p_cl.add_argument("port", type=int)
    p_cl.add_argument("x", type=int)
    p_cl.add_argument("y", type=int)
    p_cl.add_argument("--out")

    p_dr = sub.add_parser("drag")
    p_dr.add_argument("port", type=int)
    p_dr.add_argument("x1", type=int)
    p_dr.add_argument("y1", type=int)
    p_dr.add_argument("x2", type=int)
    p_dr.add_argument("y2", type=int)
    p_dr.add_argument("--out")

    p_tp = sub.add_parser("type")
    p_tp.add_argument("port", type=int)
    p_tp.add_argument("text")
    p_tp.add_argument("--out")

    p_dm = sub.add_parser("demo")
    p_dm.add_argument("port", type=int)
    p_dm.add_argument("out_dir")

    p_qm = sub.add_parser("qmp")
    p_qm.add_argument("sock")
    p_qm.add_argument("execute", default="query-status", nargs="?")

    args = ap.parse_args()
    if args.cmd == "qmp":
        run_qmp(args.sock, args.execute)
        return
    c = VNCClient("127.0.0.1", args.port)
    try:
        if args.cmd == "screenshot":
            c.capture(args.out, wait=0.1)
        elif args.cmd == "move":
            c.move(args.x, args.y)
        elif args.cmd == "click":
            c.click(args.x, args.y)
            if args.out:
                c.capture(args.out, wait=0.3)
        elif args.cmd == "drag":
            c.drag(args.x1, args.y1, args.x2, args.y2)
            if args.out:
                c.capture(args.out, wait=0.3)
        elif args.cmd == "type":
            c.type_text(args.text)
            if args.out:
                c.capture(args.out, wait=0.3)
        elif args.cmd == "demo":
            os.makedirs(args.out_dir, exist_ok=True)
            c.capture(os.path.join(args.out_dir, "01_boot.png"), wait=0.2)
            # Hover Launcher Dock icon (index 0: x~235, y~715)
            c.move(235, 715)
            c.capture(os.path.join(args.out_dir, "02_dock_hover.png"), wait=0.35)
            # Click Launcher Dock icon
            c.click(235, 715)
            c.capture(os.path.join(args.out_dir, "03_launcher.png"), wait=0.4)
            # Click Calculator Dock icon (index 4: x~540, y~715)
            c.click(540, 715)
            c.type_text("7*6=")
            c.capture(os.path.join(args.out_dir, "04_calc.png"), wait=0.4)
            # Click top-right AirStinUI Control Center
            c.click(920, 16)
            c.capture(os.path.join(args.out_dir, "05_control_center.png"), wait=0.4)
    finally:
        c.close()


if __name__ == "__main__":
    main()
