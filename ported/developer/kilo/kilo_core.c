/* SPDX-License-Identifier: AGPL-3.0-only */
/* FlairOS Kilo-inspired Compact Text Editor Core
 * Works with both FlairOS SDK (sdk/) and 64-bit /ported/developer/libs/flair_posix.
 */
#include "kilo_core.h"

#ifdef FLAIROS_SDK_APP
#include "flareos.h"
#include "stdio.h"
#include "string.h"
#include "flair_posix.h"
#define KL_OPEN(p, f)      posix_open((p), (unsigned int)(f))
#define KL_READ(fd, b, n)  posix_read((fd), (b), (flare_u32)(n))
#define KL_WRITE(fd, b, n) posix_write((fd), (b), (flare_u32)(n))
#define KL_CLOSE(fd)       posix_close(fd)
#define KL_O_RDONLY        1u
#define KL_O_WRONLY        2u
#define KL_O_CREAT         4u
#define KL_O_TRUNC         8u
#else
#include "../libs/flair_posix/flair_posix.h"
#include "../../../include/vfs.h"
#define KL_OPEN(p, f)      flair_open((p), (f))
#define KL_READ(fd, b, n)  flair_read((fd), (b), (n))
#define KL_WRITE(fd, b, n) flair_write((fd), (b), (n))
#define KL_CLOSE(fd)       flair_close(fd)
#define KL_O_RDONLY        VFS_O_RDONLY
#define KL_O_WRONLY        VFS_O_WRONLY
#define KL_O_CREAT         VFS_O_CREAT
#define KL_O_TRUNC         VFS_O_TRUNC
#endif

static void k_cpy(char *d, const char *s, int cap) {
    int i = 0;
    if (!d || cap <= 0) return;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    d[i] = 0;
}

void kilo_init(struct kilo_state *ks) {
    if (!ks) return;
    k_cpy(ks->path, "/home/NOTE.TXT", sizeof(ks->path));
    ks->buf[0] = 0;
    ks->len = 0;
    ks->cursor = 0;
    ks->dirty = 0;
    k_cpy(ks->status, "Kilo Ready", sizeof(ks->status));
}

int kilo_open_file(struct kilo_state *ks, const char *path) {
    if (!ks || !path) return -1;
    k_cpy(ks->path, path, sizeof(ks->path));
    int fd = KL_OPEN(path, KL_O_RDONLY);
    ks->len = 0;
    if (fd >= 0) {
        int n = KL_READ(fd, ks->buf, KILO_MAX_TEXT - 1);
        KL_CLOSE(fd);
        if (n > 0) ks->len = n;
    }
    ks->buf[ks->len] = 0;
    ks->cursor = ks->len;
    ks->dirty = 0;
    k_cpy(ks->status, path, sizeof(ks->status));
    return 0;
}

int kilo_save_file(struct kilo_state *ks) {
    if (!ks || !ks->path[0]) return -1;
    int fd = KL_OPEN(ks->path, KL_O_CREAT | KL_O_WRONLY | KL_O_TRUNC);
    if (fd < 0) { k_cpy(ks->status, "Save failed", sizeof(ks->status)); return -1; }
    int n = KL_WRITE(fd, ks->buf, ks->len);
    KL_CLOSE(fd);
    if (n == ks->len) {
        ks->dirty = 0;
        k_cpy(ks->status, "Saved", sizeof(ks->status));
        return 0;
    }
    k_cpy(ks->status, "Write error", sizeof(ks->status));
    return -1;
}

int kilo_handle_key(struct kilo_state *ks, int key) {
    if (!ks) return 0;
    if (key == '\b') {
        if (ks->cursor > 0 && ks->len > 0) {
            for (int i = ks->cursor - 1; i < ks->len - 1; i++)
                ks->buf[i] = ks->buf[i + 1];
            ks->len--;
            ks->cursor--;
            ks->buf[ks->len] = 0;
            ks->dirty = 1;
            return 1;
        }
        return 0;
    }
    if ((key == '\n' || (key >= 32 && key < 127)) && ks->len + 1 < KILO_MAX_TEXT) {
        for (int i = ks->len; i > ks->cursor; i--)
            ks->buf[i] = ks->buf[i - 1];
        ks->buf[ks->cursor++] = (char)key;
        ks->len++;
        ks->buf[ks->len] = 0;
        ks->dirty = 1;
        return 1;
    }
    return 0;
}

#ifdef FLAIROS_SDK_APP
FLARE_APP();
void app_main(void) {
    struct kilo_state ks;
    kilo_init(&ks);
    (void)kilo_open_file(&ks, "/NOTE.TXT");
    kilo_handle_key(&ks, '!');
    puts("Kilo Editor (SDK build) ready");
}
#endif
