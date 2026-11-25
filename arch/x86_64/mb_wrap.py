#!/usr/bin/env python3
"""Wrap an ELF64 kernel (linked at 0x100000) into an ELF32 Multiboot1 image
   so `qemu-system-x86_64 -kernel` loads it and enters the 32->64 trampoline.
"""
import struct, sys

def wrap_elf64_to_elf32(in_elf64, out_elf32):
    data = open(in_elf64, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        raise RuntimeError("Expected ELF64 input")
    e_entry, e_phoff, _, _, e_ehsize, e_phentsize, e_phnum = struct.unpack_from("<QQQIHHH", data, 24)
    segs = []
    min_va = None
    max_va = 0
    max_memsz = 0
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack_from("<IIQQQQQQ", data, off)
        if p_type == 1 and p_memsz > 0:
            segs.append((p_offset, p_vaddr, p_filesz, p_memsz))
            if min_va is None or p_vaddr < min_va:
                min_va = p_vaddr
            end_file = p_vaddr + p_filesz
            end_mem = p_vaddr + p_memsz
            if end_file > max_va:
                max_va = end_file
            if end_mem > max_memsz:
                max_memsz = end_mem
    flat_filesz = max_va - min_va
    flat_memsz = max_memsz - min_va
    flat = bytearray(flat_filesz)
    for p_offset, p_vaddr, p_filesz, _ in segs:
        flat[p_vaddr - min_va : p_vaddr - min_va + p_filesz] = data[p_offset : p_offset + p_filesz]

    # Build ELF32 header (52) + 1 PT_LOAD phdr (32), page-aligned payload at 0x1000
    hdr_off = 0x1000
    ehdr = struct.pack("<16sHHIIIIIHHHHHH",
        b"\x7fELF\x01\x01\x01" + b"\x00" * 9,
        2, 3, 1, e_entry & 0xFFFFFFFF, 52, 0, 0, 52, 32, 1, 0, 0, 0)
    phdr = struct.pack("<IIIIIIII",
        1, hdr_off, min_va & 0xFFFFFFFF, min_va & 0xFFFFFFFF,
        flat_filesz, flat_memsz, 7, 0x1000)
    out = bytearray(hdr_off)
    out[:52] = ehdr
    out[52:84] = phdr
    out.extend(flat)
    open(out_elf32, "wb").write(out)

if __name__ == "__main__":
    wrap_elf64_to_elf32(sys.argv[1], sys.argv[2])
