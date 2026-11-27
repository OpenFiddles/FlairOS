#include "../../include/windown.h"
#include "../../include/desktop_chrome.h"

static void wd_copy_title(char *dst, const char *src) {
    int i = 0;
    if (src) {
        while (src[i] && i < WINDOWN_TITLE_MAX - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static int wd_find_slot(const struct wd_compositor *c, int id) {
    if (!c || id <= 0) return -1;
    for (int i = 0; i < WINDOWN_MAX_WINDOWS; i++) {
        if ((c->windows[i].flags & WD_FLAG_USED) && c->windows[i].id == id)
            return i;
    }
    return -1;
}

static int wd_top_visible_id(const struct wd_compositor *c) {
    for (int i = c->z_count - 1; i >= 0; i--) {
        int slot = c->z_order[i];
        const struct wd_window *w = &c->windows[slot];
        if ((w->flags & WD_FLAG_VISIBLE) && !(w->flags & WD_FLAG_MINIMIZED))
            return w->id;
    }
    return -1;
}

void wd_init(struct wd_compositor *c) {
    if (!c) return;
    for (int i = 0; i < WINDOWN_MAX_WINDOWS; i++) {
        c->windows[i].id = 0;
        c->windows[i].x = 0;
        c->windows[i].y = 0;
        c->windows[i].w = 0;
        c->windows[i].h = 0;
        c->windows[i].flags = 0;
        c->windows[i].title[0] = 0;
        c->windows[i].client_surface = 0;
        c->windows[i].evt_head = 0;
        c->windows[i].evt_tail = 0;
        c->windows[i].zoomed = 0;
        c->z_order[i] = 0;
    }
    c->z_count = 0;
    c->active_id = -1;
    c->next_id = 1;
    c->dirty.x = 0;
    c->dirty.y = 0;
    c->dirty.w = 0;
    c->dirty.h = 0;
    c->dirty.valid = 0;
    c->scale_q16 = AG_SCALE_ONE;
    c->configured = 0;
    c->panel_w = 0;
    c->panel_h = 0;
}

void wd_invalidate(struct wd_compositor *c, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!c || w <= 0 || h <= 0) return;
    if (!c->dirty.valid) {
        c->dirty.x = x;
        c->dirty.y = y;
        c->dirty.w = w;
        c->dirty.h = h;
        c->dirty.valid = 1;
        return;
    }
    int32_t x0 = x < c->dirty.x ? x : c->dirty.x;
    int32_t y0 = y < c->dirty.y ? y : c->dirty.y;
    int32_t r1 = x + w;
    int32_t r2 = c->dirty.x + c->dirty.w;
    int32_t b1 = y + h;
    int32_t b2 = c->dirty.y + c->dirty.h;
    c->dirty.x = x0;
    c->dirty.y = y0;
    c->dirty.w = (r1 > r2 ? r1 : r2) - x0;
    c->dirty.h = (b1 > b2 ? b1 : b2) - y0;
}

void wd_invalidate_all(struct wd_compositor *c) {
    if (!c) return;
    c->dirty.x = 0;
    c->dirty.y = 0;
    c->dirty.w = 0x7FFF;
    c->dirty.h = 0x7FFF;
    c->dirty.valid = 1;
}

int wd_create_window(struct wd_compositor *c, int x, int y, int w, int h, const char *title) {
    if (!c || w <= 0 || h <= 0 || c->z_count >= WINDOWN_MAX_WINDOWS)
        return -1;
    for (int i = 0; i < WINDOWN_MAX_WINDOWS; i++) {
        if (!(c->windows[i].flags & WD_FLAG_USED)) {
            int id = c->next_id++;
            c->windows[i].id = id;
            c->windows[i].x = x;
            c->windows[i].y = y;
            c->windows[i].w = w;
            c->windows[i].h = h;
            c->windows[i].flags = WD_FLAG_USED | WD_FLAG_VISIBLE;
            wd_copy_title(c->windows[i].title, title);
            c->windows[i].client_surface = 0;
            c->windows[i].evt_head = 0;
            c->windows[i].evt_tail = 0;
            c->windows[i].zoomed = 0;
            c->z_order[c->z_count++] = i;
            c->active_id = id;
            wd_invalidate_all(c);
            return id;
        }
    }
    return -1;
}

int wd_destroy_window(struct wd_compositor *c, int id) {
    int slot = wd_find_slot(c, id);
    if (slot < 0) return -1;
    c->windows[slot].flags = 0;
    c->windows[slot].id = 0;
    c->windows[slot].client_surface = 0;
    for (int i = 0; i < c->z_count; i++) {
        if (c->z_order[i] == slot) {
            for (int j = i; j < c->z_count - 1; j++)
                c->z_order[j] = c->z_order[j + 1];
            c->z_count--;
            break;
        }
    }
    if (c->active_id == id)
        c->active_id = wd_top_visible_id(c);
    wd_invalidate_all(c);
    return 0;
}

int wd_set_geometry(struct wd_compositor *c, int id, int x, int y, int w, int h) {
    int slot = wd_find_slot(c, id);
    if (slot < 0 || w <= 0 || h <= 0) return -1;
    c->windows[slot].x = x;
    c->windows[slot].y = y;
    c->windows[slot].w = w;
    c->windows[slot].h = h;
    wd_invalidate_all(c);
    return 0;
}

int wd_set_visible(struct wd_compositor *c, int id, int visible) {
    int slot = wd_find_slot(c, id);
    if (slot < 0) return -1;
    if (visible)
        c->windows[slot].flags |= WD_FLAG_VISIBLE;
    else
        c->windows[slot].flags &= ~WD_FLAG_VISIBLE;
    if (!visible && c->active_id == id)
        c->active_id = wd_top_visible_id(c);
    else if (visible && !(c->windows[slot].flags & WD_FLAG_MINIMIZED))
        c->active_id = id;
    wd_invalidate_all(c);
    return 0;
}

int wd_set_minimized(struct wd_compositor *c, int id, int minimized) {
    int slot = wd_find_slot(c, id);
    if (slot < 0) return -1;
    if (minimized)
        c->windows[slot].flags |= WD_FLAG_MINIMIZED;
    else
        c->windows[slot].flags &= ~WD_FLAG_MINIMIZED;
    if (minimized && c->active_id == id)
        c->active_id = wd_top_visible_id(c);
    wd_invalidate_all(c);
    return 0;
}

int wd_set_title(struct wd_compositor *c, int id, const char *title) {
    int slot = wd_find_slot(c, id);
    if (slot < 0) return -1;
    wd_copy_title(c->windows[slot].title, title);
    wd_invalidate_all(c);
    return 0;
}

int wd_set_surface(struct wd_compositor *c, int id, struct ag_surface *surface) {
    int slot = wd_find_slot(c, id);
    if (slot < 0) return -1;
    c->windows[slot].client_surface = surface;
    wd_invalidate_all(c);
    return 0;
}

int wd_raise(struct wd_compositor *c, int id) {
    int slot = wd_find_slot(c, id);
    if (slot < 0) return -1;
    for (int i = 0; i < c->z_count; i++) {
        if (c->z_order[i] == slot) {
            for (int j = i; j < c->z_count - 1; j++)
                c->z_order[j] = c->z_order[j + 1];
            c->z_order[c->z_count - 1] = slot;
            break;
        }
    }
    c->windows[slot].flags |= WD_FLAG_VISIBLE;
    c->windows[slot].flags &= ~WD_FLAG_MINIMIZED;
    c->active_id = id;
    wd_invalidate_all(c);
    return 0;
}

int wd_hit_test(const struct wd_compositor *c, int gx, int gy) {
    if (!c) return -1;
    for (int i = c->z_count - 1; i >= 0; i--) {
        const struct wd_window *w = &c->windows[c->z_order[i]];
        if (!(w->flags & WD_FLAG_VISIBLE) || (w->flags & WD_FLAG_MINIMIZED))
            continue;
        if (gx >= w->x && gx < w->x + w->w && gy >= w->y && gy < w->y + w->h)
            return w->id;
    }
    return -1;
}

int wd_set_display(struct wd_compositor *c, int panel_w, int panel_h) {
    uint32_t scale;
    if (!c || panel_w <= 0 || panel_h <= 0) return -1;
    if (ag_scale_q16_for(panel_w, panel_h, 1280, 720, &scale) != 0) return -1;
    c->scale_q16 = scale;
    c->configured = 1;
    c->panel_w = panel_w;
    c->panel_h = panel_h;
    wd_invalidate_all(c);
    return 0;
}

int wd_set_scale_q16(struct wd_compositor *c, uint32_t scale_q16) {
    if (!c || scale_q16 == 0u) return -1;
    c->scale_q16 = scale_q16;
    c->configured = 1;
    wd_invalidate_all(c);
    return 0;
}

uint32_t wd_scale_q16(const struct wd_compositor *c) {
    return (c && c->scale_q16) ? c->scale_q16 : AG_SCALE_ONE;
}

struct wd_metrics wd_chrome_metrics(const struct wd_compositor *c) {
    struct wd_metrics m;
    uint32_t scale = wd_scale_q16(c);
    m.scale_q16 = scale;
    m.title_h = 1; m.inset = 1; m.button_w = 2; m.button_h = 1;
    m.button_gap = 0; m.grip = 1;
    if (!c || !c->configured) return m;
    m.title_h = ag_px(12, scale);
    m.inset = ag_px(8, scale);
    m.button_w = ag_px(12, scale);
    m.button_h = ag_px(8, scale);
    m.button_gap = ag_px(6, scale);
    m.grip = ag_px(10, scale);
    return m;
}

static int wd_scale_coord(int v, int from, int to) {
    int64_t out;
    if (from <= 0 || to <= 0 || from == to) return v;
    out = ((int64_t)v * (int64_t)to) / (int64_t)from;
    if (out > 0x7FFFFFFF) out = 0x7FFFFFFF;
    if (out < -0x7FFFFFFF) out = -0x7FFFFFFF;
    return (int32_t)out;
}

int wd_rescale_display(struct wd_compositor *c, int old_w, int old_h,
                       int new_w, int new_h) {
    if (!c || old_w <= 0 || old_h <= 0 || new_w <= 0 || new_h <= 0)
        return -1;
    for (int i = 0; i < WINDOWN_MAX_WINDOWS; i++) {
        struct wd_window *w = &c->windows[i];
        if (!(w->flags & WD_FLAG_USED)) continue;
        w->x = wd_scale_coord(w->x, old_w, new_w);
        w->y = wd_scale_coord(w->y, old_h, new_h);
        w->w = wd_scale_coord(w->w, old_w, new_w);
        w->h = wd_scale_coord(w->h, old_h, new_h);
        w->saved_x = wd_scale_coord(w->saved_x, old_w, new_w);
        w->saved_y = wd_scale_coord(w->saved_y, old_h, new_h);
        w->saved_w = wd_scale_coord(w->saved_w, old_w, new_w);
        w->saved_h = wd_scale_coord(w->saved_h, old_h, new_h);
        if (w->w < 1) w->w = 1;
        if (w->h < 1) w->h = 1;
    }
    return wd_set_display(c, new_w, new_h);
}

enum wd_hit_region wd_hit_chrome(const struct wd_compositor *c, int id, int gx, int gy) {
    int slot = wd_find_slot(c, id);
    if (slot < 0) return WD_HIT_NONE;
    const struct wd_window *w = &c->windows[slot];
    struct wd_metrics m;
    if (!(w->flags & WD_FLAG_VISIBLE) || (w->flags & WD_FLAG_MINIMIZED))
        return WD_HIT_NONE;
    if (gx < w->x || gx >= w->x + w->w || gy < w->y || gy >= w->y + w->h)
        return WD_HIT_NONE;
    if (!c->configured) {
        if (gy == w->y) {
            if (gx >= w->x + 1 && gx <= w->x + 2) return WD_HIT_CLOSE;
            if (gx >= w->x + 3 && gx <= w->x + 4) return WD_HIT_MINIMIZE;
            if (gx >= w->x + 5 && gx <= w->x + 6) return WD_HIT_ZOOM;
            return WD_HIT_TITLEBAR;
        }
        if (gx == w->x + w->w - 1 && gy == w->y + w->h - 1)
            return WD_HIT_RESIZE;
        return WD_HIT_CLIENT;
    }
    m = wd_chrome_metrics(c);
    if (gy < w->y + m.title_h) {
        int32_t bx = w->x + m.inset;
        if (gx >= bx && gx < bx + m.button_w) return WD_HIT_CLOSE;
        bx += m.button_w + m.button_gap;
        if (gx >= bx && gx < bx + m.button_w) return WD_HIT_MINIMIZE;
        bx += m.button_w + m.button_gap;
        if (gx >= bx && gx < bx + m.button_w) return WD_HIT_ZOOM;
        return WD_HIT_TITLEBAR;
    }
    if (gx >= w->x + w->w - m.grip && gy >= w->y + w->h - m.grip)
        return WD_HIT_RESIZE;
    return WD_HIT_CLIENT;
}

int wd_post_event(struct wd_compositor *c, int id, const struct wd_event *ev) {
    int slot = wd_find_slot(c, id);
    if (slot < 0 || !ev) return -1;
    struct wd_window *w = &c->windows[slot];
    uint8_t next = (uint8_t)((w->evt_head + 1u) % WD_EVENT_QUEUE_LEN);
    if (next == w->evt_tail) return -1;
    w->events[w->evt_head] = *ev;
    w->evt_head = next;
    return 0;
}

int wd_poll_event(struct wd_compositor *c, int id, struct wd_event *out) {
    int slot = wd_find_slot(c, id);
    if (slot < 0 || !out) return -1;
    struct wd_window *w = &c->windows[slot];
    if (w->evt_head == w->evt_tail) return 0;
    *out = w->events[w->evt_tail];
    w->evt_tail = (uint8_t)((w->evt_tail + 1u) % WD_EVENT_QUEUE_LEN);
    return 1;
}

int wd_zoom_toggle(struct wd_compositor *c, int id, int max_w, int max_h) {
    int slot = wd_find_slot(c, id);
    if (slot < 0 || max_w <= 0 || max_h <= 0) return -1;
    struct wd_window *w = &c->windows[slot];
    if (!w->zoomed) {
        w->saved_x = w->x; w->saved_y = w->y;
        w->saved_w = w->w; w->saved_h = w->h;
        w->x = 0; w->y = 2; w->w = max_w; w->h = max_h - 2;
        w->zoomed = 1;
    } else {
        w->x = w->saved_x; w->y = w->saved_y;
        w->w = w->saved_w; w->h = w->saved_h;
        w->zoomed = 0;
    }
    wd_invalidate_all(c);
    return 0;
}

int wd_composite(struct wd_compositor *c, struct ag_device *device,
                 struct ag_surface *target, int grid_cols, int grid_rows) {
    struct wd_metrics m;
    int32_t border;
    if (!c || !target || !target->pixels || grid_cols <= 0 || grid_rows <= 0)
        return -1;
    m = wd_chrome_metrics(c);
    border = (c->configured && m.inset / 2 > 0) ? m.inset / 2 : 1;
    for (int i = 0; i < c->z_count; i++) {
        const struct wd_window *w = &c->windows[c->z_order[i]];
        int32_t px, py, right, bottom, title_h;
        if (!(w->flags & WD_FLAG_VISIBLE) || (w->flags & WD_FLAG_MINIMIZED))
            continue;
        /* A configured compositor arranges windows in device pixels, so the
           chrome is drawn 1:1 and its metrics line up with hit testing. An
           unconfigured one keeps the legacy logical grid. */
        if (c->configured) {
            px = w->x;
            py = w->y;
            right = w->x + w->w;
            bottom = w->y + w->h;
            title_h = m.title_h;
        } else {
            px = (w->x * target->width) / grid_cols;
            py = (w->y * target->height) / grid_rows;
            right = ((w->x + w->w) * target->width) / grid_cols;
            bottom = ((w->y + w->h) * target->height) / grid_rows;
            title_h = ((w->y + 1) * target->height) / grid_rows - py;
            if (m.title_h > title_h) title_h = m.title_h;
        }
        if (c->configured) {
            if (desktop_chrome_draw_scaled(device, target, px, py,
                                           right - px, bottom - py, w->title,
                                           w->id == c->active_id, m.title_h,
                                           m.inset, m.button_w,
                                           m.button_gap) != 0)
                return -1;
        } else if (desktop_chrome_draw(device, target, grid_cols, grid_rows,
                                       w->x, w->y, w->w, w->h, w->title,
                                       w->id == c->active_id) != 0) {
            return -1;
        }
        if (w->client_surface && w->client_surface->pixels) {
            int32_t cx = px + border;
            int32_t cy = py + title_h;
            int32_t cw = right - px - border * 2;
            int32_t ch = bottom - py - title_h - border;
            if (cw > 0 && ch > 0) {
                int32_t bw = w->client_surface->width < cw ? w->client_surface->width : cw;
                int32_t bh = w->client_surface->height < ch ? w->client_surface->height : ch;
                if (bw > 0 && bh > 0)
                    ag_blit(device, target, cx, cy, bw, bh,
                            w->client_surface->pixels, w->client_surface->pitch);
            }
        }
    }
    c->dirty.valid = 0;
    return 0;
}
