/* SPDX-License-Identifier: AGPL-3.0-only */
/* FlairOS Toybox Mini-Coreutils (wc, grep, head, tail, cp, hexdump)
 * Compatible with FlairOS SDK (sdk/) and /ported/developer/libs/flair_posix.
 */
#include "toybox_mini.h"

#ifdef FLAIROS_SDK_APP
#include "flareos.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "flair_posix.h"
#define TB_OPEN(p, f)      posix_open((p), (unsigned int)(f))
#define TB_READ(fd, b, n)  posix_read((fd), (b), (flare_u32)(n))
#define TB_WRITE(fd, b, n) posix_write((fd), (b), (flare_u32)(n))
#define TB_CLOSE(fd)       posix_close(fd)
#define TB_O_RDONLY        1u
#define TB_O_WRONLY        2u
#define TB_O_CREAT         4u
#define TB_O_TRUNC         8u
#else
#include "../libs/flair_posix/flair_posix.h"
#include "../../../include/vfs.h"
#define TB_OPEN(p, f)      flair_open((p), (f))
#define TB_READ(fd, b, n)  flair_read((fd), (b), (n))
#define TB_WRITE(fd, b, n) flair_write((fd), (b), (n))
#define TB_CLOSE(fd)       flair_close(fd)
#define TB_O_RDONLY        VFS_O_RDONLY
#define TB_O_WRONLY        VFS_O_WRONLY
#define TB_O_CREAT         VFS_O_CREAT
#define TB_O_TRUNC         VFS_O_TRUNC
#endif

static int tb_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static int tb_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}
static void tb_cpy(char *d, const char *s, int cap) {
    int i = 0;
    if (!d || cap <= 0) return;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static void tb_cat(char *d, const char *s, int cap) {
    int i = tb_len(d), j = 0;
    while (s && s[j] && i + 1 < cap) d[i++] = s[j++];
    d[i] = 0;
}
static void tb_u32(unsigned int v, char *out, int cap) {
    char tmp[16]; int n = 0, o = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < 15) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (n > 0 && o + 1 < cap) out[o++] = tmp[--n];
    out[o] = 0;
}
static int tb_contains(const char *hay, const char *needle) {
    if (!needle[0]) return 1;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && hay[i + j] == needle[j]) j++;
        if (!needle[j]) return 1;
    }
    return 0;
}
static void tb_resolve(char *out, int cap, const char *cwd, const char *p) {
    if (!p || !p[0]) { tb_cpy(out, cwd ? cwd : "/", cap); return; }
    if (p[0] == '/') { tb_cpy(out, p, cap); return; }
    tb_cpy(out, cwd ? cwd : "/", cap);
    int n = tb_len(out);
    if (n > 0 && out[n - 1] != '/' && n + 1 < cap) { out[n++] = '/'; out[n] = 0; }
    tb_cat(out, p, cap);
}

static int read_all(const char *path, char *buf, int cap) {
    int fd = TB_OPEN(path, TB_O_RDONLY);
    if (fd < 0) return -1;
    int n = TB_READ(fd, buf, cap - 1);
    TB_CLOSE(fd);
    if (n < 0) return -1;
    buf[n] = 0;
    return n;
}

int toybox_run_cmd(int argc, const char **argv, const char *cwd,
                   toybox_emit_fn emit, void *ctx) {
    static char buf[4096];
    char path[128], path2[128], line[128];
    if (argc < 1 || !argv[0] || !emit) return -1;

    if (tb_eq(argv[0], "wc") && argc >= 2) {
        tb_resolve(path, sizeof(path), cwd, argv[1]);
        int n = read_all(path, buf, sizeof(buf));
        if (n < 0) { emit("wc: cannot open file", ctx); return -1; }
        unsigned int lines = 0, words = 0; int in_w = 0;
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') lines++;
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') in_w = 0;
            else if (!in_w) { in_w = 1; words++; }
        }
        char a[16], b[16], c[16];
        tb_u32(lines, a, sizeof(a)); tb_u32(words, b, sizeof(b)); tb_u32((unsigned int)n, c, sizeof(c));
        tb_cpy(line, "lines=", sizeof(line)); tb_cat(line, a, sizeof(line));
        tb_cat(line, " words=", sizeof(line)); tb_cat(line, b, sizeof(line));
        tb_cat(line, " bytes=", sizeof(line)); tb_cat(line, c, sizeof(line));
        emit(line, ctx);
        return 0;
    }

    if (tb_eq(argv[0], "grep") && argc >= 3) {
        tb_resolve(path, sizeof(path), cwd, argv[2]);
        int n = read_all(path, buf, sizeof(buf));
        if (n < 0) { emit("grep: cannot open file", ctx); return -1; }
        int i = 0, matches = 0;
        while (i < n) {
            int o = 0;
            while (i < n && buf[i] != '\n' && o + 1 < (int)sizeof(line)) {
                if (buf[i] != '\r') line[o++] = buf[i];
                i++;
            }
            if (i < n && buf[i] == '\n') i++;
            line[o] = 0;
            if (tb_contains(line, argv[1])) { emit(line, ctx); matches++; }
        }
        if (!matches) emit("grep: no matches", ctx);
        return 0;
    }

    if ((tb_eq(argv[0], "head") || tb_eq(argv[0], "tail")) && argc >= 2) {
        tb_resolve(path, sizeof(path), cwd, argv[1]);
        int n = read_all(path, buf, sizeof(buf));
        if (n < 0) { emit("cannot open file", ctx); return -1; }
        int is_head = tb_eq(argv[0], "head");
        int total_lines = 0;
        for (int i = 0; i < n; i++) if (buf[i] == '\n') total_lines++;
        if (n > 0 && buf[n - 1] != '\n') total_lines++;
        int start = is_head ? 0 : (total_lines > 5 ? total_lines - 5 : 0);
        int cur = 0, i = 0, shown = 0;
        while (i < n && shown < 5) {
            int o = 0;
            while (i < n && buf[i] != '\n' && o + 1 < (int)sizeof(line)) {
                if (buf[i] != '\r') line[o++] = buf[i];
                i++;
            }
            if (i < n && buf[i] == '\n') i++;
            line[o] = 0;
            if (cur >= start) { emit(line, ctx); shown++; }
            cur++;
        }
        return 0;
    }

    if (tb_eq(argv[0], "cp") && argc >= 3) {
        tb_resolve(path, sizeof(path), cwd, argv[1]);
        tb_resolve(path2, sizeof(path2), cwd, argv[2]);
        int n = read_all(path, buf, sizeof(buf));
        if (n < 0) { emit("cp: source not found", ctx); return -1; }
        int fd = TB_OPEN(path2, TB_O_CREAT | TB_O_WRONLY | TB_O_TRUNC);
        if (fd < 0) { emit("cp: cannot create target", ctx); return -1; }
        int w = TB_WRITE(fd, buf, n);
        TB_CLOSE(fd);
        emit(w == n ? "cp: ok" : "cp: write error", ctx);
        return w == n ? 0 : -1;
    }

    if (tb_eq(argv[0], "hexdump") && argc >= 2) {
        static const char hex[] = "0123456789ABCDEF";
        tb_resolve(path, sizeof(path), cwd, argv[1]);
        int n = read_all(path, buf, sizeof(buf));
        if (n < 0) { emit("hexdump: cannot open file", ctx); return -1; }
        int lim = n > 32 ? 32 : n;
        for (int off = 0; off < lim; off += 8) {
            int o = 0;
            line[o++] = hex[(off >> 4) & 15]; line[o++] = hex[off & 15];
            line[o++] = ':'; line[o++] = ' ';
            for (int j = 0; j < 8 && off + j < lim; j++) {
                unsigned char b = (unsigned char)buf[off + j];
                line[o++] = hex[(b >> 4) & 15];
                line[o++] = hex[b & 15];
                line[o++] = ' ';
            }
            line[o] = 0;
            emit(line, ctx);
        }
        return 0;
    }

    return -2;
}

#ifdef FLAIROS_SDK_APP
static void sdk_emit(const char *line, void *ctx) {
    (void)ctx;
    puts(line);
}
FLARE_APP();
void app_main(void) {
    const char *argv[2] = { "wc", "/NOTE.TXT" };
    puts("Toybox Mini-Coreutils (SDK build)");
    (void)toybox_run_cmd(2, argv, "/", sdk_emit, 0);
}
#endif
