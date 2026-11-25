#include "../include/fat32.h"

static uint16_t f_rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t f_rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static char f_up(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static void format_83(const uint8_t raw[11], char out[13]) {
    int n = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) out[n++] = (char)raw[i];
    if (raw[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) out[n++] = (char)raw[i];
    }
    out[n] = 0;
}

static int f_eq_ci(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && f_up(a[i]) == f_up(b[i])) i++;
    return f_up(a[i]) == f_up(b[i]);
}

int fat32_mount(struct fat32_fs *fs, fat32_read_fn read_fn, void *ctx, uint32_t part_lba) {
    uint8_t sec[512];
    if (!fs || !read_fn || read_fn(ctx, part_lba, sec) != 0) return -1;
    if (sec[510] != 0x55 || sec[511] != 0xAA) return -2;
    fs->read_sector = read_fn;
    fs->write_sector = 0;
    fs->ctx = ctx;
    fs->bytes_per_sector = f_rd16(sec + 11);
    fs->sectors_per_cluster = sec[13];
    fs->reserved_sectors = f_rd16(sec + 14);
    fs->num_fats = sec[16];
    fs->fat_size_sectors = f_rd32(sec + 36);
    fs->root_cluster = f_rd32(sec + 44);
    fs->total_sectors = f_rd16(sec + 19);
    if (!fs->total_sectors) fs->total_sectors = f_rd32(sec + 32);
    if (fs->bytes_per_sector != 512 || !fs->sectors_per_cluster ||
        (fs->sectors_per_cluster & (fs->sectors_per_cluster - 1u)) ||
        !fs->reserved_sectors || !fs->num_fats || !fs->fat_size_sectors ||
        fs->root_cluster < 2 || !fs->total_sectors)
        return -3;
    {
        uint64_t fat_lba = (uint64_t)part_lba + fs->reserved_sectors;
        uint64_t data_lba = fat_lba + (uint64_t)fs->num_fats * fs->fat_size_sectors;
        uint64_t volume_end = (uint64_t)part_lba + fs->total_sectors;
        if (data_lba > 0xFFFFFFFFu || volume_end > 0x100000000ull ||
            data_lba >= volume_end || data_lba < fat_lba)
            return -3;
        fs->fat_lba = (uint32_t)fat_lba;
        fs->data_lba = (uint32_t)data_lba;
    }
    fs->cluster_count = (fs->total_sectors - (fs->data_lba - part_lba)) /
                        fs->sectors_per_cluster;
    if (fs->cluster_count < 65525u || fs->root_cluster >= fs->cluster_count + 2u ||
        (uint64_t)fs->fat_size_sectors * 128u < (uint64_t)fs->cluster_count + 2u)
        return -3; /* Reject FAT12/FAT16 and malformed/out-of-range volumes. */
    return 0;
}

uint32_t fat32_next_cluster(const struct fat32_fs *fs, uint32_t cluster) {
    uint8_t sec[512];
    if (!fs || cluster < 2 || cluster >= fs->cluster_count + 2u) return FAT32_EOC;
    uint32_t fat_off = cluster * 4u;
    uint32_t lba = fs->fat_lba + (fat_off / 512u);
    uint32_t off = fat_off % 512u;
    if (lba >= fs->fat_lba + fs->fat_size_sectors || fs->read_sector(fs->ctx, lba, sec) != 0) return FAT32_EOC;
    return f_rd32(sec + off) & 0x0FFFFFFFu;
}

int fat32_find_in_dir(const struct fat32_fs *fs, uint32_t dir_cluster,
                      const char *name83, struct fat32_dirent *out) {
    uint8_t sec[512];
    uint32_t cl = dir_cluster;
    if (!fs || !name83 || cl < 2) return -1;
    uint32_t steps = 0;
    while (cl >= 2 && cl < FAT32_EOC && cl < fs->cluster_count + 2u && steps++ < fs->cluster_count) {
        uint32_t base_lba = fs->data_lba + (cl - 2u) * fs->sectors_per_cluster;
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            if (fs->read_sector(fs->ctx, base_lba + s, sec) != 0) return -1;
            for (uint32_t e = 0; e < 512u; e += 32u) {
                uint8_t first = sec[e];
                if (first == 0x00) return -1;
                if (first == 0xE5 || sec[e + 11] == 0x0F || (sec[e + 11] & 0x08)) continue;
                char nm[13];
                format_83(sec + e, nm);
                if (f_eq_ci(nm, name83)) {
                    if (out) {
                        for (int k = 0; k < 13; k++) out->name[k] = nm[k];
                        out->attr = sec[e + 11];
                        out->cluster = ((uint32_t)f_rd16(sec + e + 20) << 16) | f_rd16(sec + e + 26);
                        out->size = f_rd32(sec + e + 28);
                    }
                    return 0;
                }
            }
        }
        cl = fat32_next_cluster(fs, cl);
    }
    return -1;
}

int fat32_read_file(const struct fat32_fs *fs, const struct fat32_dirent *ent,
                    void *buf, uint32_t max_bytes) {
    uint8_t sec[512];
    if (!fs || !ent || !buf) return -1;
    uint32_t to_read = ent->size < max_bytes ? ent->size : max_bytes;
    uint32_t pos = 0, cl = ent->cluster;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t steps = 0;
    while (pos < to_read && cl >= 2 && cl < FAT32_EOC && cl < fs->cluster_count + 2u && steps++ < fs->cluster_count) {
        uint32_t base_lba = fs->data_lba + (cl - 2u) * fs->sectors_per_cluster;
        for (uint32_t s = 0; s < fs->sectors_per_cluster && pos < to_read; s++) {
            if (fs->read_sector(fs->ctx, base_lba + s, sec) != 0) return -1;
            uint32_t chunk = to_read - pos;
            if (chunk > 512u) chunk = 512u;
            for (uint32_t i = 0; i < chunk; i++) dst[pos + i] = sec[i];
            pos += chunk;
        }
        cl = fat32_next_cluster(fs, cl);
    }
    return (int)pos;
}

int fat32_set_writer(struct fat32_fs *fs, fat32_write_fn write_fn) {
    if (!fs) return -1;
    fs->write_sector = write_fn;
    return 0;
}

static int f_write(const struct fat32_fs *fs, uint32_t lba, const uint8_t *sec) {
    uint64_t end = (uint64_t)fs->fat_lba - fs->reserved_sectors + fs->total_sectors;
    if (!fs->write_sector || lba < fs->fat_lba - fs->reserved_sectors ||
        (uint64_t)lba >= end) return -1;
    return fs->write_sector(fs->ctx, lba, sec);
}

static int f_read(const struct fat32_fs *fs, uint32_t lba, uint8_t *sec) {
    uint64_t end = (uint64_t)fs->fat_lba - fs->reserved_sectors + fs->total_sectors;
    if (lba < fs->fat_lba - fs->reserved_sectors || (uint64_t)lba >= end) return -1;
    return fs->read_sector(fs->ctx, lba, sec);
}

static int f_set_fat(const struct fat32_fs *fs, uint32_t cl, uint32_t value) {
    uint8_t sec[512];
    uint32_t off = cl * 4u, rel = off / 512u, pos = off % 512u;
    if (cl < 2 || cl >= fs->cluster_count + 2u || rel >= fs->fat_size_sectors) return -1;
    for (uint32_t copy = 0; copy < fs->num_fats; ++copy) {
        uint32_t lba = fs->fat_lba + copy * fs->fat_size_sectors + rel;
        if (f_read(fs, lba, sec) != 0) return -1;
        uint32_t old = f_rd32(sec + pos);
        uint32_t v = (old & 0xF0000000u) | (value & 0x0FFFFFFFu);
        sec[pos] = (uint8_t)v; sec[pos+1] = (uint8_t)(v >> 8);
        sec[pos+2] = (uint8_t)(v >> 16); sec[pos+3] = (uint8_t)(v >> 24);
        if (f_write(fs, lba, sec) != 0) return -1;
    }
    return 0;
}

static int f_alloc_cluster(const struct fat32_fs *fs, uint32_t *out) {
    uint8_t sec[512];
    uint32_t entries = fs->cluster_count + 2u;
    for (uint32_t cl = 2; cl < entries; ++cl) {
        uint32_t rel = (cl * 4u) / 512u, pos = (cl * 4u) % 512u;
        if (rel >= fs->fat_size_sectors || f_read(fs, fs->fat_lba + rel, sec) != 0) return -1;
        if ((f_rd32(sec + pos) & 0x0FFFFFFFu) == 0) {
            if (f_set_fat(fs, cl, FAT32_EOC) != 0) return -1;
            *out = cl;
            return 0;
        }
    }
    return -1;
}

static int f_free_chain(const struct fat32_fs *fs, uint32_t cl) {
    uint32_t steps = 0;
    while (cl >= 2 && cl < fs->cluster_count + 2u && steps++ < fs->cluster_count) {
        uint32_t next = fat32_next_cluster(fs, cl);
        if (f_set_fat(fs, cl, 0) != 0) return -1;
        if (next >= FAT32_EOC) return 0;
        if (next < 2 || next >= fs->cluster_count + 2u) return -1;
        cl = next;
    }
    return cl >= FAT32_EOC ? 0 : -1;
}

static int f_name83(const char *name, uint8_t raw[11]) {
    int base = 0, ext = 0, dot = 0;
    for (int i = 0; i < 11; ++i) raw[i] = ' ';
    if (!name || !name[0]) return -1;
    for (int i = 0; name[i]; ++i) {
        char c = f_up(name[i]);
        if (c == '.') { if (dot || base == 0) return -1; dot = 1; continue; }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '$' || c == '~')) return -1;
        if (!dot) { if (base >= 8) return -1; raw[base++] = (uint8_t)c; }
        else { if (ext >= 3) return -1; raw[8 + ext++] = (uint8_t)c; }
    }
    return base ? 0 : -1;
}

static int f_find_slot(const struct fat32_fs *fs, uint32_t dir, uint32_t *lba_out, uint32_t *off_out) {
    uint8_t sec[512]; uint32_t cl = dir, steps = 0;
    while (cl >= 2 && cl < fs->cluster_count + 2u && steps++ < fs->cluster_count) {
        uint32_t base = fs->data_lba + (cl - 2u) * fs->sectors_per_cluster;
        for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s) {
            if (f_read(fs, base + s, sec) != 0) return -1;
            for (uint32_t off = 0; off < 512; off += 32) {
                if (sec[off] == 0 || sec[off] == 0xE5) { *lba_out = base + s; *off_out = off; return 0; }
            }
        }
        uint32_t next = fat32_next_cluster(fs, cl);
        if (next >= FAT32_EOC) break;
        cl = next;
    }
    /* Extend the directory chain by one zeroed cluster. */
    uint32_t fresh;
    if (f_alloc_cluster(fs, &fresh) != 0) return -1;
    if (f_set_fat(fs, cl, fresh) != 0) { f_set_fat(fs, fresh, 0); return -1; }
    uint32_t base = fs->data_lba + (fresh - 2u) * fs->sectors_per_cluster;
    for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s) {
        for (uint32_t i = 0; i < 512; ++i) sec[i] = 0;
        if (f_write(fs, base + s, sec) != 0) return -1;
    }
    *lba_out = base; *off_out = 0;
    return 0;
}

static int f_find_entry(const struct fat32_fs *fs, uint32_t dir, const char *name,
                        uint32_t *lba_out, uint32_t *off_out, struct fat32_dirent *ent) {
    uint8_t sec[512], raw[11]; uint32_t cl = dir, steps = 0;
    if (dir < 2 || dir >= fs->cluster_count + 2u || f_name83(name, raw) != 0) return -1;
    while (cl >= 2 && cl < fs->cluster_count + 2u && steps++ < fs->cluster_count) {
        uint32_t base = fs->data_lba + (cl - 2u) * fs->sectors_per_cluster;
        for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s) {
            if (f_read(fs, base + s, sec) != 0) return -1;
            for (uint32_t off = 0; off < 512; off += 32) {
                if (sec[off] == 0) return -1;
                if (sec[off] == 0xE5 || sec[off+11] == 0x0F || (sec[off+11] & 0x08)) continue;
                int same = 1; for (int i = 0; i < 11; ++i) if (sec[off+i] != raw[i]) same = 0;
                if (!same) continue;
                if (lba_out) *lba_out = base + s;
                if (off_out) *off_out = off;
                if (ent) { format_83(sec + off, ent->name); ent->attr = sec[off+11]; ent->cluster = ((uint32_t)f_rd16(sec+off+20)<<16)|f_rd16(sec+off+26); ent->size=f_rd32(sec+off+28); }
                return 0;
            }
        }
        cl = fat32_next_cluster(fs, cl);
    }
    return -1;
}


int fat32_mkdir(struct fat32_fs *fs, uint32_t dir, const char *name, struct fat32_dirent *out) {
    uint8_t sec[512], raw[11];
    uint32_t lba, off, fresh, dotdot;
    if (!fs || !fs->write_sector || dir < 2 || dir >= fs->cluster_count + 2u || f_name83(name, raw) != 0)
        return -1;
    if (f_find_entry(fs, dir, name, 0, 0, 0) == 0) return -1;
    if (f_find_slot(fs, dir, &lba, &off) != 0) return -1;
    if (f_alloc_cluster(fs, &fresh) != 0) return -1;
    for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s) {
        for (int i = 0; i < 512; ++i) sec[i] = 0;
        if (s == 0) {
            /* "." entry */
            sec[0] = '.'; for (int i = 1; i < 11; ++i) sec[i] = ' ';
            sec[11] = FAT32_ATTR_DIR;
            sec[20] = (uint8_t)(fresh >> 16); sec[21] = (uint8_t)(fresh >> 24);
            sec[26] = (uint8_t)fresh; sec[27] = (uint8_t)(fresh >> 8);
            /* ".." entry */
            dotdot = (dir == fs->root_cluster) ? 0u : dir;
            sec[32] = '.'; sec[33] = '.'; for (int i = 34; i < 43; ++i) sec[i] = ' ';
            sec[43] = FAT32_ATTR_DIR;
            sec[52] = (uint8_t)(dotdot >> 16); sec[53] = (uint8_t)(dotdot >> 24);
            sec[58] = (uint8_t)dotdot; sec[59] = (uint8_t)(dotdot >> 8);
        }
        if (f_write(fs, fs->data_lba + (fresh - 2u) * fs->sectors_per_cluster + s, sec) != 0) {
            f_free_chain(fs, fresh);
            return -1;
        }
    }
    if (f_read(fs, lba, sec) != 0) { f_free_chain(fs, fresh); return -1; }
    for (int i = 0; i < 11; ++i) sec[off + i] = raw[i];
    sec[off + 11] = FAT32_ATTR_DIR;
    sec[off + 20] = (uint8_t)(fresh >> 16); sec[off + 21] = (uint8_t)(fresh >> 24);
    sec[off + 26] = (uint8_t)fresh; sec[off + 27] = (uint8_t)(fresh >> 8);
    for (int i = 28; i < 32; ++i) sec[off + i] = 0;
    if (f_write(fs, lba, sec) != 0) { f_free_chain(fs, fresh); return -1; }
    if (out) { format_83(raw, out->name); out->attr = FAT32_ATTR_DIR; out->cluster = fresh; out->size = 0; }
    return 0;
}

int fat32_create_file(struct fat32_fs *fs, uint32_t dir, const char *name, struct fat32_dirent *out) {
    uint8_t sec[512], raw[11]; uint32_t lba, off;
    if (!fs || !fs->write_sector || dir < 2 || dir >= fs->cluster_count + 2u || f_name83(name, raw) != 0) return -1;
    if (f_find_entry(fs, dir, name, 0, 0, 0) == 0) return -1;
    if (f_find_slot(fs, dir, &lba, &off) != 0 || f_read(fs, lba, sec) != 0) return -1;
    for (int i = 0; i < 11; ++i) sec[off+i] = raw[i];
    sec[off+11] = 0x20; sec[off+20]=sec[off+21]=sec[off+26]=sec[off+27]=0;
    for (int i = 28; i < 32; ++i) sec[off+i] = 0;
    if (f_write(fs, lba, sec) != 0) return -1;
    if (out) { format_83(raw, out->name); out->attr=0x20; out->cluster=0; out->size=0; }
    return 0;
}

int fat32_write_file(struct fat32_fs *fs, uint32_t dir, const char *name, const void *data, uint32_t size) {
    struct fat32_dirent ent; uint32_t dir_lba, dir_off;
    uint8_t sec[512]; const uint8_t *src=(const uint8_t *)data;
    if (!fs || !fs->write_sector || (!data && size) || f_find_entry(fs, dir, name, &dir_lba, &dir_off, &ent) != 0 || (ent.attr & (FAT32_ATTR_DIR | FAT32_ATTR_READONLY))) return -1;
    uint32_t cluster_bytes = (uint32_t)fs->sectors_per_cluster * 512u;
    if (!cluster_bytes) return -1;
    uint32_t needed = size / cluster_bytes + ((size % cluster_bytes) != 0u);
    if (needed > fs->cluster_count) return -1;
    uint32_t first=0, tail=0, old=ent.cluster;
    for (uint32_t n=0; n<needed; ++n) {
        uint32_t fresh;
        if (f_alloc_cluster(fs,&fresh)!=0) { if(first) f_free_chain(fs,first); return -1; }
        if (!first) first=fresh;
        if (tail && f_set_fat(fs,tail,fresh)!=0) { f_free_chain(fs,first); return -1; }
        tail=fresh;
    }
    uint32_t pos=0, cl=first;
    for (uint32_t n=0; n<needed; ++n) {
        uint32_t base=fs->data_lba+(cl-2u)*fs->sectors_per_cluster;
        for (uint32_t s=0; s<fs->sectors_per_cluster; ++s) {
            for (uint32_t i=0;i<512;++i) sec[i]=(pos<size)?src[pos++]:0;
            if (f_write(fs,base+s,sec)!=0) { if(first) f_free_chain(fs,first); return -1; }
        }
        cl=fat32_next_cluster(fs,cl);
    }
    /* Publish new metadata only after all file data has reached the device. */
    if (f_read(fs,dir_lba,sec)!=0) { if(first) f_free_chain(fs,first); return -1; }
    sec[dir_off+20]=(uint8_t)(first>>16); sec[dir_off+21]=(uint8_t)(first>>24);
    sec[dir_off+26]=(uint8_t)first; sec[dir_off+27]=(uint8_t)(first>>8);
    sec[dir_off+28]=(uint8_t)size; sec[dir_off+29]=(uint8_t)(size>>8); sec[dir_off+30]=(uint8_t)(size>>16); sec[dir_off+31]=(uint8_t)(size>>24);
    if (f_write(fs,dir_lba,sec)!=0) { if(first) f_free_chain(fs,first); return -1; }
    if (old >= 2 && old < fs->cluster_count+2u) f_free_chain(fs,old);
    return 0;
}

int fat32_delete_file(struct fat32_fs *fs, uint32_t dir, const char *name) {
    uint32_t lba,off; struct fat32_dirent ent; uint8_t sec[512];
    if (!fs || !fs->write_sector || f_find_entry(fs,dir,name,&lba,&off,&ent)!=0 || (ent.attr & (FAT32_ATTR_DIR | FAT32_ATTR_READONLY))) return -1;
    if (f_read(fs,lba,sec)!=0) return -1;
    sec[off]=0xE5;
    if (f_write(fs,lba,sec)!=0) return -1;
    if (ent.cluster>=2 && ent.cluster<fs->cluster_count+2u) return f_free_chain(fs,ent.cluster);
    return 0;
}

int fat32_readdir(const struct fat32_fs *fs, uint32_t dir_cluster,
                  uint32_t index, struct fat32_dirent *out) {
    uint8_t sec[512];
    uint32_t cl = dir_cluster, seen = 0, steps = 0;
    if (!fs || cl < 2 || cl >= fs->cluster_count + 2u) return -1;
    while (cl >= 2 && cl < FAT32_EOC && cl < fs->cluster_count + 2u &&
           steps++ < fs->cluster_count) {
        uint32_t base = fs->data_lba + (cl - 2u) * fs->sectors_per_cluster;
        for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s) {
            if (fs->read_sector(fs->ctx, base + s, sec) != 0) return -1;
            for (uint32_t e = 0; e < 512u; e += 32u) {
                uint8_t first = sec[e], attr = sec[e + 11];
                if (first == 0x00) return -1;
                if (first == 0xE5 || attr == 0x0F || (attr & 0x08)) continue;
                if (seen++ != index) continue;
                if (!out) return 0;
                format_83(sec + e, out->name);
                out->attr = attr;
                out->cluster = ((uint32_t)f_rd16(sec + e + 20) << 16) |
                               f_rd16(sec + e + 26);
                out->size = f_rd32(sec + e + 28);
                return 0;
            }
        }
        cl = fat32_next_cluster(fs, cl);
    }
    return -1;
}
