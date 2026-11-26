#include "../../include/vfs.h"
#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
#include "../../include/fat32.h"
#include "../../include/apk_samples.h"
#include "../../include/drv_ata.h"
#include "../../include/boot.h"
#elif defined(FLAIROS_64BIT)
#include "../../include/apk_samples.h"
#include "../../include/jvm_sample.h"
#include "../../include/vfs_apps_gen.h"
#endif

#ifdef FLAIROS_HOST_TEST
#include <stdlib.h>
static void *vfs_alloc(unsigned int sz) { return malloc(sz); }
static void vfs_free(void *p) { free(p); }
static void vfs_console_putc(char c) { (void)c; }
static int vfs_console_getc(void) { return -1; }
#else
#include "../../include/kheap.h"
void putc(char c);
int kbd_read(void);
static void *vfs_alloc(unsigned int sz) { return kmalloc(sz); }
static void vfs_free(void *p) { kfree(p); }
static void vfs_console_putc(char c) { putc(c); }
static int vfs_console_getc(void) { return kbd_read(); }
#endif

struct vfs_node {
    int used;
    uint32_t type;
    int parent;
    char name[VFS_NAME_MAX];
    uint8_t *data;
    uint32_t size;
    uint32_t cap;
    uint32_t mtime;
    uint32_t readonly;
    uint32_t disk_backed;
    uint32_t fat_cluster;
};

struct vfs_fd {
    int used;
    int node;
    uint32_t flags;
    uint32_t offset;
};

static struct vfs_node *nodes;
static struct vfs_fd *fds;
static int cwd_node;
static uint32_t node_cap;
static uint32_t fd_cap;

#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
static struct fat32_fs *vfs_fat32;
static int vfs_ata_read(void *ctx, uint32_t lba, uint8_t *sector) {
    (void)ctx;
    return ata_read_sector(lba, sector);
}
static int vfs_ata_write(void *ctx, uint32_t lba, const uint8_t *sector) {
    (void)ctx;
    return ata_write_sector(lba, sector);
}
static int vfs_mount_fat32(void) {
    uint8_t mbr[512];
    uint32_t part = 0;
    int found = 0;
    if (!ata_is_ready() || ata_read_sector(0, mbr) != 0) return -1;
    if (!vfs_fat32) vfs_fat32 = (struct fat32_fs *)vfs_alloc(sizeof(*vfs_fat32));
    if (!vfs_fat32) return -1;
    if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
        for (int i = 0; i < 4; ++i) {
            const uint8_t *e = mbr + 446 + i * 16;
            if ((e[4] == 0x0B || e[4] == 0x0C) &&
                e[8] + ((uint32_t)e[9] << 8) + ((uint32_t)e[10] << 16) +
                ((uint32_t)e[11] << 24)) {
                part = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                       ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
                if (fat32_mount(vfs_fat32, vfs_ata_read, 0, part) == 0) {
                    found = 1;
                    break;
                }
            }
        }
    }
    if (!found && fat32_mount(vfs_fat32, vfs_ata_read, 0, 0) == 0)
        found = 1;
    if (!found) { vfs_free(vfs_fat32); vfs_fat32 = 0; return -1; }
    if (!boot_is_live_session()) fat32_set_writer(vfs_fat32, vfs_ata_write);
    return 0;
}
#endif

static int grow_nodes(void) {
    uint32_t cap = node_cap ? node_cap * 2u : VFS_MAX_NODES;
    if (cap < node_cap || cap > 0x7FFFFFFFu / sizeof(struct vfs_node)) return -1;
    struct vfs_node *next = (struct vfs_node *)vfs_alloc(cap * sizeof(struct vfs_node));
    if (!next) return -1;
    for (uint32_t i = 0; i < node_cap; ++i) next[i] = nodes[i];
    for (uint32_t i = node_cap; i < cap; ++i) {
        next[i].used = 0; next[i].data = 0; next[i].size = 0; next[i].cap = 0;
    }
    if (nodes) vfs_free(nodes);
    nodes = next; node_cap = cap;
    return 0;
}

static int grow_fds(void) {
    uint32_t cap = fd_cap ? fd_cap * 2u : VFS_MAX_FDS;
    if (cap < fd_cap || cap > 0x7FFFFFFFu / sizeof(struct vfs_fd)) return -1;
    struct vfs_fd *next = (struct vfs_fd *)vfs_alloc(cap * sizeof(struct vfs_fd));
    if (!next) return -1;
    for (uint32_t i = 0; i < fd_cap; ++i) next[i] = fds[i];
    for (uint32_t i = fd_cap; i < cap; ++i) {
        next[i].used = 0; next[i].node = -1; next[i].flags = 0; next[i].offset = 0;
    }
    if (fds) vfs_free(fds);
    fds = next; fd_cap = cap;
    return 0;
}

static int s_len(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int s_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static void s_copy(char *dst, const char *src, int max) {
    int i = 0;
    if (src) {
        while (src[i] && i < max - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static int find_child(int dir, const char *name) {
    for (uint32_t i = 0; i < node_cap; i++) {
        if (nodes[i].used && nodes[i].parent == dir && s_eq(nodes[i].name, name))
            return i;
    }
    return -1;
}

static int resolve_path(const char *path, int *parent_out, char *leaf_out) {
    if (!nodes || !path || !path[0]) return -1;
    int cur = (path[0] == '/') ? 0 : cwd_node;
    int i = 0;
    if (parent_out) *parent_out = cur;
    if (leaf_out) leaf_out[0] = 0;

    while (path[i]) {
        while (path[i] == '/') i++;
        if (!path[i]) break;
        char part[VFS_NAME_MAX];
        int p = 0;
        while (path[i] && path[i] != '/' && p < VFS_NAME_MAX - 1)
            part[p++] = path[i++];
        part[p] = 0;
        while (path[i] && path[i] != '/') i++;

        int has_more = 0;
        for (int j = i; path[j]; j++) {
            if (path[j] != '/') { has_more = 1; break; }
        }

        if (s_eq(part, ".")) {
            continue;
        } else if (s_eq(part, "..")) {
            if (nodes[cur].parent >= 0) cur = nodes[cur].parent;
            continue;
        }

        if (!has_more) {
            if (parent_out) *parent_out = cur;
            if (leaf_out) s_copy(leaf_out, part, VFS_NAME_MAX);
        }

        int next = find_child(cur, part);
        if (next < 0) {
            if (!has_more) return -1;
            return -2;
        }
        cur = next;
        if (has_more && nodes[cur].type != VFS_TYPE_DIR)
            return -2;
    }
    return cur;
}

static int alloc_node(int parent, const char *name, uint32_t type) {
    for (uint32_t i = 1; i < node_cap; i++) {
        if (!nodes[i].used) {
            nodes[i].used = 1;
            nodes[i].type = type;
            nodes[i].parent = parent;
            s_copy(nodes[i].name, name, VFS_NAME_MAX);
            nodes[i].data = 0;
            nodes[i].size = 0;
            nodes[i].cap = 0;
            nodes[i].mtime = 0;
            nodes[i].readonly = 0;
            nodes[i].disk_backed = 0;
        nodes[i].fat_cluster = 0;
            return i;
        }
    }
    if (grow_nodes() != 0) return -1;
    return alloc_node(parent, name, type);
}


int vfs_has_persistent_storage(void) {
#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
    return (vfs_fat32 && vfs_fat32->write_sector) ? 1 : 0;
#else
    return 0;
#endif
}

#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
static uint32_t node_fat_dir_cluster(int node_idx) {
    if (!vfs_fat32 || node_idx < 0) return 0;
    int p = nodes[node_idx].parent;
    if (p >= 0 && nodes[p].fat_cluster >= 2u) return nodes[p].fat_cluster;
    return vfs_fat32->root_cluster;
}

static void vfs_import_fat32_dir(int parent_node, uint32_t dir_cluster, int depth) {
    if (!vfs_fat32 || parent_node < 0 || depth > 8) return;
    for (uint32_t idx = 0; idx < vfs_fat32->cluster_count * 16u; ++idx) {
        struct fat32_dirent ent;
        if (fat32_readdir(vfs_fat32, dir_cluster, idx, &ent) != 0) break;
        if (!ent.name[0] || s_eq(ent.name, ".") || s_eq(ent.name, "..")) continue;
        if (ent.attr & FAT32_ATTR_DIR) {
            int dirnode = alloc_node(parent_node, ent.name, VFS_TYPE_DIR);
            if (dirnode >= 0) {
                nodes[dirnode].disk_backed = 1;
                nodes[dirnode].fat_cluster = ent.cluster;
                nodes[dirnode].readonly = (vfs_fat32->write_sector == 0);
                if (ent.cluster >= 2u)
                    vfs_import_fat32_dir(dirnode, ent.cluster, depth + 1);
            }
            continue;
        }
        int node = alloc_node(parent_node, ent.name, VFS_TYPE_FILE);
        if (node < 0) break;
        nodes[node].disk_backed = 1;
        nodes[node].fat_cluster = ent.cluster;
        nodes[node].readonly = (vfs_fat32->write_sector == 0) || ((ent.attr & FAT32_ATTR_READONLY) != 0);
        if (ent.size) {
            nodes[node].data = (uint8_t *)vfs_alloc(ent.size);
            if (!nodes[node].data ||
                fat32_read_file(vfs_fat32, &ent, nodes[node].data, ent.size) != (int)ent.size) {
                if (nodes[node].data) vfs_free(nodes[node].data);
                nodes[node].data = 0; nodes[node].used = 0;
                continue;
            }
            nodes[node].size = ent.size;
            nodes[node].cap = ent.size;
        }
    }
}
#endif

#if !defined(FLAIROS_HOST_TEST) || defined(FLAIROS_64BIT)
static void vfs_seed_blob(const char *path, const uint8_t *data, uint32_t len) {
    int fd = vfs_open(path, VFS_O_CREAT | VFS_O_WRONLY | VFS_O_TRUNC);
    if (fd >= 0) {
        (void)vfs_write(fd, data, len);
        vfs_close(fd);
    }
}
#endif

int vfs_init(void) {
    if (!nodes && grow_nodes() != 0) return -1;
    if (!fds && grow_fds() != 0) return -1;
    for (uint32_t i = 0; i < node_cap; i++) {
        nodes[i].used = 0;
        nodes[i].data = 0;
        nodes[i].size = 0;
        nodes[i].cap = 0;
        nodes[i].readonly = 0;
        nodes[i].disk_backed = 0;
    }
    for (uint32_t i = 0; i < fd_cap; i++) {
        fds[i].used = 0;
        fds[i].node = -1;
        fds[i].flags = 0;
        fds[i].offset = 0;
    }
    /* Root "/" at node 0 */
    nodes[0].used = 1;
    nodes[0].type = VFS_TYPE_DIR;
    nodes[0].parent = -1;
    s_copy(nodes[0].name, "/", VFS_NAME_MAX);
    cwd_node = 0;

    /* Standard FDs: 0=stdin, 1=stdout, 2=stderr */
    for (int i = 0; i < 3; i++) {
        fds[i].used = 1;
        fds[i].node = -1;
        fds[i].flags = (i == 0) ? VFS_O_RDONLY : VFS_O_WRONLY;
    }
    vfs_mkdir("/bin");
    vfs_mkdir("/lib");
    vfs_mkdir("/home");
#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
    if (vfs_mkdir("/storage") == 0 && vfs_mount_fat32() == 0) {
        int storage = find_child(0, "storage");
        if (storage >= 0) {
            nodes[storage].disk_backed = 1;
            nodes[storage].fat_cluster = vfs_fat32->root_cluster;
            vfs_import_fat32_dir(storage, vfs_fat32->root_cluster, 0);
        }
    }
    (void)vfs_mkdir("/apps");
    vfs_seed_blob("/apps/hello.apk", hello_apk_bytes, (uint32_t)sizeof(hello_apk_bytes));
    vfs_seed_blob("/apps/calc.apk", calc_apk_bytes, (uint32_t)sizeof(calc_apk_bytes));
    vfs_seed_blob("/apps/sysinfo.apk", sysinfo_apk_bytes, (uint32_t)sizeof(sysinfo_apk_bytes));
#elif defined(FLAIROS_64BIT)
    (void)vfs_mkdir("/apps");
    vfs_seed_blob("/apps/hello.apk", hello_apk_bytes, (uint32_t)sizeof(hello_apk_bytes));
    vfs_seed_blob("/apps/calc.apk", calc_apk_bytes, (uint32_t)sizeof(calc_apk_bytes));
    vfs_seed_blob("/apps/sysinfo.apk", sysinfo_apk_bytes, (uint32_t)sizeof(sysinfo_apk_bytes));
    {
        static const char note0[] = "Welcome to FlairOS macOS Desktop.\nEdit this note and click Save.\n";
        vfs_seed_blob("/apps/HelloFlair.class", hello_flair_class_bytes, (uint32_t)sizeof(hello_flair_class_bytes));
        vfs_seed_generated_apps(vfs_seed_blob);
        static const char py0[] = "x = 6 * 7\nprint(\"Python on FlairOS:\", x)\n";
        vfs_seed_blob("/home/NOTE.TXT", (const uint8_t *)note0, (uint32_t)(sizeof(note0) - 1));
        vfs_seed_blob("/home/hello.py", (const uint8_t *)py0, (uint32_t)(sizeof(py0) - 1));
    }
#endif
    return 0;
}

int vfs_mkdir(const char *path) {
    int parent = -1;
    char leaf[VFS_NAME_MAX];
    int existing = resolve_path(path, &parent, leaf);
    if (existing >= 0 || existing == -2 || parent < 0 || !leaf[0])
        return -1;
    if (nodes[parent].type != VFS_TYPE_DIR)
        return -1;
#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
    if (nodes[parent].disk_backed && nodes[parent].fat_cluster >= 2u) {
        struct fat32_dirent ent;
        if (!vfs_fat32 || !vfs_fat32->write_sector) return -1;
        if (fat32_mkdir(vfs_fat32, nodes[parent].fat_cluster, leaf, &ent) != 0) return -1;
        int dn = alloc_node(parent, leaf, VFS_TYPE_DIR);
        if (dn < 0) return -1;
        nodes[dn].disk_backed = 1;
        nodes[dn].fat_cluster = ent.cluster;
        return 0;
    }
#endif
    return alloc_node(parent, leaf, VFS_TYPE_DIR) >= 0 ? 0 : -1;
}

int vfs_unlink(const char *path) {
    int node = resolve_path(path, 0, 0);
    if (node <= 0 || nodes[node].readonly) return -1;
    if (nodes[node].type == VFS_TYPE_DIR) {
        for (uint32_t i = 1; i < node_cap; i++) {
            if (nodes[i].used && nodes[i].parent == node)
                return -1; /* directory not empty */
        }
    }
    for (uint32_t i = 3; i < fd_cap; i++) {
        if (fds[i].used && fds[i].node == node)
            return -1;
    }
#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
    if (nodes[node].disk_backed && fat32_delete_file(vfs_fat32, node_fat_dir_cluster(node), nodes[node].name) != 0) return -1;
#endif
    if (nodes[node].data) vfs_free(nodes[node].data);
    nodes[node].data = 0;
    nodes[node].used = 0;
    return 0;
}

int vfs_stat(const char *path, struct vfs_stat *out) {
    int node = resolve_path(path, 0, 0);
    if (node < 0) return -1;
    if (out) {
        out->size = nodes[node].size;
        out->type = nodes[node].type;
        out->mtime = nodes[node].mtime;
    }
    return 0;
}

int vfs_open(const char *path, uint32_t flags) {
    int parent = -1, created_disk = 0;
    char leaf[VFS_NAME_MAX];
    int node = resolve_path(path, &parent, leaf);
    if (node == -2) return -1;
    if (node < 0) {
        if (!(flags & VFS_O_CREAT) || parent < 0 || !leaf[0]) return -1;
#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
        int storage = find_child(0, "storage");
        if (parent == storage) {
            if (!vfs_fat32 || !vfs_fat32->write_sector) return -1;
            if (fat32_create_file(vfs_fat32, vfs_fat32->root_cluster, leaf, 0) != 0) return -1;
            created_disk = 1;
        }
#endif
        node = alloc_node(parent, leaf, VFS_TYPE_FILE);
        if (node < 0) {
#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
            if (created_disk) fat32_delete_file(vfs_fat32, vfs_fat32->root_cluster, leaf);
#endif
            return -1;
        }
        nodes[node].disk_backed = created_disk;
    }
    if (nodes[node].type == VFS_TYPE_DIR) return -1;
    if (nodes[node].readonly && ((flags & 3u) != VFS_O_RDONLY || (flags & VFS_O_TRUNC))) return -1;
    if (flags & VFS_O_TRUNC) {
#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
        if (nodes[node].disk_backed && fat32_write_file(vfs_fat32, vfs_fat32->root_cluster, nodes[node].name, 0, 0) != 0) return -1;
#endif
        nodes[node].size = 0;
    }
    for (uint32_t fd = 3; fd < fd_cap; fd++) {
        if (!fds[fd].used) {
            fds[fd].used = 1; fds[fd].node = node; fds[fd].flags = flags;
            fds[fd].offset = (flags & VFS_O_APPEND) ? nodes[node].size : 0;
            return fd;
        }
    }
    if (grow_fds() != 0) return -1;
    return vfs_open(path, flags);
}

int vfs_close(int fd) {
    if (fd < 3 || (uint32_t)fd >= fd_cap || !fds[fd].used) return -1;
    fds[fd].used = 0;
    fds[fd].node = -1;
    return 0;
}

int vfs_read(int fd, void *buf, uint32_t count) {
    if (fd < 0 || (uint32_t)fd >= fd_cap || !fds[fd].used || !buf) return -1;
    if (fd == 0) {
        uint8_t *dst = (uint8_t *)buf;
        uint32_t n = 0;
        while (n < count) {
            int c = vfs_console_getc();
            if (c < 0) break;
            dst[n++] = (uint8_t)c;
        }
        return (int)n;
    }
    if (fds[fd].node < 0) return -1;
    struct vfs_node *n = &nodes[fds[fd].node];
    if (fds[fd].offset >= n->size) return 0;
    uint32_t avail = n->size - fds[fd].offset;
    uint32_t to_read = count < avail ? count : avail;
    const uint8_t *src = n->data + fds[fd].offset;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < to_read; i++) dst[i] = src[i];
    fds[fd].offset += to_read;
    return (int)to_read;
}

int vfs_write(int fd, const void *buf, uint32_t count) {
    if (fd < 0 || (uint32_t)fd >= fd_cap || !fds[fd].used || !buf) return -1;
    const uint8_t *src = (const uint8_t *)buf;
    if (fd == 1 || fd == 2) {
        for (uint32_t i = 0; i < count; i++) vfs_console_putc((char)src[i]);
        return (int)count;
    }
    if (fds[fd].node < 0 || (fds[fd].flags & 3u) == VFS_O_RDONLY) return -1;
    struct vfs_node *n = &nodes[fds[fd].node];
    if (n->readonly) return -1;
    uint32_t end = fds[fd].offset + count;
    if (end < fds[fd].offset) return -1;
    uint32_t new_size = end > n->size ? end : n->size;
    uint32_t new_cap = n->cap;
    if (new_cap < new_size) {
        new_cap = new_cap ? new_cap : 256u;
        while (new_cap < new_size) {
            if (new_cap > 0x7FFFFFFFu / 2u) return -1;
            new_cap *= 2u;
        }
    }
    uint8_t *candidate = n->data;
    int replace = new_cap != n->cap;
#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
    if (n->disk_backed) replace = 1;
#endif
    if (replace && new_cap == 0) new_cap = 1;
    if (replace) {
        candidate = (uint8_t *)vfs_alloc(new_cap);
        if (!candidate) return -1;
        for (uint32_t i = 0; i < n->size; ++i) candidate[i] = n->data[i];
    }
    for (uint32_t i = 0; i < count; ++i) candidate[fds[fd].offset + i] = src[i];
#if !defined(FLAIROS_HOST_TEST) && !defined(FLAIROS_64BIT)
    if (n->disk_backed && fat32_write_file(vfs_fat32, vfs_fat32->root_cluster, n->name, candidate, new_size) != 0) {
        if (replace) vfs_free(candidate);
        return -1;
    }
#endif
    if (replace) { if (n->data) vfs_free(n->data); n->data = candidate; n->cap = new_cap; }
    fds[fd].offset = end;
    if (end > n->size) n->size = end;
    return (int)count;
}

int vfs_seek(int fd, int32_t offset, int whence) {
    if (fd < 3 || (uint32_t)fd >= fd_cap || !fds[fd].used || fds[fd].node < 0) return -1;
    struct vfs_node *n = &nodes[fds[fd].node];
    int32_t base = 0;
    if (whence == VFS_SEEK_CUR) base = (int32_t)fds[fd].offset;
    else if (whence == VFS_SEEK_END) base = (int32_t)n->size;
    else if (whence != VFS_SEEK_SET) return -1;
    int32_t pos = base + offset;
    if (pos < 0 || (uint32_t)pos > n->size) return -1;
    fds[fd].offset = (uint32_t)pos;
    return pos;
}

int vfs_readdir(const char *path, uint32_t index, struct vfs_dirent *out) {
    int dir = resolve_path(path, 0, 0);
    if (dir < 0 || nodes[dir].type != VFS_TYPE_DIR) return -1;
    uint32_t seen = 0;
    for (uint32_t i = 1; i < node_cap; i++) {
        if (nodes[i].used && nodes[i].parent == dir) {
            if (seen == index) {
                if (out) {
                    s_copy(out->name, nodes[i].name, VFS_NAME_MAX);
                    out->type = nodes[i].type;
                    out->size = nodes[i].size;
                }
                return 0;
            }
            seen++;
        }
    }
    return -1;
}

int vfs_chdir(const char *path) {
    int dir = resolve_path(path, 0, 0);
    if (dir < 0 || nodes[dir].type != VFS_TYPE_DIR) return -1;
    cwd_node = dir;
    return 0;
}

int vfs_getcwd(char *buf, uint32_t size) {
    if (!buf || size < 2) return -1;
    if (cwd_node == 0) {
        buf[0] = '/';
        buf[1] = 0;
        return 0;
    }
    int *chain = (int *)vfs_alloc(node_cap * sizeof(int));
    if (!chain) return -1;
    int depth = 0;
    int cur = cwd_node;
    while (cur > 0 && (uint32_t)depth < node_cap) {
        chain[depth++] = cur;
        cur = nodes[cur].parent;
    }
    if (cur > 0) { vfs_free(chain); return -1; }
    uint32_t pos = 0;
    for (int i = depth - 1; i >= 0; i--) {
        if (pos + 1 >= size) { vfs_free(chain); return -1; }
        buf[pos++] = '/';
        const char *nm = nodes[chain[i]].name;
        for (int j = 0; nm[j]; j++) {
            if (pos + 1 >= size) { vfs_free(chain); return -1; }
            buf[pos++] = nm[j];
        }
    }
    buf[pos] = 0;
    vfs_free(chain);
    (void)s_len;
    return 0;
}

int vfs_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || (uint32_t)oldfd >= fd_cap || !fds[oldfd].used || newfd < 0)
        return -1;
    if (oldfd == newfd) return newfd;
    while ((uint32_t)newfd >= fd_cap)
        if (grow_fds() != 0) return -1;
    fds[newfd] = fds[oldfd];
    return newfd;
}
