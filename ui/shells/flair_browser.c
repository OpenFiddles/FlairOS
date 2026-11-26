/* SPDX-License-Identifier: AGPL-3.0-only */
/* Native FlairBrowser: HTML/DOM Tokenizer, Layout Engine, History & Host Bridge Fetch */
#include "flair_browser.h"
#include "../../graphics/compositor/glass_fx.h"
#include "../../ui/theme/ios_macos_theme.h"
#include "../../net/bridge/host_bridge.h"
#include "../../include/vfs.h"
#include "../../kernel/pkg/elf64.h"
#include "../../kernel/core/syscall64.h"
#include "../../runtime/art/surface_flinger.h"

#define BR_MAX_NODES 28
#define BR_HIST_MAX  8

enum br_node_kind {
    BR_H1 = 0,
    BR_TEXT = 1,
    BR_LINK = 2
};

struct br_node {
    enum br_node_kind kind;
    char text[64];
    char href[96];
    int32_t ry; /* relative y inside viewport */
};

static struct br_node g_nodes[BR_MAX_NODES];
static int g_node_cnt = 0;
static char g_url[96] = "about:flairos";
static char g_url_edit[96] = "about:flairos";
static int g_url_len = 13;
static int g_url_focused = 0;
static char g_status[32] = "READY";
static char g_hist[BR_HIST_MAX][96];
static int g_hist_cnt = 0;
static int g_hist_pos = -1;
static int g_inited = 0;
static char g_raw_html[4096];

static void br_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

static int br_slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }

static int br_starts(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

static char br_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int br_istarts(const char *s, const char *p) {
    while (*p) { if (br_lower(*s++) != br_lower(*p++)) return 0; }
    return 1;
}

static void br_add_node(enum br_node_kind kind, const char *txt, const char *href) {
    if (g_node_cnt >= BR_MAX_NODES || !txt || !txt[0]) return;
    g_nodes[g_node_cnt].kind = kind;
    br_scpy(g_nodes[g_node_cnt].text, txt, sizeof(g_nodes[g_node_cnt].text));
    br_scpy(g_nodes[g_node_cnt].href, href ? href : "", sizeof(g_nodes[g_node_cnt].href));
    g_nodes[g_node_cnt].ry = 0;
    g_node_cnt++;
}

static void br_parse_html(const char *html) {
    g_node_cnt = 0;
    char cur[64];
    char href[96];
    int clen = 0;
    enum br_node_kind cur_kind = BR_TEXT;
    int skip_tag = 0;
    href[0] = 0;

    for (int i = 0; html[i] && g_node_cnt < BR_MAX_NODES; ) {
        if (html[i] == '<') {
            if (clen > 0) {
                cur[clen] = 0;
                br_add_node(cur_kind, cur, href);
                clen = 0;
            }
            if (br_istarts(html + i, "<script") || br_istarts(html + i, "<style")) skip_tag = 1;
            else if (br_istarts(html + i, "</script") || br_istarts(html + i, "</style")) skip_tag = 0;
            else if (br_istarts(html + i, "<h1") || br_istarts(html + i, "<h2")) cur_kind = BR_H1;
            else if (br_istarts(html + i, "</h1") || br_istarts(html + i, "</h2") || br_istarts(html + i, "</a")) {
                cur_kind = BR_TEXT;
                href[0] = 0;
            } else if (br_istarts(html + i, "<a ")) {
                cur_kind = BR_LINK;
                href[0] = 0;
                const char *hp = html + i;
                while (*hp && *hp != '>') {
                    if (br_istarts(hp, "href=\"")) {
                        hp += 6;
                        int k = 0;
                        while (*hp && *hp != '"' && k + 1 < (int)sizeof(href)) href[k++] = *hp++;
                        href[k] = 0;
                        break;
                    }
                    hp++;
                }
            }
            while (html[i] && html[i] != '>') i++;
            if (html[i] == '>') i++;
            continue;
        }
        if (skip_tag) { i++; continue; }
        char ch = html[i++];
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        if (ch == ' ' && (clen == 0 || cur[clen - 1] == ' ')) continue;
        if (clen + 1 < (int)sizeof(cur)) cur[clen++] = ch;
        else {
            cur[clen] = 0;
            br_add_node(cur_kind, cur, href);
            clen = 0;
        }
    }
    if (clen > 0 && g_node_cnt < BR_MAX_NODES) {
        cur[clen] = 0;
        br_add_node(cur_kind, cur, href);
    }
}

static void br_load_url_internal(const char *url) {
    br_scpy(g_url, url, sizeof(g_url));
    br_scpy(g_url_edit, url, sizeof(g_url_edit));
    g_url_len = br_slen(g_url_edit);

    if (br_starts(url, "about:flairos")) {
        br_scpy(g_status, "LOCAL", sizeof(g_status));
        br_parse_html(
            "<h1>FlairBrowser 1.0</h1>"
            "<p>Native HTML/DOM engine on FlairOS 64-bit.</p>"
            "<a href=\"http://example.com\">Open example.com</a>"
            "<a href=\"file:///home/NOTE.TXT\">Open /home/NOTE.TXT (VFS)</a>"
            "<a href=\"about:system\">Inspect OS Subsystems</a>"
        );
        return;
    }
    if (br_starts(url, "about:system")) {
        br_scpy(g_status, "SYS", sizeof(g_status));
        const struct host_telemetry *tel = host_bridge_telemetry();
        const struct syscall64_stats *sc = syscall64_get_stats();
        g_node_cnt = 0;
        br_add_node(BR_H1, "FlairOS 4-Subsystem Status", 0);
        br_add_node(BR_TEXT, tel->valid ? "Network Bridge: CONNECTED" : "Network Bridge: STANDBY", 0);
        br_add_node(BR_TEXT, "ELF64 + .so Dynamic Linker: READY", 0);
        br_add_node(BR_TEXT, sc->total_calls > 0 ? "Syscall64 Layer: ACTIVE" : "Syscall64 Layer: READY", 0);
        br_add_node(BR_TEXT, sf_active_count() >= 0 ? "SurfaceFlinger ANativeWindow: READY" : "", 0);
        br_add_node(BR_LINK, "Back to FlairBrowser Home", "about:flairos");
        return;
    }
    if (br_starts(url, "file://")) {
        const char *path = url + 7;
        int fd = vfs_open(path, VFS_O_RDONLY);
        if (fd >= 0) {
            int n = vfs_read(fd, g_raw_html, sizeof(g_raw_html) - 1);
            vfs_close(fd);
            if (n > 0) {
                g_raw_html[n] = 0;
                br_scpy(g_status, "VFS OK", sizeof(g_status));
                g_node_cnt = 0;
                br_add_node(BR_H1, path, 0);
                br_add_node(BR_TEXT, g_raw_html, 0);
                return;
            }
        }
        br_scpy(g_status, "404 VFS", sizeof(g_status));
        g_node_cnt = 0;
        br_add_node(BR_H1, "File Not Found", 0);
        br_add_node(BR_TEXT, path, 0);
        return;
    }

    int http_st = 0;
    int n = host_bridge_http_get(url, g_raw_html, sizeof(g_raw_html), &http_st);
    if (n > 0) {
        br_scpy(g_status, http_st == 200 ? "200 OK" : "HTTP", sizeof(g_status));
        br_parse_html(g_raw_html);
    } else {
        br_scpy(g_status, "OFFLINE", sizeof(g_status));
        g_node_cnt = 0;
        br_add_node(BR_H1, "Bridge Offline", 0);
        br_add_node(BR_TEXT, "Network unavailable. Check connection.", 0);
        br_add_node(BR_LINK, "Return to Start Page", "about:flairos");
    }
}

void flair_browser_init(void) {
    if (g_inited) return;
    g_inited = 1;
    flair_browser_navigate("about:flairos");
}

void flair_browser_navigate(const char *url) {
    if (!url || !url[0]) return;
    if (!g_inited) g_inited = 1;
    if (g_hist_pos + 1 < BR_HIST_MAX) {
        g_hist_pos++;
        br_scpy(g_hist[g_hist_pos], url, 96);
        g_hist_cnt = g_hist_pos + 1;
    }
    br_load_url_internal(url);
}

int flair_browser_handle_click(int32_t rx, int32_t ry, int32_t w, int32_t h) {
    (void)h;
    flair_browser_init();
    /* Toolbar: Back(12,44,30,24), Fwd(46,44,30,24), Home(80,44,44,24), URL(130,44,w-204,24), Go(w-68,44,54,24) */
    if (ry >= 44 && ry < 68) {
        if (rx >= 12 && rx < 42) {
            if (g_hist_pos > 0) br_load_url_internal(g_hist[--g_hist_pos]);
            return 1;
        }
        if (rx >= 46 && rx < 76) {
            if (g_hist_pos + 1 < g_hist_cnt) br_load_url_internal(g_hist[++g_hist_pos]);
            return 1;
        }
        if (rx >= 80 && rx < 124) {
            flair_browser_navigate("about:flairos");
            return 1;
        }
        if (rx >= 130 && rx < w - 74) {
            g_url_focused = 1;
            return 1;
        }
        if (rx >= w - 68 && rx < w - 14) {
            g_url_focused = 0;
            flair_browser_navigate(g_url_edit);
            return 1;
        }
    }
    g_url_focused = 0;

    /* Bookmarks row: y=74..94 */
    if (ry >= 74 && ry < 94) {
        if (rx >= 12 && rx < 82)  { flair_browser_navigate("about:flairos"); return 1; }
        if (rx >= 88 && rx < 176) { flair_browser_navigate("about:system"); return 1; }
        if (rx >= 182 && rx < 280){ flair_browser_navigate("file:///home/NOTE.TXT"); return 1; }
        if (rx >= 286 && rx < 396){ flair_browser_navigate("http://example.com"); return 1; }
    }

    /* Clickable DOM links */
    for (int i = 0; i < g_node_cnt; i++) {
        if (g_nodes[i].kind == BR_LINK && g_nodes[i].href[0]) {
            if (ry >= g_nodes[i].ry && ry < g_nodes[i].ry + 22) {
                flair_browser_navigate(g_nodes[i].href);
                return 1;
            }
        }
    }
    return 1;
}

int flair_browser_handle_key(uint16_t key) {
    flair_browser_init();
    if (!g_url_focused && key != '\n') return 0;
    if (key == '\n') {
        g_url_edit[g_url_len] = 0;
        g_url_focused = 0;
        flair_browser_navigate(g_url_edit);
        return 1;
    }
    if (key == '\b' && g_url_len > 0) {
        g_url_edit[--g_url_len] = 0;
        return 1;
    }
    if (key >= 32 && key < 127 && g_url_len + 1 < (int)sizeof(g_url_edit)) {
        g_url_edit[g_url_len++] = (char)key;
        g_url_edit[g_url_len] = 0;
        return 1;
    }
    return 0;
}

void flair_browser_render(struct ag_surface *s, int32_t wx, int32_t wy, int32_t ww, int32_t wh) {
    flair_browser_init();
    /* Toolbar */
    gfx_fill_round_rect(s, wx + 12, wy + 44, 30, 24, 6, THEME_GLASS_LIGHT, 210u);
    gfx_draw_text_scaled(s, wx + 22, wy + 49, "<", THEME_TEXT_PRIMARY, 2);
    gfx_fill_round_rect(s, wx + 46, wy + 44, 30, 24, 6, THEME_GLASS_LIGHT, 210u);
    gfx_draw_text_scaled(s, wx + 56, wy + 49, ">", THEME_TEXT_PRIMARY, 2);
    gfx_fill_round_rect(s, wx + 80, wy + 44, 44, 24, 6, THEME_GLASS_LIGHT, 210u);
    gfx_draw_text_scaled(s, wx + 86, wy + 51, "Home", THEME_TEXT_PRIMARY, 1);

    gfx_fill_round_rect(s, wx + 130, wy + 44, ww - 204, 24, 6,
                        g_url_focused ? THEME_ACCENT_INDIGO : THEME_GLASS_LIGHT, 180u);
    gfx_draw_text_scaled(s, wx + 138, wy + 51, g_url_edit, THEME_TEXT_PRIMARY, 1);

    gfx_fill_round_rect(s, wx + ww - 68, wy + 44, 54, 24, 6, THEME_ACCENT_BLUE, 235u);
    gfx_draw_text_scaled(s, wx + ww - 54, wy + 49, "Go", THEME_TEXT_PRIMARY, 2);

    /* Bookmarks bar */
    gfx_fill_round_rect(s, wx + 12,  wy + 74, 70, 20, 5, THEME_GLASS_LIGHT, 160u);
    gfx_draw_text_scaled(s, wx + 20,  wy + 79, "Start", THEME_TEXT_PRIMARY, 1);
    gfx_fill_round_rect(s, wx + 88,  wy + 74, 88, 20, 5, THEME_GLASS_LIGHT, 160u);
    gfx_draw_text_scaled(s, wx + 96,  wy + 79, "Subsystems", THEME_ACCENT_TEAL, 1);
    gfx_fill_round_rect(s, wx + 182, wy + 74, 98, 20, 5, THEME_GLASS_LIGHT, 160u);
    gfx_draw_text_scaled(s, wx + 190, wy + 79, "VFS Note", THEME_TEXT_PRIMARY, 1);
    if (ww >= 490) {
        gfx_fill_round_rect(s, wx + 286, wy + 74, 110, 20, 5, THEME_GLASS_LIGHT, 160u);
        gfx_draw_text_scaled(s, wx + 294, wy + 79, "example.com", THEME_ACCENT_BLUE, 1);
    }
    gfx_draw_text_scaled(s, wx + ww - 72, wy + 79, g_status, THEME_ACCENT_GREEN, 1);

    /* Page Viewport */
    gfx_fill_round_rect(s, wx + 12, wy + 100, ww - 24, wh - 114, 10, THEME_GLASS_LIGHT, 115u);
    int32_t ry = 112;
    for (int i = 0; i < g_node_cnt && ry + 24 < wh - 18; i++) {
        g_nodes[i].ry = ry;
        if (g_nodes[i].kind == BR_H1) {
            gfx_draw_text_scaled(s, wx + 24, wy + ry, g_nodes[i].text, THEME_TEXT_PRIMARY, 2);
            ry += 28;
        } else if (g_nodes[i].kind == BR_LINK) {
            gfx_fill_round_rect(s, wx + 22, wy + ry - 2, ww - 48, 20, 5, THEME_ACCENT_BLUE, 140u);
            gfx_draw_text_scaled(s, wx + 28, wy + ry + 3, g_nodes[i].text, THEME_TEXT_PRIMARY, 1);
            ry += 24;
        } else {
            gfx_draw_text_scaled(s, wx + 24, wy + ry, g_nodes[i].text, THEME_TEXT_MUTED, 1);
            ry += 18;
        }
    }
}

const char *flair_browser_current_url(void) { return g_url; }
int flair_browser_node_count(void) { return g_node_cnt; }
