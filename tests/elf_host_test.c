#include <stdio.h>
#include <string.h>
#include "elf.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static uint8_t fake_elf[256];
static uint8_t loaded_mem[128];

static int test_map_write(uint32_t vaddr, const uint8_t *src, uint32_t filesz, uint32_t memsz, uint32_t flags) {
    (void)flags;
    if (vaddr != 0x00400000u || memsz > sizeof(loaded_mem)) return -1;
    memset(loaded_mem, 0xAA, sizeof(loaded_mem));
    memcpy(loaded_mem, src, filesz);
    if (memsz > filesz)
        memset(loaded_mem + filesz, 0, memsz - filesz);
    return 0;
}

int main(void) {
    struct elf32_ehdr *eh = (struct elf32_ehdr *)fake_elf;
    struct elf32_phdr *ph = (struct elf32_phdr *)(fake_elf + sizeof(*eh));
    struct elf32_info info;

    memset(fake_elf, 0, sizeof(fake_elf));
    *(uint32_t *)eh->e_ident = ELF_MAGIC;
    eh->e_ident[4] = ELFCLASS32;
    eh->e_ident[5] = ELFDATA2LSB;
    eh->e_type = ET_EXEC;
    eh->e_machine = EM_386;
    eh->e_version = 1;
    eh->e_entry = 0x00400010u;
    eh->e_phoff = sizeof(*eh);
    eh->e_ehsize = sizeof(*eh);
    eh->e_phentsize = sizeof(*ph);
    eh->e_phnum = 1;

    ph->p_type = PT_LOAD;
    ph->p_offset = 128;
    ph->p_vaddr = 0x00400000u;
    ph->p_filesz = 32;
    ph->p_memsz = 64;
    for (int i = 0; i < 32; i++) fake_elf[128 + i] = (uint8_t)(i + 1);

    CHECK(elf32_validate(fake_elf, sizeof(fake_elf), &info) == 0);
    CHECK(info.entry == 0x00400010u);
    CHECK(info.min_vaddr == 0x00400000u);
    CHECK(info.max_vaddr == 0x00400040u);

    uint32_t entry = 0;
    CHECK(elf32_load_into(fake_elf, sizeof(fake_elf), &entry, test_map_write) == 0);
    CHECK(entry == 0x00400010u);
    CHECK(loaded_mem[0] == 1 && loaded_mem[31] == 32);
    CHECK(loaded_mem[32] == 0 && loaded_mem[63] == 0); /* BSS zeroed */

    /* Bad magic fails */
    fake_elf[0] = 0;
    CHECK(elf32_validate(fake_elf, sizeof(fake_elf), 0) != 0);

    /* ET_DYN relative relocation test */
    uint8_t dyn_img[16];
    memset(dyn_img, 0, sizeof(dyn_img));
    *(uint32_t *)(dyn_img + 4) = 0x100u;
    struct elf32_rel rel = { 4u, R_386_RELATIVE };
    CHECK(elf32_apply_relocs(&rel, 1, 0x00400000u, dyn_img, sizeof(dyn_img)) == 0);
    CHECK(*(uint32_t *)(dyn_img + 4) == 0x00400100u);

    puts("ELF32 loader host test: all checks passed");
    return 0;
}
