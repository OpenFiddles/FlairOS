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
stage2 = source("bootloader/stage2.asm")
bootinfo = source("bootloader/bootinfo.inc")
images = source("bootloader/loader/images.inc")
fs = source("include/fs.h")
fs_kernel = source("kernel/fs/fs.c")
airp = source("include/airp.h")
vm = source("drivers/vm.c")
pmm = source("drivers/pmm.c")
desktop = source("kernel/ui/desktop.c")
kernel = source("kernel/core/kernel.c")
linker = source("kernel/boot/link.ld")
makefile = source("Makefile")

def number(pattern, text, label):
    match = re.search(pattern, text, re.MULTILINE)
    assert match, f"missing {label}"
    return int(match.group(1), 0)

loader_consts = {}
for name, value in re.findall(r"^([A-Z0-9_]+)\s+equ\s+(\w+)",
                              bootinfo + "\n" + images, re.MULTILINE):
    try:
        loader_consts[name] = int(value, 0)
    except ValueError:
        pass


def table_rows(text):
    """Parse the loader's image table into (lba, segment, sectors, flags)."""
    rows = []
    for line in text[text.index("image_table:"):].splitlines()[1:]:
        line = line.split(";")[0].strip()
        if not line.startswith("dw "):
            continue
        fields = [f.strip() for f in line[3:].split(",")]
        if len(fields) != 4:
            continue
        rows.append(tuple(loader_consts[f] if f in loader_consts else int(f, 0)
                          for f in fields))
    return rows


assert loader_consts["KERNEL_LBA"] == 7 and loader_consts["KERNEL_SECTORS"] == 256
assert number(r"STAGE2_SECTORS\s+equ\s+(\d+)", stage, "loader sectors") == 6
assert number(r"APP_SECTORS\s+equ\s+(\d+)", images, "boot app sectors") == 128
explorer_first = number(r"EXPLORER_LBA\s+equ\s+(\d+)", images, "Explorer start LBA")
explorer_sectors = number(r"EXPLORER_SECTORS\s+equ\s+(\d+)", images, "Explorer sectors")
terminal_first = number(r"TERMINAL_LBA\s+equ\s+(\d+)", images, "Terminal start LBA")
terminal_sectors = number(r"TERMINAL_SECTORS\s+equ\s+(\d+)", images, "Terminal sectors")
assert explorer_first == 2447 and explorer_sectors == 32
assert terminal_first == explorer_first + explorer_sectors and terminal_sectors == 64
settings_first = number(r"SETTINGS_LBA\s+equ\s+(\d+)", images, "Settings start LBA")
settings_sectors = number(r"SETTINGS_SECTORS\s+equ\s+(\d+)", images, "Settings sectors")
notes_first = number(r"NOTES_LBA\s+equ\s+(\d+)", images, "Notes start LBA")
notes_sectors = number(r"NOTES_SECTORS\s+equ\s+(\d+)", images, "Notes sectors")
assert settings_first == terminal_first + terminal_sectors and settings_sectors == 32
assert notes_first == settings_first + settings_sectors and notes_sectors == 16
assert number(r"PYTHON_SECTORS\s+equ\s+(\d+)", images, "Python sectors") == 257

# Stage 2 owns the image table: kernel first, then the staged app packages.
assert table_rows(images) == [
    (7, 0x1000, 256, loader_consts["IMG_KERNEL"]),
    (263, 0x4000, 128, 0),
    (2447, 0x9000, 32, 0),
    (2479, 0x3800, 64, 0),
    (2543, 0x3000, 32, 0),
    (2575, 0x3400, 16, 0),
    (2591, 0x9400, 16, 0),
    (2623, 0x6000, 257, loader_consts["IMG_OPTIONAL"]),
]
assert "%include \"loader/images.inc\"" in stage2
assert "call load_images" in stage2
assert "call build_boot_info" in stage2
assert "verify_kernel" in images
assert "KERNEL_MAGIC" in images
# The boot protocol and its C mirror must describe the same block.
assert "BOOT_INFO_ADDR    equ 0x4000" in bootinfo
assert "BOOT_INFO_VERSION equ 2" in bootinfo
assert "BI_CHECKSUM     equ 60" in bootinfo and "BI_SIZE         equ 64" in bootinfo
boot_h = source("include/boot.h")
assert "FLAIR_BOOT_INFO_ADDR 0x4000u" in boot_h
assert "FLAIR_BOOT_INFO_VERSION 2u" in boot_h

fs_dir = number(r"#define FS_DIR_LBA\s+(\d+)", fs, "filesystem directory LBA")
files = number(r"#define FS_FILES\s+(\d+)", fs, "filesystem file count")
file_max = number(r"#define FS_FILE_MAX\s+(\d+)", fs, "filesystem file max")
image_max = number(r"#define AIRP_IMAGE_MAX\s+(\d+)", airp, "AIRP image max")
slot_size = number(r"#define AIRP_SLOT_SIZE\s+(\d+)", airp, "AIRP slot size")
app_lo = number(r"#define APP_LO\s+(0x[0-9A-Fa-f]+)", vm, "app lower bound")
app_hi = number(r"#define APP_HI\s+(0x[0-9A-Fa-f]+)", vm, "app upper bound")
assert image_max == 131072
assert slot_size == 262144 == app_hi - app_lo
# The filesystem lives on the primary ATA data disk, not inside the
# 1.44 MiB boot floppy. Its 16 fixed extents are 3600 sectors each.
assert file_max == 1843200 and files == 16 and fs_dir == 398
fs_file_sectors = file_max // 512
fs_data_first = fs_dir + 1
fs_data_end = fs_data_first + files * fs_file_sectors
assert fs_file_sectors == 3600 and fs_data_first == 399
assert fs_data_end == 57999
assert "#define FSK_DATA_LBA   (FS_DIR_LBA + 1)" in fs_kernel
assert "#define FSK_FILE_SECTS (FS_FILE_MAX / ATA_SECTOR_SIZE)" in fs_kernel
assert "return ata_sector_count() >= fsk_file_lba(FS_FILES);" in fs_kernel
python_first = 2623
python_last = python_first + 257 - 1
explorer_last = explorer_first + explorer_sectors - 1
terminal_last = terminal_first + terminal_sectors - 1
settings_last = settings_first + settings_sectors - 1
notes_last = notes_first + notes_sectors - 1
assert fs_data_end > 2880  # Separate ATA data disk; boot image is a 2880-sector floppy.
assert explorer_last + 1 == terminal_first
assert terminal_last + 1 == settings_first
assert settings_last + 1 == notes_first
assert notes_last < python_first
assert python_last == 2879 and python_last < 2880
assert "seek=2447 count=32" in makefile
assert "seek=2479 count=64" in makefile
assert "seek=2543 count=32" in makefile
assert "seek=2575 count=16" in makefile
assert "seek=2623 count=257" in makefile

avail = number(r"#define AVAIL_BITS\s+\(\(unsigned char \*\)(0x[0-9A-Fa-f]+)u\)", pmm, "available bitmap")
managed = number(r"#define MANAGED_BITS\s+\(\(unsigned char \*\)(0x[0-9A-Fa-f]+)u\)", pmm, "managed bitmap")
reserved = number(r"#define RESERVED_BITS\s+\(\(unsigned char \*\)(0x[0-9A-Fa-f]+)u\)", pmm, "reserved bitmap")
frame_count = (0x10000000 - 0x00100000) // 4096
bitmap_bytes = frame_count // 8
assert avail == 0x80000
assert avail + bitmap_bytes <= managed
assert managed + bitmap_bytes <= reserved
assert reserved + bitmap_bytes <= 0x86000
def stage_address(name):
    return number(rf"#define {name}_STAGE\s+\(\(const u8 \*\)(0x[0-9A-Fa-f]+)\)",
                  desktop, f"{name} staging address")

def stage_sectors(name):
    return number(rf"#define {name}_STAGE_LEN\s+\((\d+)u \* 512u\)",
                  desktop, f"{name} staging size")

settings_base, notes_base = stage_address("SETTINGS"), stage_address("NOTES")
explorer_base, terminal_base = stage_address("EXPLORER"), stage_address("TERMINAL")
settings_bytes = stage_sectors("SETTINGS") * 512
notes_bytes = stage_sectors("NOTES") * 512
explorer_bytes = stage_sectors("EXPLORER") * 512
terminal_bytes = stage_sectors("TERMINAL") * 512
assert settings_base == 0x30000
assert notes_base == settings_base + settings_bytes == 0x34000
assert explorer_base == 0x90000 and explorer_base + explorer_bytes <= 0xA0000
assert terminal_base == 0x38000
assert terminal_base + terminal_bytes == app_lo == 0x40000
assert "0x7F000" in desktop
assert "micropython_stage_init();" in kernel
assert "pmm_alloc_contig(frames)" in kernel
assert "__bss_end <= 0x30000" in linker

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
