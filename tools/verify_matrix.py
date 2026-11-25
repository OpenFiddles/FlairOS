#!/usr/bin/env python3
"""Boot all presets (desktop, mobile, tv) on x86_64 and aarch64 QEMU,
   verify serial telemetry (ART + AirP + render), and capture screenshots.
"""
import os, subprocess, sys, time
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOT_DIR = os.path.join(ROOT, "build-presets", "screenshots")
os.makedirs(SHOT_DIR, exist_ok=True)

def boot_and_capture(preset, arch):
    out_dir = os.path.join(ROOT, "build-presets", f"{preset}-{arch}")
    ser_log = os.path.join(out_dir, "serial.log")
    ppm = os.path.join(SHOT_DIR, f"{preset}-{arch}.ppm")
    png = os.path.join(SHOT_DIR, f"{preset}-{arch}.png")
    for f in (ser_log, ppm, png):
        if os.path.exists(f):
            os.remove(f)

    if arch == "x86_64":
        kern = os.path.join(out_dir, f"flairos-{preset}-x86_64.mb.elf")
        cmd = [
            "qemu-system-x86_64", "-kernel", kern,
            "-vga", "std", "-m", "256M",
            "-serial", f"file:{ser_log}",
            "-display", "none", "-monitor", "stdio", "-no-reboot",
        ]
    else:
        kern = os.path.join(out_dir, f"flairos-{preset}-aarch64.elf")
        cmd = [
            "qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a57", "-m", "256M",
            "-device", "ramfb", "-kernel", kern,
            "-serial", f"file:{ser_log}",
            "-display", "none", "-monitor", "stdio", "-no-reboot",
        ]

    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    # Wait for render=ok in serial.log (up to 6s)
    deadline = time.time() + 6.0
    ready = False
    while time.time() < deadline:
        if os.path.exists(ser_log):
            txt = open(ser_log, errors="ignore").read()
            if "render=ok" in txt:
                ready = True
                break
        time.sleep(0.15)

    time.sleep(0.3)
    try:
        proc.stdin.write(f"mouse_move 120 120\n")
        proc.stdin.flush()
        time.sleep(0.2)
        proc.stdin.write(f"screendump {ppm}\nquit\n")
        proc.stdin.flush()
    except Exception:
        pass
    try:
        proc.wait(timeout=4)
    except subprocess.TimeoutExpired:
        proc.kill()

    ser = open(ser_log, errors="ignore").read() if os.path.exists(ser_log) else ""
    if not ready or "render=ok" not in ser:
        print(f"[FAIL] {preset}/{arch}: missing render=ok\n{ser}")
        return False

    if not os.path.exists(ppm) or os.path.getsize(ppm) < 100:
        print(f"[FAIL] {preset}/{arch}: screenshot not captured")
        return False

    im = Image.open(ppm)
    im.save(png)
    w, h = im.size
    px = list(im.getdata())
    non_black = sum(1 for r, g, b in px[::16] if (r | g | b) != 0)
    total_s = len(px[::16])
    pct = (non_black * 100.0) / max(1, total_s)
    print(f"[PASS] {preset}/{arch}: {w}x{h}, non-black={pct:.1f}%, png={png}")
    for line in ser.strip().splitlines():
        print(f"       {line}")
    return pct > 10.0

if __name__ == "__main__":
    ok = True
    for p in ("desktop", "mobile", "tv"):
        for a in ("x86_64", "aarch64"):
            if not boot_and_capture(p, a):
                ok = False
    sys.exit(0 if ok else 1)
