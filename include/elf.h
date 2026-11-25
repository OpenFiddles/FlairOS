#ifndef FLAIROS_ELF_H
#define FLAIROS_ELF_H

#include <stdint.h>

#define ELF_MAGIC   0x464C457Fu /* "\x7FELF" little-endian */
#define ELFCLASS32  1u
#define ELFDATA2LSB 1u
#define ET_EXEC     2u
#define ET_DYN      3u
#define EM_386      3u
#define PT_LOAD     1u
#define PT_DYNAMIC  2u

struct elf32_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed));

struct elf32_info {
    uint32_t entry;
    uint32_t min_vaddr;
    uint32_t max_vaddr;
    uint16_t load_count;
    uint16_t is_dyn;
    uint32_t dynamic_vaddr;
};

int elf32_validate(const uint8_t *buf, uint32_t len, struct elf32_info *info);
int elf32_load_into(const uint8_t *buf, uint32_t len, uint32_t *entry_out,
                    int (*map_write)(uint32_t vaddr, const uint8_t *src,
                                     uint32_t filesz, uint32_t memsz, uint32_t flags));

#define R_386_NONE     0u
#define R_386_32       1u
#define R_386_RELATIVE 8u

struct elf32_rel {
    uint32_t r_offset;
    uint32_t r_info;
} __attribute__((packed));

int elf32_apply_relocs(const struct elf32_rel *rels, uint32_t count,
                       uint32_t load_bias, uint8_t *image, uint32_t image_size);

#endif
