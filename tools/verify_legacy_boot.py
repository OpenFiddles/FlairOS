#!/usr/bin/env python3
"""Boot the legacy i386 reference image and assert its framebuffer really came up.

`make legacy` builds a 32-bit floppy image whose desktop brings up a VBE linear
framebuffer. This gate boots that image (under qemu-system-i386 when it exists,
otherwise under qemu-system-x86_64, which runs the same 32-bit machine code),
waits for the kernel's own "AirGraph pixel desktop active (WxH ...)" line, then
takes a monitor screendump and checks that:

  * the capture has exactly the resolution the kernel reports,
  * the orientation matches the geometry,
  * the picture is varied rather than blank or single-colour.

Exit status is 1 when any check fails, so a regression in the i386 display path
cannot pass on a log line alone.

    python3 tools/verify_legacy_boot.py build/textile.img
"""
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def qemu_env():
    env = dict(os.environ)
    if env.get("QEMU_MODULE_DIR"):
        return env
    home = os.path.expanduser("~")
    for candidate in (os.path.join(home, "local/usr/lib/x86_64-linux-gnu/qemu"),
                      "/usr/lib/x86_64-linux-gnu/qemu",
                      "/usr/local/lib/qemu",
                      os.path.join(home, "local/lib/qemu")):
        if os.path.isdir(candidate):
            env["QEMU_MODULE_DIR"] = candidate
            break
    return env


def qemu_command(image, log, sock, storage):
    """The i386 machine, with whichever QEMU binary this host actually has."""
    forced = os.environ.get("QEMU_I386")
    names = [forced] if forced else ["qemu-system-i386", "qemu-system-x86_64"]
    prefix_bin = os.path.join(os.path.expanduser("~"), "local/bin")
    candidates = []
    for name in (n for n in names if n):
        candidates.append(shutil.which(name) or "")
        candidates.append(os.path.join(prefix_bin, name))
    binary = next((c for c in candidates if c and os.path.exists(c)), None)
    if binary is None:
        raise RuntimeError("no i386-capable QEMU found (tried "
                           + ", ".join(c for c in candidates if c) + ")")

    args = [binary]
    # A prefix install needs its firmware share directory; system installs do not.
    for share in (os.path.join(os.path.expanduser("~"), "local/usr/share/qemu"),
                  "/usr/share/qemu"):
        if os.path.isdir(share):
            args += ["-L", share]
            break
    args += [
        "-drive", f"file={image},format=raw,if=floppy,index=0",
        "-drive", f"file={storage},format=raw,if=ide,index=0",
        "-m", "256M", "-vga", "std", "-global", "VGA.vgamem_mb=64",
        "-display", "none", "-no-reboot",
        "-serial", f"file:{log}",
        "-monitor", f"unix:{sock},server=on,wait=off",
    ]
    return args


def monitor(sock, command, wait=0.6):
    deadline = time.time() + 20
    while not os.path.exists(sock) and time.time() < deadline:
        time.sleep(0.1)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect(sock)
    try:
        s.recv(4096)
    except socket.timeout:
        pass
    s.sendall(command.encode() + b"\n")
    time.sleep(wait)
    s.close()


def read_ppm(path):
    data = open(path, "rb").read()
    fields = []
    i = 0
    while len(fields) < 4:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while i < len(data) and data[i:i + 1] != b"\n":
                i += 1
            continue
        start = i
        while i < len(data) and not data[i:i + 1].isspace():
            i += 1
        fields.append(data[start:i])
    i += 1
    magic, width, height, maxval = fields
    if magic != b"P6" or int(maxval) != 255:
        raise RuntimeError("monitor did not produce an 8-bit PPM capture")
    width, height = int(width), int(height)
    pixels = memoryview(data)[i:]
    if len(pixels) < width * height * 3:
        raise RuntimeError("PPM capture is truncated")
    return width, height, pixels


def framebuffer_report(pixels, width, height):
    counts = {}
    sampled = 0
    step = max(1, (width * height) // 20000)
    for p in range(0, width * height, step):
        i = p * 3
        pixel = bytes(pixels[i:i + 3])
        counts[pixel] = counts.get(pixel, 0) + 1
        sampled += 1
    modal = max(counts.values())
    return len(counts), 1.0 - (modal / float(sampled))


def main(argv):
    image = argv[1] if len(argv) > 1 else os.path.join(ROOT, "build/textile.img")
    if not os.path.exists(image):
        print(f"[FAIL] legacy/i386: missing floppy image {image} (run `make legacy`)")
        return 1

    work = tempfile.mkdtemp(prefix="flairos-i386-")
    log = os.path.join(work, "serial.log")
    shot = os.path.join(work, "desktop.ppm")
    sock = os.path.join(work, "monitor.sock")
    storage = os.path.join(work, "storage.img")
    with open(storage, "wb") as f:
        f.truncate(30 * 1024 * 1024)

    try:
        cmd = qemu_command(image, log, sock, storage)
    except RuntimeError as exc:
        print(f"[FAIL] legacy/i386: {exc}")
        return 1

    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                env=qemu_env())
    except FileNotFoundError as exc:
        print(f"[FAIL] legacy/i386: cannot start QEMU: {exc}")
        return 1

    marker = "display: AirGraph pixel desktop active ("
    output = ""
    deadline = time.time() + 120
    reached = False
    while time.time() < deadline:
        time.sleep(0.25)
        output = open(log, errors="replace").read() if os.path.exists(log) else ""
        if marker in output:
            reached = True
            break
        if proc.poll() is not None:
            break

    if not reached:
        proc.terminate()
        out = proc.communicate(timeout=5)[0]
        tail = "\n".join(output.strip().splitlines()[-6:])
        print(f"[FAIL] legacy/i386: never reached '{marker}' in 120s")
        print(f"    serial tail:\n    {tail.replace(chr(10), chr(10) + '    ')}")
        print(f"    qemu: {(out or b'').decode(errors='replace').strip()[:300]}")
        print(f"    work dir: {work}")
        return 1

    declared = re.search(r"AirGraph pixel desktop active \((\d+)x(\d+)", output)
    if not declared:
        proc.terminate()
        print("[FAIL] legacy/i386: desktop line does not declare a resolution")
        return 1
    width, height = int(declared.group(1)), int(declared.group(2))
    want_orientation = "portrait" if height > width else "landscape"
    if f"{width}x{height} {want_orientation}" not in output:
        proc.terminate()
        print(f"[FAIL] legacy/i386: desktop does not label {width}x{height} as "
              f"{want_orientation}")
        return 1

    # Let the wallpaper and shell settle before capturing.
    time.sleep(2.0)
    monitor(sock, f"screendump {shot}", wait=0.8)
    alive = proc.poll() is None
    monitor(sock, "quit", wait=0.2)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

    if not alive:
        print("[FAIL] legacy/i386: guest died before the screendump")
        return 1
    if not os.path.exists(shot):
        print(f"[FAIL] legacy/i386: monitor produced no screendump (work dir: {work})")
        return 1

    pw, ph, pixels = read_ppm(shot)
    if (pw, ph) != (width, height):
        print(f"[FAIL] legacy/i386: unusable capture, screendump is {pw}x{ph}, "
              f"desktop declares {width}x{height} (work dir: {work})")
        return 1
    colours, varied = framebuffer_report(pixels, pw, ph)
    # Flat Material surfaces are legitimately few-colour; only a blank or
    # single-colour capture is rejected here.
    if colours < 8 or varied < 0.02:
        print(f"[FAIL] legacy/i386: unusable capture ({colours} colours, "
              f"{varied * 100:.1f}% non-modal pixels; work dir: {work})")
        return 1

    print(f"[ok]   legacy/i386: floppy boot -> desktop {width}x{height} "
          f"{want_orientation}, screendump {pw}x{ph}, {colours} colours, "
          f"{varied * 100:.1f}% non-modal pixels")
    shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
