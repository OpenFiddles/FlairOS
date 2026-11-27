#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airgraph.h"
#include "airp.h"
#include "input64.h"
#include "vfs.h"
#include "../ui/shells/shell_common.h"
#include "../net/service/net_service.h"
#include "../net/bt/bt_core.h"
#include "../net/airp/airp_net.h"
#include "ui/mobile_layout.h"
#include "ui/shell_launcher.h"

/* Proof on the build host that the *shipped* session packages are readable by
   the real kernel loader and can drive the real phone shell's draw path.

   The inputs are `build/FlairMobile.airp`, `build/FlairTV.airp` and
   `build/FlairDesktop.airp` exactly as `make` packs them from `mobile/`, `tv/`
   and `desktop/` resources -- not a fixture assembled in this file. The
   executed image is checked separately by `tools/verify_disk_boot.py`, which
   requires the kernel's own `session=pkg-load ok` line before it accepts a
   frame. */

static int failures;

static void check(int cond, const char *what) {
    printf("%s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) failures++;
}

static unsigned char *read_airp(const char *path, unsigned int *len) {
    FILE *f = fopen(path, "rb");
    unsigned char *data;
    long size;
    if (!f) {
        printf("FAIL could not open %s (run `make test` so the package is packed)\n", path);
        failures++;
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (unsigned char *)malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        failures++;
        return 0;
    }
    fclose(f);
    *len = (unsigned int)size;
    return data;
}

static void expect_layout(const char *label, const char *path, enum shell_kind kind,
                          int tiles, int dock, int strings) {
    unsigned int len = 0;
    unsigned char *bytes = read_airp(path, &len);
    struct shell_launcher launcher;
    struct shell_launcher_report report;
    char msg[160];

    if (!bytes) return;
    memset(&launcher, 0, sizeof launcher);
    snprintf(msg, sizeof msg, "%s: the shipped package loads through shell_launcher", label);
    check(shell_launcher_load_package(bytes, len, kind, &launcher, &report) == 1 &&
              report.loaded, msg);
    snprintf(msg, sizeof msg, "%s: %d tiles, %d dock slots, %d strings", label, tiles, dock, strings);
    check(report.tiles == tiles && report.dock == dock && report.strings == strings, msg);
    snprintf(msg, sizeof msg, "%s: the packaged tiles carry ids, labels and targets", label);
    check(launcher.tile_count == tiles && launcher.tiles[0].id[0] &&
              launcher.tiles[0].label[0] && launcher.tiles[0].target[0], msg);
    free(bytes);
}

/* --- Minimal kernel services the phone shell links against ----------------- */

static const struct net_service_state g_net;
static struct vfs_dirent g_empty_dir;
static const struct art_activity *g_art;
static struct bt_adapter_status g_bt;
static struct airp_session g_airp;

void net_service_init(void) {}
void bt_core_init(void) {}
int bt_core_power(int on) { (void)on; return 0; }
int bt_core_toggle_power(void) { return 0; }
int bt_core_scan_start(void) { return 0; }
int bt_core_connect(int idx) { (void)idx; return 0; }
int bt_core_disconnect(int idx) { (void)idx; return 0; }
void bt_core_recover(void) {}
void bt_core_tick(void) {}
const struct bt_adapter_status *bt_core_status(void) { return &g_bt; }
int bt_core_device_count(void) { return 0; }
const struct bt_device *bt_core_device_get(int idx) { (void)idx; return 0; }

int airp_discover_start(void) { return 0; }
int airp_peer_count(void) { return 0; }
int airp_pair_peer(int peer_idx, unsigned int pin) { (void)peer_idx; (void)pin; return -1; }
int airp_start_stream(enum airp_payload_type type, const char *label, unsigned int bytes) {
    (void)type; (void)label; (void)bytes; return 0;
}
void airp_disconnect(void) {}
const struct airp_session *airp_session_status(void) { return &g_airp; }
const char *airp_state_name(enum airp_session_state st) { (void)st; return "Idle"; }

void net_service_sync(void) {}
const struct net_service_state *net_service_state(void) { return &g_net; }
void net_service_toggle_wifi(void) {}
void net_service_toggle_cell(void) {}
void net_service_toggle_airplane(void) {}

int vfs_open(const char *path, uint32_t flags) { (void)path; (void)flags; return -1; }
int vfs_read(int fd, void *buf, unsigned int len) { (void)fd; (void)buf; (void)len; return -1; }
int vfs_write(int fd, const void *buf, unsigned int len) { (void)fd; (void)buf; (void)len; return -1; }
int vfs_close(int fd) { (void)fd; return 0; }
int vfs_readdir(const char *path, unsigned int idx, struct vfs_dirent *out) {
    (void)path; (void)idx; (void)out;
    memset(&g_empty_dir, 0, sizeof g_empty_dir);
    return -1;
}

void *kmem64_alloc(size_t bytes) { return malloc(bytes); }
void kmem64_free(void *p) { free(p); }

const struct art_activity *art_am_active(void) { return g_art; }
int art_am_trigger_button(int idx) { (void)idx; return 0; }
int art_am_launch(int idx) { (void)idx; return 0; }
int art_pm_count(void) { return 0; }

int sf_find_by_tag(const char *tag) { (void)tag; return -1; }
int sf_create_surface(const char *tag, int32_t w, int32_t h) { (void)tag; (void)w; (void)h; return -1; }
uint32_t *sf_lock_buffer(int id, int32_t *w, int32_t *h) { (void)id; (void)w; (void)h; return 0; }
void sf_unlock_and_post(int id) { (void)id; }
void sf_composite_to(int id, struct ag_surface *dst, int32_t x, int32_t y) {
    (void)id; (void)dst; (void)x; (void)y;
}
int sf_destroy_surface(int id) { (void)id; return 0; }

int flair_browser_handle_click(int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)x; (void)y; (void)w; (void)h; return 0;
}
int flair_browser_handle_key(int key) { (void)key; return 0; }
void flair_browser_render(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)s; (void)x; (void)y; (void)w; (void)h;
}


int main(int argc, char **argv) {
    const char *mobile_path = argc > 1 ? argv[1] : "build/FlairMobile.airp";
    const char *tv_path = argc > 2 ? argv[2] : "build/FlairTV.airp";
    const char *desktop_path = argc > 3 ? argv[3] : "build/FlairDesktop.airp";

    expect_layout("mobile", mobile_path, SHELL_KIND_MOBILE, 6, 4, 29);
    expect_layout("tv", tv_path, SHELL_KIND_TV, 3, 0, 76);
    expect_layout("desktop", desktop_path, SHELL_KIND_DESKTOP, 8, 8, 39);

    /* Draw the phone home screen from the shipped bytes. The surface is the
       preset's 480x800 panel, the shell is the real one, and the only inputs
       are the package and the boot info the kernel would hand it. */
    {
        unsigned int len = 0;
        unsigned char *bytes = read_airp(mobile_path, &len);
        if (bytes) {
            struct shell_boot_info info;
            struct ag_surface surface;
            uint32_t *pixels = (uint32_t *)calloc(480u * 800u, sizeof(uint32_t));
            uint32_t checksum = 2166136261u;
            int distinct = 0, drawn = 0, have_first = 0;
            uint32_t first = 0;
            static uint32_t seen[256];
            int seen_count = 0;

            memset(&info, 0, sizeof info);
            info.arch_name = "x86_64";
            info.preset_name = "mobile";
            info.width = 480;
            info.height = 800;
            info.session_pkg = bytes;
            info.session_pkg_len = len;

            ag_surface_init(&surface, pixels, 480, 800, 480u * 4u, AG_PIXEL_XRGB8888);
            ag_clear(&surface, 0x00112233u);
            shell_render(&surface, &info, 0);
            for (int i = 0; i < 480 * 800; i++) {
                uint32_t px = pixels[i] & 0x00FFFFFFu;
                checksum ^= px;
                checksum *= 16777619u;
                if (px != 0x00112233u) {
                    int known = 0;
                    drawn++;
                    if (!have_first) { first = px; have_first = 1; }
                    if (px != first) distinct = 1;
                    for (int k = 0; k < seen_count; k++)
                        if (seen[k] == px) { known = 1; break; }
                    if (!known && seen_count < 256) seen[seen_count++] = px;
                }
            }
            printf("     mobile frame: %d drawn pixels, %d distinct colours\n", drawn, seen_count);
            check(checksum != 2166136261u, "mobile: the shipped package renders pixels");
            check(drawn == 480 * 800, "mobile: the whole panel is painted");
            check(seen_count > 32 && distinct,
                  "mobile: the frame is a composed home screen, not one fill");
            free(pixels);
            free(bytes);
        }
    }

    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
