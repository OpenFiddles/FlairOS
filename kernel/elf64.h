#ifndef FLAIROS_ELF64_H
#define FLAIROS_ELF64_H

#include <stdint.h>

#define ELF64_EM_X86_64   62u
#define ELF64_EM_AARCH64  183u
#define ELF64_ET_EXEC     2u
#define ELF64_ET_DYN      3u

#define ELF64_MAX_SYMS    128

struct elf64_sym_entry {
    char name[48];
    uintptr_t addr;
};

struct elf64_image {
    int loaded;
    uint16_t machine;
    uint16_t type;
    uint64_t entry;
    uint8_t *base;
    uint64_t vaddr_min;
    uint64_t mem_size;
    int reloc_count;
    int sym_count;
    struct elf64_sym_entry syms[ELF64_MAX_SYMS];
};

int elf64_validate(const uint8_t *buf, uint32_t size, uint16_t *out_machine, uint16_t *out_type);
int elf64_load_image(const uint8_t *buf, uint32_t size, struct elf64_image *out);
int elf64_dlopen(const char *vfs_path, struct elf64_image *out);
uintptr_t elf64_dlsym(const struct elf64_image *img, const char *name);
uintptr_t elf64_lookup_runtime_sym(const char *name);

#endif
