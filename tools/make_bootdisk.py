#!/usr/bin/env python3
"""Lay out a BIOS-bootable disk image for the 64-bit kernel.

    [LBA 0]        boot sector   (tools/bootdisk/stage1.asm, 512 bytes)
    [LBA 1..8]     stage-2 loader(tools/bootdisk/stage2.asm, 4 KiB)
    [LBA 16..]     flat kernel image

The loader reads the kernel with the BIOS extended disk interface, so the
sectors between the loader and the kernel start are unused padding. The kernel
LBA and sector count are patched into the loader's F64HDR00 header here, which
keeps the layout in one place: this file.

No partition table: the BIOS loads LBA 0 as a boot sector, which is exactly what
this image wants. Usage:

    python3 tools/make_bootdisk.py <kernel-flat.bin> <out.img> --entry 0x100030
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOOTDISK = os.path.join(ROOT, "tools", "bootdisk")

STAGE2_LBA = 1
STAGE2_SECTORS = 8
KERNEL_LBA = 16
SECTOR = 512
HEADER_MAGIC = b"F64HDR00"


def assemble(src, out):
    nasm = os.environ.get("NASM", "nasm")
    res = subprocess.run([nasm, "-f", "bin", src, "-o", out],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if res.returncode != 0:
        print(res.stdout)
        raise RuntimeError(f"nasm failed on {src}")


def build_image(flat_path, out_path, build_dir, entry_addr):
    os.makedirs(build_dir, exist_ok=True)
    stage1 = os.path.join(build_dir, "stage1.bin")
    stage2 = os.path.join(build_dir, "stage2.bin")
    assemble(os.path.join(BOOTDISK, "stage1.asm"), stage1)
    assemble(os.path.join(BOOTDISK, "stage2.asm"), stage2)

    stage1_bytes = open(stage1, "rb").read()
    stage2_bytes = open(stage2, "rb").read()
    kernel = open(flat_path, "rb").read()

    if len(stage1_bytes) != SECTOR:
        raise RuntimeError(f"boot sector is {len(stage1_bytes)} bytes, expected {SECTOR}")
    if len(stage2_bytes) > STAGE2_SECTORS * SECTOR:
        raise RuntimeError(f"loader is {len(stage2_bytes)} bytes, limit {STAGE2_SECTORS * SECTOR}")
    if not kernel:
        raise RuntimeError("kernel flat image is empty")
    if kernel[:4] != b"\x7fELF" and kernel[:1] == b"":
        raise RuntimeError("kernel image looks empty")

    kernel_sectors = (len(kernel) + SECTOR - 1) // SECTOR
    # The loader stages the kernel at 0x10000 and copies it to 1 MiB, so the
    # staging buffer must stay below the 640 KiB hole.
    if 0x10000 + kernel_sectors * SECTOR >= 0xA0000:
        raise RuntimeError("kernel image too large for the 16-bit staging buffer")

    patched = bytearray(stage2_bytes)
    pos = patched.find(HEADER_MAGIC)
    if pos < 0:
        raise RuntimeError("loader is missing its F64HDR00 header")
    if not 0x100000 <= entry_addr < 0x100000 + kernel_sectors * SECTOR:
        raise RuntimeError(f"kernel entry 0x{entry_addr:x} is outside the loaded image")
    struct.pack_into("<III", patched, pos + len(HEADER_MAGIC),
                     KERNEL_LBA, kernel_sectors, entry_addr)

    image = bytearray(b"\x00" * (KERNEL_LBA * SECTOR))
    image[0:SECTOR] = stage1_bytes
    stage2_off = STAGE2_LBA * SECTOR
    image[stage2_off:stage2_off + len(patched)] = patched
    image.extend(kernel)
    pad = (-len(image)) % SECTOR
    image.extend(b"\x00" * pad)

    open(out_path, "wb").write(image)
    return len(image), kernel_sectors


def main():
    if len(sys.argv) not in (3, 5):
        print(__doc__)
        return 2
    flat_path, out_path = sys.argv[1], sys.argv[2]
    entry = int(sys.argv[4], 0) if len(sys.argv) == 5 else 0x100000
    build_dir = os.path.join(os.path.dirname(os.path.abspath(out_path)), "bootdisk")
    size, sectors = build_image(flat_path, out_path, build_dir, entry)
    print(f"[OK] disk image: {out_path} ({size} bytes, kernel {sectors} sectors "
          f"at LBA {KERNEL_LBA}, entry 0x{entry:x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
