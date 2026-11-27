#!/usr/bin/env python3
"""Boot a preset from its disk image and assert it really rendered.

For each preset this starts QEMU with the disk image as the boot medium (no
-kernel, no floppy, no ISO), waits for the kernel's own ``render=ok WxH`` line,
asks the monitor for a screendump and then checks the returned framebuffer: the
picture must have the preset's dimensions and must not be a blank or
single-colour surface, which is what a failed or skipped render looks like.

x86_64 boots its raw BIOS image. aarch64 boots through the real AAVMF (EDK2)
firmware off a partitioned image with a FAT ESP, where the firmware's own block
device reads our loader and the loader reads the kernel off the same disk.

Exit status is 1 if any preset fails, so CI fails on a regression of the disk
path rather than on a log line that merely says the kernel intended to boot.

    python3 tools/verify_disk_boot.py                    # all presets, both arches
    python3 tools/verify_disk_boot.py --arch x86_64       # one architecture
    python3 tools/verify_disk_boot.py mobile              # one preset
    python3 tools/verify_disk_boot.py --image x.img --name desktop --dims 1024x768
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRESETS = ("desktop", "mobile", "tv")
ARCHES = ("x86_64", "aarch64")


def qemu_env():
    """QEMU needs its module directory when it is installed under a prefix."""
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


def accel_args():
    forced = os.environ.get("FLAIROS_QEMU_ACCEL")
    if forced:
        return ["-accel", forced]
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


def aarch64_firmware():
    """Return (code, vars) paths for real AAVMF/EDK2 firmware, or None."""
    home = os.path.expanduser("~")
    dirs = [os.environ.get("FLAIROS_AAVMF", ""),
            os.path.join(home, "local/usr/share/AAVMF"),
            "/usr/share/AAVMF",
            "/usr/share/qemu-efi-aarch64"]
    for d in dirs:
        if not d:
            continue
        code = os.path.join(d, "AAVMF_CODE.fd")
        varsf = os.path.join(d, "AAVMF_VARS.fd")
        if os.path.exists(code) and os.path.exists(varsf):
            return code, varsf
        qefi = os.path.join(d, "QEMU_EFI.fd")
        if os.path.exists(qefi):
            return qefi, None
    return None


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
    return w, h, data[pos:]


def framebuffer_report(pixels, w, h):
    """Return (distinct_colours, fraction_not_modal) for the captured surface."""
    stride = 3
    total = w * h
    if len(pixels) < total * stride:
        raise RuntimeError(f"PPM holds {len(pixels)} bytes, need {total * stride}")
    counts = {}
    for i in range(total):
        px = pixels[i * stride:i * stride + 3]
        counts[px] = counts.get(px, 0) + 1
    modal = max(counts.values())
    return len(counts), 1.0 - (modal / float(total))


def qemu_command(arch, image, log, sock, vars_copy):
    if arch == "x86_64":
        return [os.environ.get("QEMU_DISK", "qemu-system-x86_64")] + firmware_args() + accel_args() + [
            "-drive", f"file={image},format=raw,if=ide,index=0",
            "-boot", "c", "-vga", "std", "-m", "512M",
            "-display", "none", "-no-reboot",
            "-serial", f"file:{log}",
            "-monitor", f"unix:{sock},server=on,wait=off",
        ]

    firmware = aarch64_firmware()
    if firmware is None:
        raise RuntimeError("no AAVMF/EDK2 aarch64 firmware found "
                           "(install qemu-efi-aarch64 or set FLAIROS_AAVMF)")
    code, varsf = firmware
    cmd = ([os.environ.get("QEMU_AARCH64", "qemu-system-aarch64")] + firmware_args()
           + accel_args() + [
        "-M", "virt", "-cpu", "max", "-m", "1024M", "-device", "ramfb",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
    ])
    if varsf and vars_copy:
        cmd += ["-drive", f"if=pflash,format=raw,unit=1,file={vars_copy}"]
    cmd += [
        "-drive", f"file={image},format=raw,if=none,id=hd0",
        "-device", "virtio-blk-pci,drive=hd0,bootindex=1",
        "-boot", "order=c",
        "-display", "none", "-no-reboot",
        "-serial", f"file:{log}",
        "-monitor", f"unix:{sock},server=on,wait=off",
    ]
    return cmd


def verify(image, arch, preset, width, height, timeout):
    if not os.path.exists(image):
        return False, f"{preset}/{arch}: missing disk image {image}"

    work = os.path.dirname(os.path.abspath(image))
    log = os.path.join(work, f"disk-boot-{arch}.log")
    shot = os.path.join(work, f"disk-boot-{arch}.ppm")
    sock = os.path.join(work, f"disk-boot-{arch}.monitor.sock")
    for path in (log, shot, sock):
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass

    vars_copy = None
    try:
        cmd = qemu_command(arch, image, log, sock, None)
    except RuntimeError as exc:
        return False, f"{preset}/{arch}: {exc}"

    if arch == "aarch64":
        firmware = aarch64_firmware()
        varsf = firmware[1] if firmware else None
        if varsf:
            tmp = tempfile.NamedTemporaryFile(prefix="aavmf-vars-", suffix=".fd", delete=False)
            tmp.close()
            shutil.copyfile(varsf, tmp.name)
            vars_copy = tmp.name
            cmd = qemu_command(arch, image, log, sock, vars_copy)

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env=qemu_env())

    marker = f"render=ok {width}x{height}"
    deadline = time.time() + timeout
    rendered = False
    output = ""
    while time.time() < deadline:
        time.sleep(0.25)
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
        if vars_copy:
            os.unlink(vars_copy)
        tail = "\n".join(output.strip().splitlines()[-6:])
        return False, (f"{preset}/{arch}: never reached '{marker}' in {int(timeout)}s\n"
                       f"    serial tail:\n    {tail.replace(chr(10), chr(10) + '    ')}\n"
                       f"    qemu: {(out or b'').decode(errors='replace').strip()[:300]}")

    if f"arch={arch} preset={preset}" not in output:
        proc.terminate()
        if vars_copy:
            os.unlink(vars_copy)
        return False, f"{preset}/{arch}: kernel log does not identify the preset"

    monitor(sock, f"screendump {shot}", wait=0.6)
    alive = proc.poll() is None
    monitor(sock, "quit", wait=0.2)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    if vars_copy:
        os.unlink(vars_copy)

    if not alive:
        return False, f"{preset}/{arch}: guest died before the screendump"
    if not os.path.exists(shot):
        return False, f"{preset}/{arch}: monitor produced no screendump"

    pw, ph, pixels = read_ppm(shot)
    if (pw, ph) != (width, height):
        return False, f"{preset}/{arch}: screendump is {pw}x{ph}, preset is {width}x{height}"
    colours, varied = framebuffer_report(pixels, pw, ph)
    if colours < 32 or varied < 0.02:
        return False, (f"{preset}/{arch}: framebuffer looks blank "
                       f"({colours} colours, {varied * 100:.1f}% non-modal pixels)")

    return True, (f"{preset}/{arch}: disk boot -> render=ok {width}x{height}, "
                  f"screendump {pw}x{ph}, {colours} colours, "
                  f"{varied * 100:.1f}% non-modal pixels")


def image_for(preset, arch):
    return os.path.join(ROOT, "build-presets", f"{preset}-{arch}",
                        f"flairos-{preset}-{arch}.img")


def main(argv):
    presets = []
    archs = list(ARCHES)
    image = name = dims = None
    timeout = None

    i = 1
    while i < len(argv):
        arg = argv[i]
        if arg in ("--arch", "--image", "--name", "--dims", "--timeout"):
            if i + 1 >= len(argv):
                print(f"{arg} needs a value")
                return 2
            value = argv[i + 1]
            i += 2
            if arg == "--arch":
                archs = [value]
            elif arg == "--image":
                image = value
            elif arg == "--name":
                name = value
            elif arg == "--dims":
                dims = value
            else:
                timeout = float(value)
            continue
        if arg.startswith("--"):
            print(f"unknown option: {arg}")
            return 2
        presets.append(arg)
        i += 1

    for arch in archs:
        if arch not in ARCHES:
            print(f"unknown arch: {arch}")
            return 2

    results = []
    if image:
        if not name or not dims:
            print("--image needs --name and --dims WxH")
            return 2
        w, h = (int(x) for x in dims.lower().split("x"))
        arch = archs[0]
        limit = timeout if timeout is not None else (240 if arch == "aarch64" else 120)
        ok, message = verify(image, arch, name, w, h, limit)
        print(("[ok]   " if ok else "[FAIL] ") + message)
        return 0 if ok else 1

    for arch in archs:
        for preset in (presets or PRESETS):
            if preset not in PRESETS:
                print(f"unknown preset: {preset}")
                return 2
            width, height = preset_dims(preset)
            limit = timeout if timeout is not None else (240 if arch == "aarch64" else 120)
            ok, message = verify(image_for(preset, arch), arch, preset, width, height, limit)
            print(("[ok]   " if ok else "[FAIL] ") + message)
            results.append(ok)

    failed = results.count(False)
    if failed:
        print(f"\n{failed} of {len(results)} disk boots failed")
        return 1
    print(f"\nall {len(results)} disk boots reached render=ok with a matching framebuffer")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
