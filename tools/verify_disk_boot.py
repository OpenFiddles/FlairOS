#!/usr/bin/env python3
"""Boot a preset from its disk image and assert it really rendered.

For each preset this starts QEMU with the raw disk image as the boot disk
(no -kernel, no floppy, no ISO), waits for the kernel's own ``render=ok WxH``
line, asks the monitor for a screendump and then checks the returned
framebuffer: the picture must have the preset's dimensions and must not be a
blank or single-colour surface, which is what a failed or skipped render looks
like.

Exit status is 1 if any preset fails, so CI fails on a regression of the disk
path rather than on a log line that merely says the kernel intended to boot.

    python3 tools/verify_disk_boot.py            # desktop, mobile, tv (x86_64)
    python3 tools/verify_disk_boot.py mobile      # one preset
"""
import json
import os
import socket
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRESETS = ("desktop", "mobile", "tv")
TIMEOUT = float(os.environ.get("DISK_BOOT_TIMEOUT", "120"))


def qemu_binary():
    return os.environ.get("QEMU_DISK", "qemu-system-x86_64")


def accel_args():
    if os.access("/dev/kvm", os.R_OK | os.W_OK):
        return ["-accel", "kvm"]
    return ["-accel", "tcg"]


def firmware_args():
    """Point QEMU at a firmware directory when the default one is not installed."""
    for env in ("QEMU_FIRMWARE", "FLAIROS_QEMU_FIRMWARE"):
        path = os.environ.get(env)
        if path and os.path.isdir(path):
            return ["-L", path]
    local = os.path.expanduser("~/local/usr/share/qemu")
    if os.path.exists(os.path.join(local, "bios-256k.bin")):
        return ["-L", local]
    return []


def preset_dims(preset):
    cfg = json.load(open(os.path.join(ROOT, "presets", preset, "preset.json")))
    return int(cfg["width"]), int(cfg["height"])


def monitor(path, command, wait=0.4):
    if not os.path.exists(path):
        return ""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect(path)
    time.sleep(0.2)
    try:
        s.recv(65536)
    except Exception:
        pass
    s.sendall((command + "\n").encode())
    time.sleep(wait)
    out = b""
    try:
        out = s.recv(65536)
    except Exception:
        pass
    s.close()
    return out.decode(errors="replace")


def read_ppm(path):
    data = open(path, "rb").read()
    fields = []
    pos = 0
    while len(fields) < 4 and pos < len(data):
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    pos += 1
    magic, w, h, maxval = fields[0], int(fields[1]), int(fields[2]), int(fields[3])
    if magic != b"P6" or maxval != 255:
        raise RuntimeError(f"{path}: unsupported PPM header {magic!r} maxval={maxval}")
    pixels = data[pos:]
    return w, h, pixels


def framebuffer_report(pixels, w, h):
    """Return (distinct_colours, fraction_not_modal) for the captured surface."""
    stride = 3
    total = w * h
    if len(pixels) < total * stride:
        raise RuntimeError(f"PPM holds {len(pixels)} bytes, need {total * stride}")
    counts = {}
    step = 1
    for i in range(0, total, step):
        px = pixels[i * stride:i * stride + 3]
        counts[px] = counts.get(px, 0) + 1
    modal = max(counts.values())
    return len(counts), 1.0 - (modal / float(total))


def boot_preset(preset):
    width, height = preset_dims(preset)
    image = os.path.join(ROOT, "build-presets", f"{preset}-x86_64",
                         f"flairos-{preset}-x86_64.img")
    if not os.path.exists(image):
        return False, f"{preset}: missing disk image {os.path.relpath(image, ROOT)}"

    work = os.path.join(ROOT, "build-presets", f"{preset}-x86_64")
    log = os.path.join(work, "disk-boot-serial.log")
    shot = os.path.join(work, "disk-boot.ppm")
    sock = os.path.join(work, "disk-boot.monitor.sock")
    for path in (log, shot, sock):
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass

    cmd = [qemu_binary()] + firmware_args() + accel_args() + [
        "-drive", f"file={image},format=raw,if=ide,index=0",
        "-boot", "c", "-vga", "std", "-m", "512M",
        "-display", "none", "-no-reboot",
        "-serial", f"file:{log}",
        "-monitor", f"unix:{sock},server=on,wait=off",
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    marker = f"render=ok {width}x{height}"
    deadline = time.time() + TIMEOUT
    rendered = False
    output = ""
    while time.time() < deadline:
        time.sleep(0.2)
        output = open(log, errors="replace").read() if os.path.exists(log) else ""
        if marker in output:
            rendered = True
            break
        if proc.poll() is not None:
            break

    if not rendered:
        proc.terminate()
        try:
            out, _ = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            out = b""
        tail = "\n".join(output.strip().splitlines()[-6:])
        return False, (f"{preset}: never reached '{marker}' in {int(TIMEOUT)}s\n"
                       f"    serial tail:\n    {tail.replace(chr(10), chr(10) + '    ')}\n"
                       f"    qemu: {(out or b'').decode(errors='replace').strip()[:300]}")

    if f"arch=x86_64 preset={preset}" not in output:
        proc.terminate()
        return False, f"{preset}: kernel log does not identify the preset"

    monitor(sock, f"screendump {shot}", wait=0.6)
    alive = proc.poll() is None
    monitor(sock, "quit", wait=0.2)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

    if not alive:
        return False, f"{preset}: guest died before the screendump"
    if not os.path.exists(shot):
        return False, f"{preset}: monitor produced no screendump"

    pw, ph, pixels = read_ppm(shot)
    if (pw, ph) != (width, height):
        return False, f"{preset}: screendump is {pw}x{ph}, preset is {width}x{height}"
    colours, varied = framebuffer_report(pixels, pw, ph)
    if colours < 32 or varied < 0.02:
        return False, (f"{preset}: framebuffer looks blank "
                       f"({colours} colours, {varied * 100:.1f}% non-modal pixels)")

    return True, (f"{preset}: disk boot -> render=ok {width}x{height}, "
                  f"screendump {pw}x{ph}, {colours} colours, "
                  f"{varied * 100:.1f}% non-modal pixels")


def main(argv):
    presets = argv[1:] or list(PRESETS)
    failures = 0
    for preset in presets:
        if preset not in PRESETS:
            print(f"unknown preset: {preset}")
            return 2
        ok, message = boot_preset(preset)
        print(("[ok]   " if ok else "[FAIL] ") + message)
        if not ok:
            failures += 1
    if failures:
        print(f"\n{failures} of {len(presets)} disk boots failed")
        return 1
    print(f"\nall {len(presets)} presets booted from disk and rendered")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
