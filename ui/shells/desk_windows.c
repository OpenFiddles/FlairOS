#include "desk_windows.h"
#include "../../include/airnim.h"
#include "../../include/desktop_chrome.h"

static const char *const DW_PRESET_OPEN = "slide-in";

static an_id dw_open_id(int slot) {
    return an_id_at(an_id_str("desk.window"), slot, 0);
}

void dw_init(struct dw_state *st) {
    if (!st) return;
    wd_init(&st->wd);
    for (int i = 0; i < DW_MAX_WINDOWS; i++) st->wd_ids[i] = 0;
    st->panel_w = 0;
    st->panel_h = 0;
    st->scale_q16 = AG_SCALE_ONE;
    st->active = 0;
    st->drag_id = -1;
    st->drag_ox = 0;
    st->drag_oy = 0;
}

int dw_configure(struct dw_state *st, int panel_w, int panel_h) {
    uint32_t scale;
    if (!st || panel_w <= 0 || panel_h <= 0) return -1;
    if (st->panel_w == panel_w && st->panel_h == panel_h) return 0;
    if (wd_set_display(&st->wd, panel_w, panel_h) != 0) return -1;
    scale = wd_scale_q16(&st->wd);
    st->panel_w = panel_w;
    st->panel_h = panel_h;
    st->scale_q16 = scale;
    st->active = scale > AG_SCALE_ONE;
    return 1;
}

int dw_active(const struct dw_state *st) {
    return st && st->active;
}

struct wd_metrics dw_metrics(struct dw_state *st) {
    return wd_chrome_metrics(&st->wd);
}

void dw_sync(struct dw_state *st, int slot, const struct dw_window *win) {
    int id;
    if (!st || !win || slot < 0 || slot >= DW_MAX_WINDOWS) return;
    id = st->wd_ids[slot];
    if (id <= 0) {
        id = wd_create_window(&st->wd, win->x > 0 ? win->x : 0,
                              win->y > 0 ? win->y : 0,
                              win->w > 0 ? win->w : 1, win->h > 0 ? win->h : 1,
                              win->title);
        if (id <= 0) return;
        st->wd_ids[slot] = id;
    } else {
        wd_set_geometry(&st->wd, id, win->x, win->y,
                        win->w > 0 ? win->w : 1, win->h > 0 ? win->h : 1);
        wd_set_title(&st->wd, id, win->title);
    }
    wd_set_visible(&st->wd, id, win->open && !win->minimized);
}

enum wd_hit_region dw_region(struct dw_state *st, int slot,
                             const struct dw_window *win, int mx, int my) {
    if (!st || !win || slot < 0 || slot >= DW_MAX_WINDOWS) return WD_HIT_NONE;
    dw_sync(st, slot, win);
    if (st->wd_ids[slot] <= 0) return WD_HIT_NONE;
    return wd_hit_chrome(&st->wd, st->wd_ids[slot], mx, my);
}

void dw_draw_chrome(struct dw_state *st, struct ag_surface *surface, int slot,
                    const struct dw_window *win, int active) {
    struct wd_metrics m;
    if (!st || !st->active || !surface || !win) return;
    if (slot < 0 || slot >= DW_MAX_WINDOWS) return;
    m = wd_chrome_metrics(&st->wd);
    (void)desktop_chrome_draw_scaled(0, surface, win->x, win->y, win->w, win->h,
                                     win->title, active, m.title_h, m.inset,
                                     m.button_w, m.button_gap);
}

void dw_open(struct dw_state *st, int slot) {
    if (!st || slot < 0 || slot >= DW_MAX_WINDOWS) return;
    (void)an_preset_play(dw_open_id(slot), DW_PRESET_OPEN);
}

int32_t dw_open_offset(struct dw_state *st, int slot) {
    const struct an_preset *preset;
    int32_t span;
    if (!st || slot < 0 || slot >= DW_MAX_WINDOWS) return 0;
    preset = an_preset_lookup(DW_PRESET_OPEN);
    if (!preset) return 0;
    span = ag_px(24, st->scale_q16 ? st->scale_q16 : AG_SCALE_ONE);
    return an_preset_offset(dw_open_id(slot), preset, span);
}

enum dw_click dw_click(struct dw_state *st, struct dw_window *win, int slot,
                       int mx, int my) {
    enum wd_hit_region region;
    if (!st || !win) return DW_CLICK_NONE;
    region = dw_region(st, slot, win, mx, my);
    switch (region) {
    case WD_HIT_CLOSE:
        win->open = 0;
        if (st->drag_id == slot) dw_end_drag(st);
        return DW_CLICK_CLOSE;
    case WD_HIT_MINIMIZE:
        win->minimized = 1;
        if (st->drag_id == slot) dw_end_drag(st);
        return DW_CLICK_MINIMIZE;
    case WD_HIT_ZOOM:
        if (!win->zoomed) {
            win->sx = win->x; win->sy = win->y;
            win->sw = win->w; win->sh = win->h;
            win->x = ag_px(16, st->scale_q16);
            win->y = ag_px(42, st->scale_q16);
            win->w = st->panel_w - ag_px(32, st->scale_q16);
            win->h = st->panel_h - ag_px(142, st->scale_q16);
            if (win->w < ag_px(200, st->scale_q16))
                win->w = st->panel_w - ag_px(16, st->scale_q16);
            if (win->h < ag_px(160, st->scale_q16))
                win->h = st->panel_h - ag_px(60, st->scale_q16);
            win->zoomed = 1;
        } else {
            win->x = win->sx; win->y = win->sy;
            win->w = win->sw; win->h = win->sh;
            win->zoomed = 0;
        }
        return DW_CLICK_ZOOM;
    case WD_HIT_TITLEBAR:
    case WD_HIT_RESIZE:
        st->drag_id = slot;
        st->drag_ox = mx - win->x;
        st->drag_oy = my - win->y;
        return DW_CLICK_DRAG;
    default:
        return DW_CLICK_CLIENT;
    }
}

int dw_drag_to(struct dw_state *st, struct dw_window *win, int slot,
               int mx, int my) {
    int32_t nx, ny, min_y;
    if (!st || !win || st->drag_id != slot) return 0;
    nx = mx - st->drag_ox;
    ny = my - st->drag_oy;
    min_y = ag_px(34, st->scale_q16 ? st->scale_q16 : AG_SCALE_ONE);
    if (ny < min_y) ny = min_y;
    if (st->panel_h > 0 && ny > st->panel_h - ag_px(48, st->scale_q16))
        ny = st->panel_h - ag_px(48, st->scale_q16);
    if (nx > st->panel_w - ag_px(64, st->scale_q16))
        nx = st->panel_w - ag_px(64, st->scale_q16);
    if (nx < -win->w + ag_px(64, st->scale_q16))
        nx = -win->w + ag_px(64, st->scale_q16);
    if (nx == win->x && ny == win->y) return 0;
    win->x = nx;
    win->y = ny;
    return 1;
}

int dw_end_drag(struct dw_state *st) {
    int was = st && st->drag_id >= 0;
    if (st) st->drag_id = -1;
    return was;
}

struct dw_client dw_client_rect(struct dw_state *st,
                                const struct dw_window *win) {
    struct dw_client c = {0, 0, 0, 0};
    struct wd_metrics m;
    int32_t border;
    if (!st || !win) return c;
    m = wd_chrome_metrics(&st->wd);
    border = (st->active && m.inset / 2 > 0) ? m.inset / 2 : 1;
    c.x = win->x + border;
    c.y = win->y + m.title_h;
    c.w = win->w - border * 2;
    c.h = win->h - m.title_h - border;
    if (c.w < 0) c.w = 0;
    if (c.h < 0) c.h = 0;
    return c;
}

int dw_dispatch_click(struct dw_state *st, struct dw_window *wins,
                      const int *z_order, int count, int mx, int my,
                      int *slot_out, enum dw_click *action_out) {
    if (slot_out) *slot_out = -1;
    if (action_out) *action_out = DW_CLICK_NONE;
    if (!st || !wins || !z_order || count <= 0 || !st->active) return 0;
    for (int i = count - 1; i >= 0; i--) {
        int slot = z_order[i];
        struct dw_window *w;
        enum dw_click acted;
        if (slot < 0 || slot >= DW_MAX_WINDOWS || slot >= count) continue;
        w = &wins[slot];
        if (!w->open || w->minimized) continue;
        if (mx < w->x || my < w->y || mx >= w->x + w->w || my >= w->y + w->h)
            continue;
        acted = dw_click(st, w, slot, mx, my);
        if (slot_out) *slot_out = slot;
        if (action_out) *action_out = acted;
        return acted != DW_CLICK_CLIENT;
    }
    return 0;
}

int dw_rescale(struct dw_state *st, struct dw_window *wins, int count,
               int old_w, int old_h, int new_w, int new_h) {
    uint32_t scale;
    int32_t menu;
    if (!st || !wins || old_w <= 0 || old_h <= 0 || new_w <= 0 || new_h <= 0)
        return -1;
    if (dw_configure(st, new_w, new_h) < 0) return -1;
    scale = st->scale_q16;
    menu = ag_px(34, scale);
    for (int i = 0; i < count; i++) {
        struct dw_window *w = &wins[i];
        if (wd_scale_rect(&w->x, &w->y, &w->w, &w->h,
                          old_w, old_h, new_w, new_h) != 0)
            return -1;
        if (wd_scale_rect(&w->sx, &w->sy, &w->sw, &w->sh,
                          old_w, old_h, new_w, new_h) != 0)
            return -1;
        if (w->w > new_w) w->w = new_w;
        if (w->h > new_h - menu) w->h = new_h - menu;
        if (w->x < 0) w->x = 0;
        if (w->y < menu) w->y = menu;
        if (w->x + w->w > new_w) w->x = new_w - w->w;
        if (w->y + w->h > new_h) w->y = new_h - w->h;
    }
    st->drag_id = -1;
    return 0;
}
