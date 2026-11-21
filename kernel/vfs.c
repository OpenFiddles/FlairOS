#include "../include/vfs.h"

#ifdef FLAIROS_HOST_TEST
#include <stdlib.h>
static void *vfs_alloc(unsigned int sz) { return malloc(sz); }
static void vfs_free(void *p) { free(p); }
static void vfs_console_putc(char c) { (void)c; }
static int vfs_console_getc(void) { return -1; }
#else
#include "../include/kheap.h"
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
    for (int i = 0; i < VFS_MAX_NODES; i++) {
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
    for (int i = 1; i < VFS_MAX_NODES; i++) {
        if (!nodes[i].used) {
            nodes[i].used = 1;
            nodes[i].type = type;
            nodes[i].parent = parent;
            s_copy(nodes[i].name, name, VFS_NAME_MAX);
            nodes[i].data = 0;
            nodes[i].size = 0;
            nodes[i].cap = 0;
            nodes[i].mtime = 0;
            return i;
        }
    }
    return -1;
}

int vfs_init(void) {
    if (!nodes) nodes = (struct vfs_node *)vfs_alloc(sizeof(struct vfs_node) * VFS_MAX_NODES);
    if (!fds) fds = (struct vfs_fd *)vfs_alloc(sizeof(struct vfs_fd) * VFS_MAX_FDS);
    if (!nodes || !fds) return -1;
    for (int i = 0; i < VFS_MAX_NODES; i++) {
        nodes[i].used = 0;
        nodes[i].data = 0;
        nodes[i].size = 0;
        nodes[i].cap = 0;
    }
    for (int i = 0; i < VFS_MAX_FDS; i++) {
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
    return alloc_node(parent, leaf, VFS_TYPE_DIR) >= 0 ? 0 : -1;
}

int vfs_unlink(const char *path) {
    int node = resolve_path(path, 0, 0);
    if (node <= 0) return -1;
    if (nodes[node].type == VFS_TYPE_DIR) {
        for (int i = 1; i < VFS_MAX_NODES; i++) {
            if (nodes[i].used && nodes[i].parent == node)
                return -1; /* directory not empty */
        }
    }
    for (int i = 3; i < VFS_MAX_FDS; i++) {
        if (fds[i].used && fds[i].node == node)
            return -1;
    }
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
    int parent = -1;
    char leaf[VFS_NAME_MAX];
    int node = resolve_path(path, &parent, leaf);
    if (node == -2) return -1;
    if (node < 0) {
        if (!(flags & VFS_O_CREAT) || parent < 0 || !leaf[0])
            return -1;
        node = alloc_node(parent, leaf, VFS_TYPE_FILE);
        if (node < 0) return -1;
    }
    if (nodes[node].type == VFS_TYPE_DIR)
        return -1;
    if (flags & VFS_O_TRUNC)
        nodes[node].size = 0;
    for (int fd = 3; fd < VFS_MAX_FDS; fd++) {
        if (!fds[fd].used) {
            fds[fd].used = 1;
            fds[fd].node = node;
            fds[fd].flags = flags;
            fds[fd].offset = (flags & VFS_O_APPEND) ? nodes[node].size : 0;
            return fd;
        }
    }
    return -1;
}

int vfs_close(int fd) {
    if (fd < 3 || fd >= VFS_MAX_FDS || !fds[fd].used) return -1;
    fds[fd].used = 0;
    fds[fd].node = -1;
    return 0;
}

int vfs_read(int fd, void *buf, uint32_t count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].used || !buf) return -1;
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
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].used || !buf) return -1;
    const uint8_t *src = (const uint8_t *)buf;
    if (fd == 1 || fd == 2) {
        for (uint32_t i = 0; i < count; i++) vfs_console_putc((char)src[i]);
        return (int)count;
    }
    if (fds[fd].node < 0) return -1;
    struct vfs_node *n = &nodes[fds[fd].node];
    uint32_t end = fds[fd].offset + count;
    if (end < fds[fd].offset) return -1;
    if (end > n->cap) {
        uint32_t new_cap = n->cap ? n->cap : 256u;
        while (new_cap < end) new_cap *= 2u;
        uint8_t *nb = (uint8_t *)vfs_alloc(new_cap);
        if (!nb) return -1;
        for (uint32_t i = 0; i < n->size; i++) nb[i] = n->data[i];
        if (n->data) vfs_free(n->data);
        n->data = nb;
        n->cap = new_cap;
    }
    for (uint32_t i = 0; i < count; i++) n->data[fds[fd].offset + i] = src[i];
    fds[fd].offset = end;
    if (end > n->size) n->size = end;
    return (int)count;
}

int vfs_seek(int fd, int32_t offset, int whence) {
    if (fd < 3 || fd >= VFS_MAX_FDS || !fds[fd].used || fds[fd].node < 0) return -1;
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
    for (int i = 1; i < VFS_MAX_NODES; i++) {
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
    int chain[VFS_MAX_NODES];
    int depth = 0;
    int cur = cwd_node;
    while (cur > 0 && depth < VFS_MAX_NODES) {
        chain[depth++] = cur;
        cur = nodes[cur].parent;
    }
    uint32_t pos = 0;
    for (int i = depth - 1; i >= 0; i--) {
        if (pos + 1 >= size) return -1;
        buf[pos++] = '/';
        const char *nm = nodes[chain[i]].name;
        for (int j = 0; nm[j]; j++) {
            if (pos + 1 >= size) return -1;
            buf[pos++] = nm[j];
        }
    }
    buf[pos] = 0;
    (void)s_len;
    return 0;
}

int vfs_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= VFS_MAX_FDS || !fds[oldfd].used) return -1;
    if (newfd < 0 || newfd >= VFS_MAX_FDS) return -1;
    if (oldfd == newfd) return newfd;
    fds[newfd] = fds[oldfd];
    return newfd;
}
