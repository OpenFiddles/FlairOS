#!/usr/bin/env python3
"""Build a bootable aarch64 disk image: MBR + FAT ESP + UEFI loader.

    MBR (LBA 0)        partition table, one EFI System Partition (type 0xEF)
    LBA 2048..         FAT16 ESP containing
                         /EFI/BOOT/BOOTAA64.EFI   the loader, as a PE32+ image
                         /flair.bin               load header + flat kernel

The firmware loads BOOTAA64.EFI from the partition, the loader asks the
firmware's file-system protocol for flair.bin, copies the kernel to its link
address and hands over. Nothing about the kernel is embedded in the loader or
in this script.

    python3 tools/make_uefi_disk.py --loader loader.elf --kernel kernel.elf --out out.img
"""
import argparse
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from fat16 import Fat16, SECTOR  # noqa: E402

LOADER_BASE = 0x50000000
ESP_SECTORS = 32768
PART_LBA = 2048


def elf_entry(path):
    head = open(path, "rb").read(64)
    if head[:4] != b"\x7fELF" or head[4] != 2 or head[18:20] != b"\xb7\x00":
        raise RuntimeError(f"{path} is not an AArch64 ELF64 image")
    return struct.unpack_from("<Q", head, 24)[0]


def objcopy_binary(elf_path, out_path):
    subprocess.run(["llvm-objcopy", "-O", "binary", elf_path, out_path], check=True)
    return open(out_path, "rb").read()


def base_relocations(size):
    """A .reloc stream of ABSOLUTE entries covering `size` bytes.

    The loader is compiled PIC-free but only ever uses PC-relative addressing,
    so nothing actually needs fixing up. The section still matters: without
    base relocations EDK2 can only load an image at its declared ImageBase and
    silently skips it when that address is not free. Declaring the image
    relocatable lets the firmware put it wherever it likes.
    """
    blocks = b""
    page = 0
    while page * 0x1000 < size:
        entries = b""
        offset = 0
        while offset < 0x1000 and page * 0x1000 + offset < size:
            span = min(0xFFF, 0x1000 - offset)
            entries += struct.pack("<H", (0 << 12) | span)  # IMAGE_REL_BASED_ABSOLUTE
            offset += span
        block = struct.pack("<II", page * 0x1000, 8 + len(entries)) + entries
        if len(block) % 4:
            block += b"\x00\x00"
        blocks += block
        page += 1
    return blocks


def pe_wrap(flat, entry_rva, image_base, out_path):
    """Wrap a flat blob in a minimal PE32+ EFI application image."""
    headers = 0x200
    text_rva = 0x1000
    text_raw = (len(flat) + 0x1FF) & ~0x1FF
    relocs = base_relocations(len(flat))
    reloc_rva = text_rva + ((len(flat) + 0xFFF) & ~0xFFF)
    reloc_raw = (len(relocs) + 0x1FF) & ~0x1FF
    size_of_image = reloc_rva + ((len(relocs) + 0xFFF) & ~0xFFF)

    dos = bytearray(headers)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 0x40)
    dos[0x40:0x44] = b"PE\0\0"

    coff = struct.pack("<HHIIIHH", 0xAA64, 2, 0, 0, 0, 0xF0, 0x2022)
    opt = bytearray(0xF0)
    struct.pack_into("<H", opt, 0, 0x20B)          # PE32+
    struct.pack_into("<B", opt, 2, 2)              # linker major
    struct.pack_into("<I", opt, 4, text_raw)       # SizeOfCode
    struct.pack_into("<I", opt, 8, reloc_raw)      # SizeOfInitializedData
    struct.pack_into("<I", opt, 16, entry_rva)     # AddressOfEntryPoint
    struct.pack_into("<I", opt, 20, text_rva)      # BaseOfCode
    struct.pack_into("<Q", opt, 24, image_base)
    struct.pack_into("<I", opt, 32, 0x1000)        # SectionAlignment
    struct.pack_into("<I", opt, 36, 0x200)         # FileAlignment
    struct.pack_into("<H", opt, 40, 1)             # MajorOperatingSystemVersion
    struct.pack_into("<H", opt, 48, 1)             # MajorSubsystemVersion
    struct.pack_into("<I", opt, 56, size_of_image)
    struct.pack_into("<I", opt, 60, headers)       # SizeOfHeaders
    struct.pack_into("<H", opt, 68, 10)            # EFI application
    struct.pack_into("<Q", opt, 72, 0x40000)       # SizeOfStackReserve
    struct.pack_into("<Q", opt, 80, 0x40000)       # SizeOfStackCommit
    struct.pack_into("<Q", opt, 88, 0x100000)      # SizeOfHeapReserve
    struct.pack_into("<Q", opt, 96, 0x100000)      # SizeOfHeapCommit
    struct.pack_into("<I", opt, 108, 16)           # NumberOfRvaAndSizes
    struct.pack_into("<II", opt, 112 + 5 * 8, reloc_rva, len(relocs))  # base relocations

    text = struct.pack("<8sIIIIIIHHI", b".text", len(flat), text_rva,
                       text_raw, headers, 0, 0, 0, 0, 0xE0000020)  # code|execute|read|write: the flat image keeps .data here
    reloc = struct.pack("<8sIIIIIIHHI", b".reloc", len(relocs), reloc_rva,
                        reloc_raw, headers + text_raw, 0, 0, 0, 0, 0x42000040)

    out = bytearray(headers)
    out[0:len(dos)] = dos
    out[0x44:0x44 + len(coff)] = coff
    out[0x58:0x58 + len(opt)] = opt
    out[0x148:0x148 + len(text)] = text
    out[0x148 + 40:0x148 + 40 + len(reloc)] = reloc
    out += flat
    out += b"\x00" * (text_raw - len(flat))
    out += relocs
    out += b"\x00" * (reloc_raw - len(relocs))
    open(out_path, "wb").write(out)
    return size_of_image


def build(loader_elf, kernel_elf, out_path, workdir):
    os.makedirs(workdir, exist_ok=True)
    loader_flat = objcopy_binary(loader_elf, os.path.join(workdir, "loader.bin"))
    entry = elf_entry(loader_elf)
    if not LOADER_BASE <= entry < LOADER_BASE + len(loader_flat):
        raise RuntimeError(f"loader entry 0x{entry:x} is outside its image")
    efi = os.path.join(workdir, "BOOTAA64.EFI")
    pe_wrap(loader_flat, 0x1000 + (entry - LOADER_BASE), LOADER_BASE, efi)

    kernel_entry = elf_entry(kernel_elf)
    kernel_flat = objcopy_binary(kernel_elf, os.path.join(workdir, "kernel.bin"))
    header = struct.pack("<IIQQQ", 0x41363446, 1, kernel_entry, kernel_entry, len(kernel_flat))
    flair = header + kernel_flat

    esp = Fat16(ESP_SECTORS)
    esp.mkdir("/EFI")
    esp.mkdir("/EFI/BOOT")
    esp.add_file("/EFI/BOOT/BOOTAA64.EFI", open(efi, "rb").read())
    esp.add_file("/flair.bin", flair)
    esp_bytes = esp.to_bytes()

    disk = bytearray(PART_LBA * SECTOR)
    disk[0x1BE] = 0x00                       # not bootable; firmware reads the table
    disk[0x1BF:0x1C2] = b"\x00\x02\x00"      # CHS start (unused)
    disk[0x1C2] = 0xEF                       # EFI System Partition
    disk[0x1C3:0x1C6] = b"\xFF\xFF\xFF"      # CHS end (unused)
    struct.pack_into("<II", disk, 0x1C6, PART_LBA, len(esp_bytes) // SECTOR)
    disk[510:512] = b"\x55\xAA"
    disk.extend(esp_bytes)
    open(out_path, "wb").write(disk)
    return len(disk), len(kernel_flat), entry, kernel_entry


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--loader", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    work = os.path.join(os.path.dirname(os.path.abspath(args.out)), "uefi")
    size, ksize, lentry, kentry = build(args.loader, args.kernel, args.out, work)
    print(f"[OK] uefi disk: {args.out} ({size} bytes, kernel {ksize} bytes at "
          f"0x{kentry:x}, loader entry 0x{lentry:x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
