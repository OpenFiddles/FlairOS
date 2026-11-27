#!/usr/bin/env python3
"""Prove the disk-boot gate can fail.

The verifier is only worth gating CI on if it rejects damaged media and a boot
that never renders. This drives tools/verify_disk_boot.py in probe mode against
mutated copies of a real preset image and requires a nonzero exit for each:

  1. a missing disk image
  2. a truncated image (the kernel region is not on disk any more)
  3. a corrupted kernel region (the loader reads garbage and the kernel never runs)
  4. a kernel whose entry never returns to the render loop (timeout path)

It also runs the untouched image once to show the same command passes, so a
"everything fails" verifier cannot pass this test by accident.

    python3 tests/disk_boot_gate_test.py [--arch x86_64]
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERIFY = os.path.join(ROOT, "tools", "verify_disk_boot.py")
SECTOR = 512
KERNEL_LBA = 16          # matches tools/make_bootdisk.py
ENTRY_OFFSET = 0x30      # _start follows the 48-byte Multiboot header


def run_verifier(image, name, dims, timeout):
    cmd = [sys.executable, VERIFY, "--image", image, "--name", name,
           "--dims", dims, "--arch", "x86_64", "--timeout", str(timeout)]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return res.returncode, res.stdout.strip()


def main():
    preset, arch = "desktop", "x86_64"
    dims = "1024x768"
    src = os.path.join(ROOT, "build-presets", f"{preset}-{arch}",
                       f"flairos-{preset}-{arch}.img")
    if not os.path.exists(src):
        print(f"missing {src}; run `make presets` first")
        return 2

    work = tempfile.mkdtemp(prefix="disk-gate-")
    failures = 0

    def check(label, expect_ok, code, out):
        """Require the exit code, and print what the verifier said either way.

        The exit code alone does not show that the case was really exercised,
        so the verifier's own output goes into the log under every case: which
        QEMU and accelerator booted it, and for a rejection the message that
        made it reject the image.
        """
        nonlocal failures
        good = (code == 0) == expect_ok
        print(f"[{'ok' if good else 'FAIL'}] {label}: verifier exit {code} "
              f"({'expected pass' if expect_ok else 'expected failure'})")
        text = out.strip()
        if len(text) > 1200:
            text = text[:1200].rstrip() + " ..."
        for line in text.splitlines():
            print("    " + line)
        if not good:
            failures += 1

    # 0. control: the untouched image must pass (generous, since CI runs TCG
    # on a shared runner; the broken images below must still never pass)
    code, out = run_verifier(src, preset, dims, 180)
    check("intact image", True, code, out)

    # 1. missing image
    code, out = run_verifier(os.path.join(work, "absent.img"), preset, dims, 20)
    check("missing disk image", False, code, out)

    # 2. truncated image: keep the boot sector and loader, drop the kernel
    trunc = os.path.join(work, "truncated.img")
    with open(src, "rb") as f:
        head = f.read(KERNEL_LBA * SECTOR)
    with open(trunc, "wb") as f:
        f.write(head)
    code, out = run_verifier(trunc, preset, dims, 25)
    check("truncated kernel region", False, code, out)

    # 3. corrupted kernel region: same size, garbage content
    corrupt = os.path.join(work, "corrupt.img")
    data = bytearray(open(src, "rb").read())
    for i in range(KERNEL_LBA * SECTOR, len(data)):
        data[i] = 0xFF
    open(corrupt, "wb").write(data)
    code, out = run_verifier(corrupt, preset, dims, 25)
    check("corrupted kernel region", False, code, out)

    # 4. a kernel that never reaches the render loop: patch the entry with `jmp .`
    hang = os.path.join(work, "hang.img")
    data = bytearray(open(src, "rb").read())
    entry = KERNEL_LBA * SECTOR + ENTRY_OFFSET
    data[entry:entry + 2] = b"\xeb\xfe"
    open(hang, "wb").write(data)
    code, out = run_verifier(hang, preset, dims, 8)
    check("render never completes", False, code, out)

    shutil.rmtree(work, ignore_errors=True)
    if failures:
        print(f"\n{failures} gate expectations failed")
        return 1
    print("\ngate rejects every broken image and accepts the intact one")
    return 0


if __name__ == "__main__":
    sys.exit(main())
