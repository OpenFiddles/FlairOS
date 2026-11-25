/* SPDX-License-Identifier: AGPL-3.0-only */
/* 64-bit ELF + .so Dynamic Linker for FlairOS (x86_64 & aarch64) */
#include "elf64.h"
#include "../include/kmem64.h"
#include "../include/vfs.h"
#include "../runtime/art/android_libs.h"

struct elf64_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

struct elf64_shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct elf64_sym {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct elf64_rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};

#define PT_LOAD       1u
#define SHT_SYMTAB    2u
#define SHT_STRTAB    3u
#define SHT_RELA      4u
#define SHT_DYNSYM    11u

#define R_X86_64_GLOB_DAT   6u
#define R_X86_64_JUMP_SLOT  7u
#define R_X86_64_RELATIVE   8u
#define R_AARCH64_RELATIVE  1027u
#define R_AARCH64_GLOB_DAT  1025u
#define R_AARCH64_JUMP_SLOT 1026u

static void e_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

static int e_seq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static void e_zero(void *p, uint64_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint64_t i = 0; i < n; i++) b[i] = 0;
}

static void e_copy(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
}

static int rt_strlen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static int rt_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void *rt_malloc(uint64_t sz) { return kmem64_alloc((size_t)sz); }
static void rt_free(void *p) { (void)p; }

uintptr_t elf64_lookup_runtime_sym(const char *name) {
    if (!name || !name[0]) return 0;
    if (e_seq(name, "malloc"))  return (uintptr_t)rt_malloc;
    if (e_seq(name, "free"))    return (uintptr_t)rt_free;
    if (e_seq(name, "strlen"))  return (uintptr_t)rt_strlen;
    if (e_seq(name, "strcmp"))  return (uintptr_t)rt_strcmp;
    if (e_seq(name, "vfs_open"))  return (uintptr_t)vfs_open;
    if (e_seq(name, "vfs_read"))  return (uintptr_t)vfs_read;
    if (e_seq(name, "vfs_write")) return (uintptr_t)vfs_write;
    if (e_seq(name, "vfs_close")) return (uintptr_t)vfs_close;
    return android_lookup_bionic_sym(name);
}

int elf64_validate(const uint8_t *buf, uint32_t size, uint16_t *out_machine, uint16_t *out_type) {
    if (!buf || size < sizeof(struct elf64_ehdr)) return -1;
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)buf;
    if (eh->e_ident[0] != 0x7Fu || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return -1;
    if (eh->e_ident[4] != 2u || eh->e_ident[5] != 1u) return -1; /* ELF64 little-endian */
    if (eh->e_machine != ELF64_EM_X86_64 && eh->e_machine != ELF64_EM_AARCH64) return -1;
    if (eh->e_type != ELF64_ET_EXEC && eh->e_type != ELF64_ET_DYN) return -1;
    if (out_machine) *out_machine = eh->e_machine;
    if (out_type) *out_type = eh->e_type;
    return 0;
}

int elf64_load_image(const uint8_t *buf, uint32_t size, struct elf64_image *out) {
    if (!out) return -1;
    e_zero(out, sizeof(*out));
    uint16_t mach = 0, typ = 0;
    if (elf64_validate(buf, size, &mach, &typ) != 0) return -1;

    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)buf;
    if (eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(struct elf64_phdr) > size) return -1;

    uint64_t vmin = ~0ULL, vmax = 0;
    const struct elf64_phdr *ph = (const struct elf64_phdr *)(buf + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) continue;
        if (ph[i].p_offset + ph[i].p_filesz > size) return -1;
        if (ph[i].p_vaddr < vmin) vmin = ph[i].p_vaddr;
        if (ph[i].p_vaddr + ph[i].p_memsz > vmax) vmax = ph[i].p_vaddr + ph[i].p_memsz;
    }
    if (vmin >= vmax || (vmax - vmin) > (4ULL * 1024ULL * 1024ULL)) return -1;

    uint64_t span = vmax - vmin;
    uint8_t *base = (uint8_t *)kmem64_alloc((size_t)span);
    if (!base) return -1;
    e_zero(base, span);

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) continue;
        uint64_t off = ph[i].p_vaddr - vmin;
        if (ph[i].p_filesz > 0) e_copy(base + off, buf + ph[i].p_offset, ph[i].p_filesz);
    }

    out->loaded = 1;
    out->machine = mach;
    out->type = typ;
    out->base = base;
    out->vaddr_min = vmin;
    out->mem_size = span;
    out->entry = (eh->e_entry >= vmin && eh->e_entry < vmax)
                 ? (uint64_t)(uintptr_t)(base + (eh->e_entry - vmin))
                 : (uint64_t)(uintptr_t)base;

    /* Parse symbol tables & apply RELA relocations if sections exist */
    if (eh->e_shoff && eh->e_shoff + (uint64_t)eh->e_shnum * sizeof(struct elf64_shdr) <= size) {
        const struct elf64_shdr *sh = (const struct elf64_shdr *)(buf + eh->e_shoff);
        for (uint16_t i = 0; i < eh->e_shnum; i++) {
            if ((sh[i].sh_type == SHT_SYMTAB || sh[i].sh_type == SHT_DYNSYM) &&
                sh[i].sh_link < eh->e_shnum &&
                sh[i].sh_offset + sh[i].sh_size <= size &&
                sh[sh[i].sh_link].sh_offset + sh[sh[i].sh_link].sh_size <= size) {
                const struct elf64_sym *syms = (const struct elf64_sym *)(buf + sh[i].sh_offset);
                const char *strs = (const char *)(buf + sh[sh[i].sh_link].sh_offset);
                uint64_t str_sz = sh[sh[i].sh_link].sh_size;
                uint64_t cnt = sh[i].sh_size / sizeof(struct elf64_sym);
                for (uint64_t si = 0; si < cnt && out->sym_count < ELF64_MAX_SYMS; si++) {
                    if (syms[si].st_name < str_sz && syms[si].st_value >= vmin && syms[si].st_value < vmax) {
                        const char *nm = strs + syms[si].st_name;
                        if (nm[0]) {
                            e_scpy(out->syms[out->sym_count].name, nm, 48);
                            out->syms[out->sym_count].addr = (uintptr_t)(base + (syms[si].st_value - vmin));
                            out->sym_count++;
                        }
                    }
                }
            }
        }
        for (uint16_t i = 0; i < eh->e_shnum; i++) {
            if (sh[i].sh_type != SHT_RELA || sh[i].sh_offset + sh[i].sh_size > size) continue;
            const struct elf64_rela *rel = (const struct elf64_rela *)(buf + sh[i].sh_offset);
            uint64_t rcnt = sh[i].sh_size / sizeof(struct elf64_rela);
            const struct elf64_sym *syms = 0;
            const char *strs = 0;
            uint64_t sym_cnt = 0, str_sz = 0;
            if (sh[i].sh_link < eh->e_shnum) {
                const struct elf64_shdr *sysh = &sh[sh[i].sh_link];
                if (sysh->sh_offset + sysh->sh_size <= size && sysh->sh_link < eh->e_shnum) {
                    const struct elf64_shdr *stsh = &sh[sysh->sh_link];
                    if (stsh->sh_offset + stsh->sh_size <= size) {
                        syms = (const struct elf64_sym *)(buf + sysh->sh_offset);
                        sym_cnt = sysh->sh_size / sizeof(struct elf64_sym);
                        strs = (const char *)(buf + stsh->sh_offset);
                        str_sz = stsh->sh_size;
                    }
                }
            }
            for (uint64_t ri = 0; ri < rcnt; ri++) {
                uint32_t rtype = (uint32_t)(rel[ri].r_info & 0xFFFFFFFFu);
                uint32_t rsym  = (uint32_t)(rel[ri].r_info >> 32);
                if (rel[ri].r_offset < vmin || rel[ri].r_offset + 8 > vmax) continue;
                uint64_t *slot = (uint64_t *)(base + (rel[ri].r_offset - vmin));
                if (rtype == R_X86_64_RELATIVE || rtype == R_AARCH64_RELATIVE) {
                    *slot = (uint64_t)(uintptr_t)(base + ((uint64_t)rel[ri].r_addend - vmin));
                    out->reloc_count++;
                } else if ((rtype == R_X86_64_GLOB_DAT || rtype == R_X86_64_JUMP_SLOT ||
                            rtype == R_AARCH64_GLOB_DAT || rtype == R_AARCH64_JUMP_SLOT) &&
                           syms && rsym < sym_cnt && syms[rsym].st_name < str_sz) {
                    const char *sn = strs + syms[rsym].st_name;
                    uintptr_t resolved = elf64_lookup_runtime_sym(sn);
                    if (!resolved) resolved = elf64_dlsym(out, sn);
                    if (resolved) {
                        *slot = (uint64_t)resolved;
                        out->reloc_count++;
                    }
                }
            }
        }
    }
    return 0;
}

int elf64_dlopen(const char *vfs_path, struct elf64_image *out) {
    if (!vfs_path || !out) return -1;
    struct vfs_stat st;
    if (vfs_stat(vfs_path, &st) != 0 || st.size == 0 || st.size > 262144u) return -1;
    uint8_t *buf = (uint8_t *)kmem64_alloc(st.size);
    if (!buf) return -1;
    int fd = vfs_open(vfs_path, VFS_O_RDONLY);
    if (fd < 0) return -1;
    int n = vfs_read(fd, buf, st.size);
    vfs_close(fd);
    int rc = (n > 0) ? elf64_load_image(buf, (uint32_t)n, out) : -1;
    return rc;
}

uintptr_t elf64_dlsym(const struct elf64_image *img, const char *name) {
    if (!img || !img->loaded || !name) return 0;
    for (int i = 0; i < img->sym_count; i++) {
        if (e_seq(img->syms[i].name, name)) return img->syms[i].addr;
    }
    return 0;
}
