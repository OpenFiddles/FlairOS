#ifndef FLAIROS_ELF_H
#define FLAIROS_ELF_H

#include <stdint.h>

#define ELF_MAGIC   0x464C457Fu /* "\x7FELF" little-endian */
#define ELFCLASS32  1u
#define ELFDATA2LSB 1u
#define ET_EXEC     2u
#define EM_386      3u
#define PT_LOAD     1u

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
};

int elf32_validate(const uint8_t *buf, uint32_t len, struct elf32_info *info);
int elf32_load_into(const uint8_t *buf, uint32_t len, uint32_t *entry_out,
                    int (*map_write)(uint32_t vaddr, const uint8_t *src,
                                     uint32_t filesz, uint32_t memsz, uint32_t flags));

#endif
