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
import platform
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Every shipped preset is gated. The 4K frames need a VGA with enough video
# memory (see qemu_command); with the default 16 MiB the std VGA reports a
# truncated 32x1092 capture, which is why the boot below sets vgamem_mb.
PRESETS = ("desktop", "desktop800", "desktop1080", "desktop4k", "mobile",
           "mobileland", "template", "tv", "tv4k")
# Presets whose shell plays the Airnimation preset showcase on startup. Their
# boot must show changing frames during the animation and a settled final one.
ANIMATED_PRESETS = ("desktop", "desktop800", "desktop1080", "desktop4k",
                    "mobile", "mobileland")
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


def accel_name(arch="x86_64"):
    """The accelerator the boots run under, exactly as it is passed to QEMU.

    A machine can expose /dev/kvm and still have to boot under TCG (shared CI
    runners), so the caller can force one and the log then says which one ran.
    KVM is only used for the host's own architecture: an aarch64 guest on an
    x86_64 host must run TCG, and asking QEMU for kvm there is a hard error.
    """
    forced = os.environ.get("FLAIROS_QEMU_ACCEL")
    if forced:
        return forced
    host = platform.machine().lower()
    host_aarch64 = host in ("aarch64", "arm64")
    if arch == "aarch64" and not host_aarch64:
        return "tcg"
    if arch == "x86_64" and host_aarch64:
        return "tcg"
    if os.access("/dev/kvm", os.R_OK | os.W_OK):
        return "kvm"
    return "tcg"


def accel_args(arch="x86_64"):
    return ["-accel", accel_name(arch)]


def qemu_binary(arch):
    if arch == "x86_64":
        return os.environ.get("QEMU_DISK", "qemu-system-x86_64")
    return os.environ.get("QEMU_AARCH64", "qemu-system-aarch64")


_QEMU = {}


def qemu_identity(arch):
    """(binary, version) of the emulator that will be run, so the log names it."""
    if arch not in _QEMU:
        binary = qemu_binary(arch)
        try:
            out = subprocess.run([binary, "--version"], stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, env=qemu_env(),
                                 text=True, timeout=30).stdout
            version = out.strip().splitlines()[0] if out.strip() else "unknown"
        except (OSError, subprocess.SubprocessError) as exc:
            version = f"unavailable ({exc})"
        _QEMU[arch] = (binary, version.replace("QEMU emulator version ", ""))
    return _QEMU[arch]


def boot_banner(arch):
    """One line naming the emulator, accelerator and firmware a boot will use.

    Printed before every boot so the evidence does not rest on reading this
    file: the log itself says which QEMU, which accelerator and which firmware
    produced the framebuffer below it.
    """
    binary, version = qemu_identity(arch)
    if version.startswith("unavailable"):
        raise RuntimeError(
            f"{binary} is not installed or not runnable ({version}). The disk boot\n"
            "    gate needs QEMU; install qemu-system-x86 / qemu-system-arm, or set\n"
            "    QEMU_DISK / QEMU_AARCH64 to the binary, or put it under ~/local/bin.\n"
            "    Refusing to report a boot that never ran.")
    parts = [f"{binary} ({version})", f"accel={accel_name(arch)}"]
    if arch == "aarch64":
        firmware = aarch64_firmware()
        parts.append("firmware=" + (firmware[0] if firmware else "missing"))
    else:
        extra = firmware_args()
        parts.append("bios=" + (extra[1] if extra else "default"))
    return "disk boot with " + ", ".join(parts)


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


def screendump_sequence(path, shots):
    """Take several screendumps over one monitor connection.

    shots is [(file, seconds_to_wait_after_that_dump), ...]. One connection is
    kept open so the frame timestamps are controlled by the delays here rather
    than by reconnecting for every capture.
    """
    if not os.path.exists(path):
        raise RuntimeError("QEMU monitor socket disappeared")
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect(path)
    time.sleep(0.1)
    try:
        s.recv(65536)
    except Exception:
        pass
    try:
        for shot, delay in shots:
            s.sendall((f"screendump {shot}\n").encode())
            time.sleep(0.10)   # let the dump land before the next command
            if delay:
                time.sleep(delay)
    finally:
        s.close()


def wait_for_output(log, needle, timeout, proc):
    deadline = time.time() + timeout
    while time.time() < deadline:
        output = open(log, errors="replace").read() if os.path.exists(log) else ""
        if needle in output:
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.1)
    return False


def frame_delta(a, b):
    """Fraction of sampled pixels that differ visibly between two captures."""
    aw, ah, ap = read_ppm(a)
    bw, bh, bp = read_ppm(b)
    if (aw, ah) != (bw, bh):
        raise RuntimeError(f"frame sizes differ: {a} is {aw}x{ah}, {b} is {bw}x{bh}")
    total = aw * ah
    step = max(1, total // 200000)
    changed = sampled = 0
    for i in range(0, total, step):
        ao = i * 3
        if (abs(ap[ao] - bp[ao]) > 8 or abs(ap[ao + 1] - bp[ao + 1]) > 8 or
                abs(ap[ao + 2] - bp[ao + 2]) > 8):
            changed += 1
        sampled += 1
    return changed / float(sampled or 1)


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
    """Return (distinct_colours, fraction_not_modal) for the captured surface.

    A 4K frame is 8.3M pixels; counting every one of them in a dict would cost
    hundreds of megabytes for the same verdict, so large surfaces are sampled
    on a uniform stride and the ratios are reported over the sample.
    """
    stride = 3
    total = w * h
    if len(pixels) < total * stride:
        raise RuntimeError(f"PPM holds {len(pixels)} bytes, need {total * stride}")
    step = max(1, total // 200000)
    counts = {}
    sampled = 0
    for i in range(0, total, step):
        px = pixels[i * stride:i * stride + 3]
        counts[px] = counts.get(px, 0) + 1
        sampled += 1
    modal = max(counts.values())
    return len(counts), 1.0 - (modal / float(sampled))


def qemu_command(arch, image, log, sock, vars_copy):
    if arch == "x86_64":
        return [os.environ.get("QEMU_DISK", "qemu-system-x86_64")] + firmware_args() + accel_args(arch) + [
            "-drive", f"file={image},format=raw,if=ide,index=0",
            "-boot", "c", "-vga", "std", "-global", "VGA.vgamem_mb=64",
            "-m", "512M",
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
           + accel_args(arch) + [
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


# Everything that ends up inside a preset image. An image is only accepted when
# it is newer than the newest file under these roots, so an edit to any kernel
# driver, shared component, header, linker script or packing tool that the last
# build predates is reported as stale instead of being booted and trusted. The
# roots are directories rather than a hand-maintained file list because a list
# always lags: a new driver or header would escape it silently.
SOURCE_ROOTS = (
    "arch", "kernel", "include", "ui", "net", "runtime", "graphics",
    "ported", "drivers", "resources",
)
TOOL_INPUTS = (
    "tools/build_preset.py", "tools/airpkg.py", "tools/make_bootdisk.py",
    "tools/make_uefi_disk.py", "tools/bootdisk", "tools/arch", "airp.py",
)
SKIP_DIRS = {".git", "build", "build-presets", "build-apps", "__pycache__", "artifacts"}


def _walk_newest(root, newest, newest_path):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            full = os.path.join(dirpath, name)
            try:
                mtime = os.path.getmtime(full)
            except OSError:
                continue
            if mtime > newest[0]:
                newest[0] = mtime
                newest_path[0] = os.path.relpath(full, ROOT)


def newest_input(preset):
    """(mtime, repo-relative path) of the newest input that image depends on."""
    cfg = json.load(open(os.path.join(ROOT, "presets", preset, "preset.json")))
    newest = [0.0]
    newest_path = [f"presets/{preset}/preset.json"]
    roots = [os.path.join(ROOT, rel) for rel in SOURCE_ROOTS]
    roots.append(os.path.join(ROOT, "presets", preset))
    if cfg.get("session_package"):
        roots.append(os.path.join(ROOT, cfg["session_package"]))
    for root in roots:
        if os.path.isdir(root):
            _walk_newest(root, newest, newest_path)
        elif os.path.exists(root):
            mtime = os.path.getmtime(root)
            if mtime > newest[0]:
                newest[0] = mtime
                newest_path[0] = os.path.relpath(root, ROOT)
    for rel in TOOL_INPUTS:
        full = os.path.join(ROOT, rel)
        if os.path.isdir(full):
            _walk_newest(full, newest, newest_path)
        elif os.path.exists(full):
            mtime = os.path.getmtime(full)
            if mtime > newest[0]:
                newest[0] = mtime
                newest_path[0] = rel
    return newest[0], newest_path[0]


def image_problem(preset, arch, image):
    """Why an image cannot be gated, or None when it is present and current."""
    if not os.path.exists(image):
        return (f"{preset}/{arch}: missing disk image {image} (run `make presets`; "
                "the gate does not build or skip a preset it was asked to gate)")
    newest, newest_path = newest_input(preset)
    if os.path.getmtime(image) < newest:
        return (f"{preset}/{arch}: stale disk image {image} is older than "
                f"{newest_path} (run `make presets`; the gate refuses to boot an "
                "image that does not match the tree)")
    return None


def verify(image, arch, preset, width, height, timeout):
    problem = image_problem(preset, arch, image)
    if problem:
        return False, problem

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
        hint = ""
        if width > 2560 or height > 1600:
            hint = ("\n    a 4K mode needs more display video memory than the VGA "
                    "default; check the -global VGA.vgamem_mb=64 argument")
        return False, (f"{preset}/{arch}: never reached '{marker}' in {int(timeout)}s{hint}\n"
                       f"    serial tail:\n    {tail.replace(chr(10), chr(10) + '    ')}\n"
                       f"    qemu: {(out or b'').decode(errors='replace').strip()[:300]}")

    if f"arch={arch} preset={preset}" not in output:
        proc.terminate()
        if vars_copy:
            os.unlink(vars_copy)
        return False, f"{preset}/{arch}: kernel log does not identify the preset"

    # The kernel reports the orientation its preset declares; it must agree
    # with the framebuffer geometry and with the preset's own define.
    want_orientation = "portrait" if height > width else "landscape"
    if f"flairos64: orientation={want_orientation}" not in output:
        proc.terminate()
        if vars_copy:
            os.unlink(vars_copy)
        return False, (f"{preset}/{arch}: expected orientation={want_orientation} for "
                       f"{width}x{height}, kernel log does not report it")

    # A session whose preset declares a package must report that the kernel read
    # it. Without this a shell could render on its built-in fallback and still
    # pass the framebuffer check.
    packaged = preset_package(preset)
    if packaged and "session=pkg-load ok" not in output:
        proc.terminate()
        if vars_copy:
            os.unlink(vars_copy)
        wanted = f"session=pkg-load ok tiles="
        return False, (f"{preset}/{arch}: preset declares the {packaged} session package, "
                       f"but the kernel log has no '{wanted}...' line")

    anim_frames = []
    settled = None
    if preset in ANIMATED_PRESETS:
        # Capture at the kernel's own animation phase markers. This is a
        # timestamp, not a wall-clock guess, so it works whether the host's
        # timer runs at 100 Hz or faster.
        settled = os.path.join(work, f"disk-boot-{arch}-settled.ppm")
        for path in (shot, settled):
            try:
                os.unlink(path)
            except FileNotFoundError:
                pass
        # Capture successive animation frames as fast as the monitor allows
        # until the kernel reports its resting phase. This does not assume a
        # particular timer rate, so it works under KVM and TCG alike.
        deadline = time.time() + 20
        while time.time() < deadline and proc.poll() is None:
            path = os.path.join(work, f"disk-boot-{arch}-anim{len(anim_frames)}.ppm")
            anim_frames.append(path)
            monitor(sock, f"screendump {path}", wait=0.2)
            output = open(log, errors="replace").read() if os.path.exists(log) else ""
            if "flairos64: anim=rest" in output:
                break
            time.sleep(0.12)
        if "flairos64: anim=rest" not in (open(log, errors="replace").read()
                                           if os.path.exists(log) else ""):
            return False, f"{preset}/{arch}: animation never reached its resting frame"
        time.sleep(0.4)
        monitor(sock, f"screendump {shot}", wait=0.5)
        time.sleep(0.3)
        monitor(sock, f"screendump {settled}", wait=0.3)
    else:
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
        detail = ""
        if width > 2560 or height > 1600:
            detail = (" — the requested mode does not fit the video memory QEMU gave the "
                      "display, so the capture was truncated. A 4K preset needs the "
                      "-global VGA.vgamem_mb=64 argument qemu_command supplies")
        return False, (f"{preset}/{arch}: unusable capture, screendump is {pw}x{ph}, "
                       f"preset is {width}x{height}{detail}")
    colours, varied = framebuffer_report(pixels, pw, ph)
    if colours < 32 or varied < 0.02:
        return False, (f"{preset}/{arch}: unusable capture, framebuffer looks blank "
                       f"({colours} colours, {varied * 100:.1f}% non-modal pixels); "
                       "the preset was accepted only if its own resolution renders")

    anim_evidence = ""
    if preset in ANIMATED_PRESETS:
        missing = [p for p in anim_frames + [settled] if not os.path.exists(p)]
        if missing:
            return False, f"{preset}/{arch}: animation capture missing: {missing}"
        if len(anim_frames) < 2:
            return False, f"{preset}/{arch}: only {len(anim_frames)} animation frame(s) captured"
        deltas = [frame_delta(anim_frames[i], anim_frames[i + 1])
                  for i in range(len(anim_frames) - 1)]
        peak = max(deltas)
        settle = frame_delta(shot, settled)
        if peak < 0.002:
            return False, (f"{preset}/{arch}: Airnimation frames never changed "
                           f"(peak {peak * 100:.2f}% across {len(anim_frames)} frames); "
                           "the booted image is not playing the preset showcase")
        if settle > 0.005:
            return False, (f"{preset}/{arch}: animation did not settle "
                           f"(final frames differ by {settle * 100:.2f}%)")
        anim_evidence = (f", anim {len(anim_frames)} frames peak delta {peak * 100:.1f}%, "
                         f"resting frames settled {settle * 100:.2f}%")

    return True, (f"{preset}/{arch}: disk boot -> render=ok {width}x{height} "
                  f"{want_orientation}, screendump {pw}x{ph}, {colours} colours, "
                  f"{varied * 100:.1f}% non-modal pixels{anim_evidence}, "
                  f"accel={accel_name(arch)}")


def preset_package(preset):
    """The session package a preset declares, or None when it ships none."""
    path = os.path.join(ROOT, "presets", preset, "preset.json")
    try:
        return json.load(open(path)).get("session_package")
    except (OSError, ValueError):
        return None


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
        limit = timeout if timeout is not None else (240 if arch == "aarch64" else 180)
        print(boot_banner(arch))
        ok, message = verify(image, arch, name, w, h, limit)
        print(("[ok]   " if ok else "[FAIL] ") + message)
        return 0 if ok else 1

    for arch in archs:
        try:
            print(boot_banner(arch))
        except RuntimeError as exc:
            print(f"[FAIL] {exc}")
            return 1
        for preset in (presets or PRESETS):
            if preset not in PRESETS:
                print(f"unknown preset: {preset}")
                return 2
            width, height = preset_dims(preset)
            limit = timeout if timeout is not None else (240 if arch == "aarch64" else 180)
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
    try:
        sys.exit(main(sys.argv))
    except RuntimeError as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        sys.exit(1)
