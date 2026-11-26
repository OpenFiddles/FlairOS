#include "kernel.h"
#include "../../include/desktop_gfx.h"
#include "../../include/airgraph.h"
#include "../../include/sprunkui.h"
#include "../../include/drv_touch.h"
#include "../../include/vfs.h"
#include "../../include/apk.h"
#include "../../include/kheap.h"
#include "../../include/cursor.h"

#define MOBILE_BG       0x00131A2B
#define MOBILE_SURFACE  0x00202B40
#define MOBILE_CARD     0x002B3850
#define MOBILE_ACCENT   0x007C83FF
#define MOBILE_TEXT     0x00F2F4FA
#define MOBILE_MUTED    0x00AAB5C8
#define MOBILE_APK_MAX  12
#define MOBILE_NAME_MAX 48

enum mobile_page { MOBILE_HOME, MOBILE_DRAWER, MOBILE_INSTALL, MOBILE_DETAILS };

struct mobile_apk {
    char name[MOBILE_NAME_MAX];
    unsigned int size;
};

static struct mobile_apk *installed_apks;
static struct mobile_apk *storage_apks;
static unsigned int installed_count;
static unsigned int storage_count;
static enum mobile_page page;
static int selected_apk = -1;
static int right_was_down;
static int left_was_down;
static unsigned int left_press_started;
static int left_press_x, left_press_y;
static int touch_press_x, touch_press_y;
static unsigned int touch_press_started;
static struct apk_info detail_info;
static int detail_info_valid;

extern u32 cursor_norm_x, cursor_norm_y;
extern volatile u8 mouse_buttons;
extern volatile u32 mouse_press_x, mouse_press_y;
u8 mouse_take_clicks(void);
void axi_poll_mouse(void);
void axi_gfx_render(void);
void desktop_present_app(void);
int airp_load(const u8 *file, u32 len, u32 *entry);
int vm_run_app(u32 entry, u32 user_esp);
int axi_apk_launch_path(const char *path);
int axi_apk_install_path(const char *src_path, const char *name);

#define MOBILE_SETTINGS_STAGE ((const u8 *)0x30000)
#define MOBILE_NOTES_STAGE    ((const u8 *)0x34000)
#define MOBILE_TERM_STAGE     ((const u8 *)0x38000)
#define MOBILE_EXPLORER_STAGE ((const u8 *)0x90000)
#define MOBILE_STACK_TOP      0x7F000u

static void mobile_copy(char *dst, const char *src, unsigned int cap) {
    unsigned int i = 0;
    if (!dst || !cap) return;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int mobile_ends_apk(const char *s) {
    unsigned int n = 0;
    while (s && s[n]) n++;
    return n >= 4 && s[n-4] == '.' && s[n-3] == 'a' &&
           s[n-2] == 'p' && s[n-1] == 'k';
}

static void mobile_scan_dir(const char *path, struct mobile_apk *out,
                            unsigned int *count) {
    struct vfs_dirent ent;
    *count = 0;
    if (!out) return;
    for (unsigned int i = 0; i < MOBILE_APK_MAX; ++i) {
        if (vfs_readdir(path, i, &ent) != 0) break;
        if (ent.type != VFS_TYPE_FILE || !mobile_ends_apk(ent.name)) continue;
        mobile_copy(out[*count].name, ent.name, MOBILE_NAME_MAX);
        out[*count].size = ent.size;
        if (++(*count) >= MOBILE_APK_MAX) break;
    }
}

static void mobile_refresh_apps(void) {
    if (!installed_apks)
        installed_apks = (struct mobile_apk *)kmalloc(sizeof(*installed_apks) * MOBILE_APK_MAX);
    if (!storage_apks)
        storage_apks = (struct mobile_apk *)kmalloc(sizeof(*storage_apks) * MOBILE_APK_MAX);
    mobile_scan_dir("/apps", installed_apks, &installed_count);
    mobile_scan_dir("/storage", storage_apks, &storage_count);
}

static void mobile_clock(char out[9]) {
    u8 t[6];
    rtc_now(t);
    out[0] = (char)('0' + t[2] / 10); out[1] = (char)('0' + t[2] % 10);
    out[2] = ':'; out[3] = (char)('0' + t[1] / 10); out[4] = (char)('0' + t[1] % 10);
    out[5] = ':'; out[6] = (char)('0' + t[0] / 10); out[7] = (char)('0' + t[0] % 10);
    out[8] = 0;
}

static void mobile_icon(struct sp_context *ui, int x, int y, int kind) {
    struct ag_surface *s = ui->surface;
    uint32_t color = kind == 0 ? 0x007FA8D8 : kind == 1 ? 0x008B80D8 :
                     kind == 2 ? 0x007BBE9A : kind == 3 ? 0x00D5A45C :
                     0x0095A4BD;
    ag_round_rect(ui->device, s, x, y, 54, 54, 16, color);
    if (kind == 0) { /* Files */
        ag_round_rect(ui->device, s, x+10, y+17, 34, 25, 5, 0x00EAF3FF);
        ag_round_rect(ui->device, s, x+12, y+12, 18, 10, 4, 0x00EAF3FF);
    } else if (kind == 1) { /* Terminal */
        ag_round_rect(ui->device, s, x+9, y+11, 36, 32, 6, 0x00202B40);
        ag_fill_rect(ui->device, s, x+17, y+21, 8, 3, 0x00EAF3FF);
        ag_fill_rect(ui->device, s, x+23, y+27, 8, 3, 0x00EAF3FF);
    } else if (kind == 2) { /* Settings */
        ag_round_frame(ui->device, s, x+12, y+12, 30, 30, 15, 0x00FFFFFF);
        ag_round_rect(ui->device, s, x+21, y+21, 12, 12, 6, 0x00FFFFFF);
    } else if (kind == 3) { /* Notes */
        ag_round_rect(ui->device, s, x+13, y+9, 29, 36, 4, 0x00FFF5DC);
        for (int i=0;i<4;i++) ag_fill_rect(ui->device,s,x+19,y+18+i*6,17,2,0x009A8B70);
    } else { /* App drawer */
        for (int yy=0; yy<2; yy++) for (int xx=0; xx<2; xx++)
            ag_round_rect(ui->device,s,x+13+xx*15,y+13+yy*15,10,10,4,0x00FFFFFF);
    }
}

static void mobile_draw_status(struct sp_context *ui) {
    char clock[9];
    mobile_clock(clock);
    sp_label(ui, 26, 16, clock, 0);
    sp_label(ui, 420, 16, "FlairOS Mobile", 1);
    sp_icon_network(ui, 530, 13, 1, 1, 1, 0, -1);
}

static void mobile_draw_home(struct sp_context *ui) {
    static const char *labels[5] = {"Files", "Terminal", "Settings", "Notes", "All apps"};
    static const int kinds[5] = {0,1,2,3,4};
    const int xs[4] = {78, 350, 78, 350};
    const int ys[4] = {170, 170, 330, 330};
    ag_clear(ui->surface, MOBILE_BG);
    ag_round_rect(ui->device, ui->surface, -80, 470, 760, 230, 100, 0x001D2940);
    mobile_draw_status(ui);
    sp_label(ui, 34, 78, "Good day", 1);
    sp_label(ui, 34, 103, "Your FlairOS", 0);
    sp_label(ui, 34, 126, "Everything you need, one tap away.", 1);
    for (int i=0;i<4;i++) {
        sp_card(ui, xs[i]-15, ys[i]-18, 190, 132);
        mobile_icon(ui, xs[i], ys[i], kinds[i]);
        sp_label(ui, xs[i]-3, ys[i]+68, labels[i], 0);
    }
    sp_panel(ui, 28, 560, 544, 104);
    mobile_icon(ui, 56, 582, 0);
    mobile_icon(ui, 188, 582, 1);
    mobile_icon(ui, 320, 582, 2);
    mobile_icon(ui, 452, 582, 4);
    sp_label(ui, 68, 644, "Files", 1);
    sp_label(ui, 190, 644, "Terminal", 1);
    sp_label(ui, 325, 644, "Settings", 1);
    sp_label(ui, 462, 644, "Apps", 1);
    ag_round_rect(ui->device, ui->surface, 245, 716, 110, 6, 3, 0x00D5DCE8);
}

static void mobile_draw_list(struct sp_context *ui, const char *heading,
                             struct mobile_apk *items, unsigned int count,
                             int installed) {
    ag_clear(ui->surface, MOBILE_BG);
    mobile_draw_status(ui);
    sp_label(ui, 28, 70, heading, 0);
    sp_label(ui, 28, 96, installed ? "Tap to launch. Long-press for options." :
             "Select an APK package from /storage.", 1);
    for (unsigned int i=0;i<count && i<8;i++) {
        int y = 130 + (int)i * 62;
        sp_list_item(ui, 24, y, 552, 54, items[i].name,
                     installed ? "Installed application" : "Available package", 0);
    }
    if (installed) sp_button(ui, 24, 650, 260, 48, "Install APK", 1);
    sp_button(ui, 300, 650, 276, 48, "Home", 0);
    ag_round_rect(ui->device, ui->surface, 245, 716, 110, 6, 3, 0x00D5DCE8);
}

static void mobile_load_detail_info(int index) {
    uint8_t *buf;
    char path[96] = "/apps/";
    int fd, n;
    detail_info_valid = 0;
    if (index < 0 || (unsigned int)index >= installed_count) return;
    mobile_copy(path + 6, installed_apks[index].name, sizeof(path) - 6);
    buf = (uint8_t *)kmalloc(65536u);
    if (!buf) return;
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd >= 0) {
        n = vfs_read(fd, buf, 65536u);
        vfs_close(fd);
        if (n > 0 && apk_inspect(buf, (uint32_t)n, &detail_info) == 0)
            detail_info_valid = 1;
    }
    kfree(buf);
}

static void mobile_draw_details(struct sp_context *ui) {
    ag_clear(ui->surface, MOBILE_BG);
    mobile_draw_status(ui);
    sp_label(ui, 28, 72, "Application details", 0);
    if (selected_apk >= 0 && (unsigned int)selected_apk < installed_count) {
        sp_card(ui, 24, 112, 552, 238);
        sp_label(ui, 48, 142, detail_info_valid && detail_info.app_label[0] ? detail_info.app_label : installed_apks[selected_apk].name, 0);
        sp_label(ui, 48, 178, detail_info_valid && detail_info.package_name[0] ? detail_info.package_name : "Package metadata unavailable", 1);
        sp_label(ui, 48, 208, "Android APK  /  installed in /apps", 1);
        if (detail_info_valid && detail_info.is_system_app) sp_badge(ui, 48, 246, "SYSTEM", ui->theme->color.warning);
        else if (detail_info_valid && detail_info.is_user_test_app) sp_badge(ui, 48, 246, "TEST APP", ui->theme->color.success);
        else sp_badge(ui, 48, 246, "USER APP", ui->theme->color.accent);
        sp_button(ui, 24, 380, 168, 52, "Uninstall", 0);
        sp_button(ui, 216, 380, 168, 52, "Open App", 1);
        sp_button(ui, 408, 380, 168, 52, "Back", 0);
    }
    ag_round_rect(ui->device, ui->surface, 245, 716, 110, 6, 3, 0x00D5DCE8);
}

void mobile_present_app(void) {
    struct sp_context *ui = desktop_gfx_ui_context();
    if (!ui || !ui->surface || !ui->device) return;
    /* Mobile apps own the full display. Do not composite desktop window chrome
       over them; AXI draws the app surface and pointer as the final layer. */
    mobile_draw_home(ui);
    axi_gfx_render();
    if (desktop_gfx_flush() != 0) serial_str("mobile: frame flush failed\n");
}

static void mobile_render(void) {
    struct sp_context *ui = desktop_gfx_ui_context();
    if (!ui || !ui->surface) return;
    if (page == MOBILE_HOME) mobile_draw_home(ui);
    else if (page == MOBILE_DRAWER) mobile_draw_list(ui, "Your apps", installed_apks, installed_count, 1);
    else if (page == MOBILE_INSTALL) mobile_draw_list(ui, "Install an app", storage_apks, storage_count, 0);
    else mobile_draw_details(ui);
    if (desktop_gfx_flush() != 0) serial_str("mobile: frame flush failed\n");
}

static void mobile_launch_builtin(int app) {
    const u8 *file = 0;
    u32 len = 0, entry = 0;
    if (app == 0) { file = MOBILE_EXPLORER_STAGE; len = 32u * 512u; }
    else if (app == 1) { file = MOBILE_TERM_STAGE; len = 64u * 512u; }
    else if (app == 2) { file = MOBILE_SETTINGS_STAGE; len = 32u * 512u; }
    else if (app == 3) { file = MOBILE_NOTES_STAGE; len = 16u * 512u; }
    if (!file || airp_load(file, len, &entry) != 0) {
        serial_str("mobile: built-in application failed to load\n");
        return;
    }
    (void)vm_run_app(entry, MOBILE_STACK_TOP);
}

static void mobile_click(int x, int y, int long_press) {
    if (page == MOBILE_HOME) {
        if (y >= 150 && y < 300) {
            if (x >= 45 && x < 260) mobile_launch_builtin(0);
            else if (x >= 320 && x < 555) mobile_launch_builtin(1);
        } else if (y >= 310 && y < 465) {
            if (x >= 45 && x < 260) mobile_launch_builtin(2);
            else if (x >= 320 && x < 555) mobile_launch_builtin(3);
        } else if (y >= 560 && y < 680) {
            if (x < 150) mobile_launch_builtin(0);
            else if (x < 285) mobile_launch_builtin(1);
            else if (x < 420) mobile_launch_builtin(2);
            else { mobile_refresh_apps(); page = MOBILE_DRAWER; }
        }
    } else if (page == MOBILE_DRAWER) {
        if (y >= 130 && y < 626) {
            int idx = (y - 130) / 62;
            if (idx >= 0 && (unsigned int)idx < installed_count) {
                if (long_press) {
                    selected_apk = idx;
                    mobile_load_detail_info(idx);
                    page = MOBILE_DETAILS;
                } else {
                    char path[96] = "/apps/";
                    mobile_copy(path + 6, installed_apks[idx].name, sizeof(path) - 6);
                    (void)axi_apk_launch_path(path);
                    mobile_refresh_apps();
                }
            }
        } else if (y >= 640 && x < 290) page = MOBILE_INSTALL;
        else if (y >= 640) page = MOBILE_HOME;
    } else if (page == MOBILE_INSTALL) {
        if (y >= 130 && y < 626) {
            int idx = (y - 130) / 62;
            if (idx >= 0 && (unsigned int)idx < storage_count) {
                char src[96] = "/storage/";
                mobile_copy(src + 9, storage_apks[idx].name, sizeof(src) - 9);
                char appname[MOBILE_NAME_MAX];
                mobile_copy(appname, storage_apks[idx].name, sizeof(appname));
                if (mobile_ends_apk(appname)) {
                    unsigned int name_len = 0;
                    while (appname[name_len]) name_len++;
                    appname[name_len - 4] = 0;
                }
                if (axi_apk_install_path(src, appname) == 0) mobile_refresh_apps();
            }
        } else if (y >= 640) page = MOBILE_DRAWER;
    } else if (page == MOBILE_DETAILS) {
        if (y >= 370 && y < 445 && selected_apk >= 0 &&
            (unsigned int)selected_apk < installed_count) {
            char path[96] = "/apps/";
            mobile_copy(path + 6, installed_apks[selected_apk].name, sizeof(path) - 6);
            if (x < 200) {
                if (vfs_unlink(path) == 0) serial_str("mobile: app uninstalled\n");
                mobile_refresh_apps();
                page = MOBILE_DRAWER;
                selected_apk = -1;
                detail_info_valid = 0;
            } else if (x < 400) {
                (void)axi_apk_launch_path(path);
                mobile_refresh_apps();
            } else page = MOBILE_DRAWER;
        }
    }
}

int mobile_run(void) {
    int touch_down = 0;
    if (!desktop_gfx_active() && !desktop_gfx_init())
        kernel_panic("Mobile AirGraph initialization failed");
    mobile_refresh_apps();
    page = MOBILE_HOME;
    serial_str("mobile: Android-style portrait shell started\n");
    while (!kbd_toggle_take()) {
        int k;
        while ((k = kbd_read()) >= 0) {
            if (k == 27 || k == 'h' || k == 'H') page = MOBILE_HOME;
            else if (k == 'a' || k == 'A') {
                mobile_refresh_apps();
                page = MOBILE_DRAWER;
                serial_str("mobile: app drawer opened\n");
            } else if (page == MOBILE_DRAWER && k >= '1' && k <= '9' &&
                       (unsigned int)(k - '1') < installed_count) {
                serial_str("mobile: launching installed APK\n");
                char path[96] = "/apps/";
                mobile_copy(path + 6, installed_apks[k - '1'].name, sizeof(path) - 6);
                (void)axi_apk_launch_path(path);
                mobile_refresh_apps();
            }
        }

        /* PS/2 mouse events are also used by QEMU/VNC touch emulation. */
        axi_poll_mouse();
        u8 buttons = mouse_buttons;
        u8 clicks = mouse_take_clicks();
        int left_down = (buttons & 1u) ? 1 : 0;
        if (left_down && !left_was_down) {
            left_press_x = (int)(((uint64_t)mouse_press_x * 600u) >> 16);
            left_press_y = (int)(((uint64_t)mouse_press_y * 750u) >> 16);
            left_press_started = ticks;
        } else if (!left_down && left_was_down) {
            mobile_click(left_press_x, left_press_y,
                         (ticks - left_press_started) >= 35u);
        } else if ((clicks & 1u) && !left_down && !left_was_down) {
            int x = (int)(((uint64_t)mouse_press_x * 600u) >> 16);
            int y = (int)(((uint64_t)mouse_press_y * 750u) >> 16);
            mobile_click(x, y, 0);
        }
        left_was_down = left_down;
        if ((buttons & 2u) && !right_was_down) {
            int x = (int)(((uint64_t)cursor_norm_x * 600u) >> 16);
            int y = (int)(((uint64_t)cursor_norm_y * 750u) >> 16);
            mobile_click(x, y, 1);
        }
        right_was_down = (buttons & 2u) ? 1 : 0;

        /* Optional serial touch bridge (COM2): T x y d\n. */
        (void)touch_poll();
        struct touch_ev ev;
        while (touch_next(&ev)) {
            if (ev.down && !touch_down) {
                touch_press_x = (int)(((uint32_t)ev.x * 600u) / 65535u);
                touch_press_y = (int)(((uint32_t)ev.y * 750u) / 65535u);
                touch_press_started = ticks;
            } else if (!ev.down && touch_down) {
                mobile_click(touch_press_x, touch_press_y,
                             (ticks - touch_press_started) >= 35u);
            }
            touch_down = ev.down ? 1 : 0;
        }
        mobile_render();
        wait_ticks(2);
    }
    if (desktop_gfx_active()) desktop_gfx_shutdown();
    serial_str("mobile: shell suspended for developer console\n");
    return 1;
}
