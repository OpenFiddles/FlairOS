#!/usr/bin/env python3
"""Validate El Torito boot images and Live-session safety wiring."""
import pathlib
import struct
import sys

def check_iso(path):
    data = pathlib.Path(path).read_bytes()
    assert len(data) >= 20 * 2048, f"{path}: ISO is too small"
    pvd = data[16 * 2048:17 * 2048]
    assert pvd[:7] == b"\x01CD001\x01", f"{path}: missing ISO primary volume descriptor"
    boot = data[17 * 2048:18 * 2048]
    assert boot[:7] == b"\x00CD001\x01", f"{path}: missing El Torito boot record"
    assert b"EL TORITO SPECIFICATION" in boot[7:39], f"{path}: not an El Torito boot record"
    catalog_lba = struct.unpack_from("<I", boot, 71)[0]
    catalog = data[catalog_lba * 2048:(catalog_lba + 1) * 2048]
    assert len(catalog) == 2048, f"{path}: boot catalog outside ISO"
    assert catalog[0] == 1 and catalog[30:32] == b"\x55\xAA", f"{path}: invalid catalog validation entry"
    assert sum(struct.unpack_from("<16H", catalog, 0)) & 0xFFFF == 0, f"{path}: catalog checksum mismatch"
    entry = catalog[32:64]
    assert entry[0] == 0x88, f"{path}: default boot entry is not bootable"
    image_lba = struct.unpack_from("<I", entry, 8)[0]
    boot_image = data[image_lba * 2048:image_lba * 2048 + 512]
    assert len(boot_image) == 512 and boot_image[510:512] == b"\x55\xAA", f"{path}: invalid floppy-boot image"

assert len(sys.argv) == 3, "usage: boot_media_test.py LIVE.iso BOOT.iso"
check_iso(sys.argv[1])
check_iso(sys.argv[2])
root = pathlib.Path(__file__).resolve().parents[1]
stage2 = (root / "legacy/i386/bootloader/stage2.asm").read_text()
bootinfo = (root / "legacy/i386/bootloader/bootinfo.inc").read_text()
entry_asm = (root / "kernel/boot/entry.asm").read_text()
boot_c = (root / "kernel/boot/boot.c").read_text()
boot_h = (root / "include/boot.h").read_text()
fs_c = (root / "kernel/fs/fs.c").read_text()
ata_c = (root / "drivers/drv_ata.c").read_text()
kernel_c = (root / "kernel/core/kernel.c").read_text()
fs_h = (root / "include/fs.h").read_text()
web = (root / "web/index.html").read_text()
assert "FORCE_BOOT_MODE" in stage2 and "BOOT_INFO_ADDR + BI_MODE" in stage2
# The loader and the kernel image agree on the image header it verifies.
assert "KERNEL_MAGIC    equ 0x4B524C46" in bootinfo
assert "0x4B524C46" in entry_asm
assert "FLAIR_BOOT_INFO_VERSION 2u" in boot_h
assert "FLAIR_BOOT_LIVE" in boot_h and "FLAIR_BOOT_SETUP" in boot_h
assert "boot_is_live_session()" in fs_c
assert "if (fsk_volatile || !fsk_disk)" in fs_c
assert "if (fsk_volatile)" in fs_c[fs_c.index("int fs_format(void)"):]
assert "if (boot_is_live_session())" in ata_c[ata_c.index("int ata_write_device_sector"):]
assert "port >= 0x1F0 && port <= 0x1F7" in kernel_c
assert "port >= 0x170 && port <= 0x177" in kernel_c
assert "SYS_FS_IS_VOLATILE 35" in fs_h and "SYS_FS_DISK_WRITES  36" in fs_h
assert 'cdrom: { url: "flair-livecd.iso" }' in web
assert 'fda: { url: "textile.img"' not in web
print("BootCD/LiveCD ISO structure and Live-mode safeguards passed")
