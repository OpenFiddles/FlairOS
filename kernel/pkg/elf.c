#include "../../include/elf.h"

int elf32_validate(const uint8_t *buf, uint32_t len, struct elf32_info *info) {
    const struct elf32_ehdr *eh;
    uint32_t ph_end;
    uint32_t min_v = 0xFFFFFFFFu, max_v = 0u;
    uint16_t loads = 0;
    uint32_t dyn_v = 0;

    if (!buf || len < sizeof(struct elf32_ehdr))
        return -1;
    eh = (const struct elf32_ehdr *)buf;
    if (*(const uint32_t *)eh->e_ident != ELF_MAGIC)
        return -2;
    if (eh->e_ident[4] != ELFCLASS32 || eh->e_ident[5] != ELFDATA2LSB)
        return -3;
    if ((eh->e_type != ET_EXEC && eh->e_type != ET_DYN) || eh->e_machine != EM_386)
        return -4;
    if (eh->e_phentsize < sizeof(struct elf32_phdr) || eh->e_phnum == 0 || eh->e_phnum > 64)
        return -5;
    if (eh->e_phoff > len)
        return -6;
    ph_end = eh->e_phoff + (uint32_t)eh->e_phnum * (uint32_t)eh->e_phentsize;
    if (ph_end > len || ph_end < eh->e_phoff)
        return -6;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr *ph = (const struct elf32_phdr *)(buf + eh->e_phoff + (uint32_t)i * eh->e_phentsize);
        if (ph->p_type == PT_DYNAMIC) { dyn_v = ph->p_vaddr; continue; }
        if (ph->p_type != PT_LOAD)
            continue;
        if (ph->p_memsz == 0 || ph->p_filesz > ph->p_memsz)
            return -7;
        if (ph->p_offset > len || ph->p_filesz > len - ph->p_offset)
            return -8;
        if (ph->p_vaddr + ph->p_memsz < ph->p_vaddr)
            return -9;
        if (ph->p_vaddr < min_v) min_v = ph->p_vaddr;
        if (ph->p_vaddr + ph->p_memsz > max_v) max_v = ph->p_vaddr + ph->p_memsz;
        loads++;
    }

    if (loads == 0)
        return -10;
    if (eh->e_type == ET_EXEC && (eh->e_entry < min_v || eh->e_entry >= max_v))
        return -11;

    if (info) {
        info->entry = eh->e_entry;
        info->min_vaddr = min_v;
        info->max_vaddr = max_v;
        info->load_count = loads;
        info->is_dyn = (eh->e_type == ET_DYN) ? 1u : 0u;
        info->dynamic_vaddr = dyn_v;
    }
    return 0;
}

int elf32_load_into(const uint8_t *buf, uint32_t len, uint32_t *entry_out,
                    int (*map_write)(uint32_t vaddr, const uint8_t *src,
                                     uint32_t filesz, uint32_t memsz, uint32_t flags)) {
    struct elf32_info info;
    const struct elf32_ehdr *eh;
    int rc = elf32_validate(buf, len, &info);
    if (rc != 0 || !map_write)
        return rc != 0 ? rc : -1;

    eh = (const struct elf32_ehdr *)buf;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr *ph = (const struct elf32_phdr *)(buf + eh->e_phoff + (uint32_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD)
            continue;
        if (map_write(ph->p_vaddr, buf + ph->p_offset, ph->p_filesz, ph->p_memsz, ph->p_flags) != 0)
            return -12;
    }

    if (entry_out)
        *entry_out = info.entry;
    return 0;
}

int elf32_apply_relocs(const struct elf32_rel *rels, uint32_t count,
                       uint32_t load_bias, uint8_t *image, uint32_t image_size) {
    if (!rels || !image) return -1;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t type = rels[i].r_info & 0xFFu;
        uint32_t off = rels[i].r_offset;
        if (type == R_386_NONE) continue;
        if (off + 4u > image_size) return -2;
        uint32_t *slot = (uint32_t *)(image + off);
        if (type == R_386_RELATIVE || type == R_386_32)
            *slot += load_bias;
        else
            return -3;
    }
    return 0;
}
