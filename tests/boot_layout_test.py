#!/usr/bin/env python3
"""Guard the boot-sector, disk, and app-memory layout shared by FlareOS."""
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text()


stage = source("bootloader/stage1.asm")
fs = source("include/fs.h")
airp = source("include/airp.h")
vm = source("drivers/vm.c")
pmm = source("drivers/pmm.c")
desktop = source("kernel/desktop.c")
kernel = source("kernel/kernel.c")
makefile = source("Makefile")

def number(pattern, text, label):
    match = re.search(pattern, text, re.MULTILINE)
    assert match, f"missing {label}"
    return int(match.group(1), 0)

assert number(r"APP_SECTORS\s+equ\s+(\d+)", stage, "boot app sectors") == 128
assert number(r"EXPLORER_SECTORS\s+equ\s+(\d+)", stage, "Explorer sectors") == 9
assert number(r"PYTHON_SECTORS\s+equ\s+(\d+)", stage, "Python sectors") == 257
assert "mov word [lba], 389" in stage
assert "mov word [lba], 2623" in stage
assert "cmp dword [es:0], 0x50524941" in stage
assert "mov ax, 0x3400" in stage and "mov ax, 0x6000" in stage

fs_dir = number(r"#define FS_DIR_LBA\s+(\d+)", fs, "filesystem directory LBA")
files = number(r"#define FS_FILES\s+(\d+)", fs, "filesystem file count")
file_max = number(r"#define FS_FILE_MAX\s+(\d+)", fs, "filesystem file max")
image_max = number(r"#define AIRP_IMAGE_MAX\s+(\d+)", airp, "AIRP image max")
slot_size = number(r"#define AIRP_SLOT_SIZE\s+(\d+)", airp, "AIRP slot size")
app_lo = number(r"#define APP_LO\s+(0x[0-9A-Fa-f]+)", vm, "app lower bound")
app_hi = number(r"#define APP_HI\s+(0x[0-9A-Fa-f]+)", vm, "app upper bound")
assert image_max == 131072
assert slot_size == 262144 == app_hi - app_lo
assert file_max == 65536 and files == 16 and fs_dir == 398
fs_last = fs_dir + 1 + files * (file_max // 512) - 1
python_first = 2623
python_last = python_first + 257 - 1
assert fs_last == 2446 and python_first > fs_last
assert python_last == 2879 and python_last < 2880
assert "seek=2623 count=257" in makefile

avail = number(r"#define AVAIL_BITS\s+\(\(unsigned char \*\)(0x[0-9A-Fa-f]+)u\)", pmm, "available bitmap")
managed = number(r"#define MANAGED_BITS\s+\(\(unsigned char \*\)(0x[0-9A-Fa-f]+)u\)", pmm, "managed bitmap")
frame_count = (0x10000000 - 0x00100000) // 4096
bitmap_bytes = frame_count // 8
assert avail + bitmap_bytes <= managed
assert managed + bitmap_bytes <= 0x34000
assert "0x34000" in desktop and "0x7F000" in desktop
assert "micropython_stage_init();" in kernel
assert "pmm_alloc_contig(frames)" in kernel

# Exercise the exact maximum AIRP image size and reject the first byte over it.
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    image = bytearray(image_max)
    struct.pack_into("<II", image, 0, 0x54505041, 0x40008)
    src, dst = tmp / "max.bin", tmp / "max.airp"
    src.write_bytes(image)
    subprocess.run([sys.executable, str(ROOT / "tools/airpack.py"),
                    str(src), str(dst), "micropython"], check=True,
                   stdout=subprocess.DEVNULL)
    assert dst.stat().st_size == 36 + image_max
    src.write_bytes(image + b"x")
    rejected = subprocess.run([sys.executable, str(ROOT / "tools/airpack.py"),
                               str(src), str(dst), "micropython"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    assert rejected.returncode != 0

print("boot layout and 128 KiB AIRP boundary checks passed")
