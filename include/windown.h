#ifndef FLAIROS_WINDOWN_H
#define FLAIROS_WINDOWN_H

#include <stdint.h>
#include "airgraph.h"

#define WINDOWN_MAX_WINDOWS 16
#define WINDOWN_TITLE_MAX   48

#define WD_FLAG_USED      (1u << 0)
#define WD_FLAG_VISIBLE   (1u << 1)
#define WD_FLAG_MINIMIZED (1u << 2)

enum wd_hit_region {
    WD_HIT_NONE = 0,
    WD_HIT_CLOSE = 1,
    WD_HIT_MINIMIZE = 2,
    WD_HIT_ZOOM = 3,
    WD_HIT_TITLEBAR = 4,
    WD_HIT_CLIENT = 5,
    WD_HIT_RESIZE = 6
};


#define WD_EVENT_QUEUE_LEN 16

enum wd_event_type {
    WD_EVT_NONE = 0,
    WD_EVT_MOUSE_DOWN = 1,
    WD_EVT_MOUSE_UP = 2,
    WD_EVT_KEY = 3,
    WD_EVT_CLOSE = 4,
    WD_EVT_RESIZE = 5
};

struct wd_event {
    uint8_t type;
    uint8_t button;
    int16_t x;
    int16_t y;
    uint32_t key;
};

struct wd_rect {
    int32_t x, y, w, h;
    int valid;
};

struct wd_window {
    int id;
    int x, y, w, h;
    uint32_t flags;
    char title[WINDOWN_TITLE_MAX];
    struct ag_surface *client_surface;
    struct wd_event events[WD_EVENT_QUEUE_LEN];
    uint8_t evt_head;
    uint8_t evt_tail;
    int saved_x, saved_y, saved_w, saved_h;
    int zoomed;
};

struct wd_compositor {
    struct wd_window windows[WINDOWN_MAX_WINDOWS];
    int z_order[WINDOWN_MAX_WINDOWS]; /* back-to-front indices into windows[] */
    int z_count;
    int active_id;
    int next_id;
    struct wd_rect dirty;
};

void wd_init(struct wd_compositor *c);
int wd_create_window(struct wd_compositor *c, int x, int y, int w, int h, const char *title);
int wd_destroy_window(struct wd_compositor *c, int id);
int wd_set_geometry(struct wd_compositor *c, int id, int x, int y, int w, int h);
int wd_set_visible(struct wd_compositor *c, int id, int visible);
int wd_set_minimized(struct wd_compositor *c, int id, int minimized);
int wd_set_title(struct wd_compositor *c, int id, const char *title);
int wd_set_surface(struct wd_compositor *c, int id, struct ag_surface *surface);
int wd_raise(struct wd_compositor *c, int id);
int wd_hit_test(const struct wd_compositor *c, int gx, int gy);
enum wd_hit_region wd_hit_chrome(const struct wd_compositor *c, int id, int gx, int gy);
void wd_invalidate(struct wd_compositor *c, int32_t x, int32_t y, int32_t w, int32_t h);
void wd_invalidate_all(struct wd_compositor *c);
int wd_post_event(struct wd_compositor *c, int id, const struct wd_event *ev);
int wd_poll_event(struct wd_compositor *c, int id, struct wd_event *out);
int wd_zoom_toggle(struct wd_compositor *c, int id, int max_w, int max_h);
int wd_composite(struct wd_compositor *c, struct ag_device *device,
                 struct ag_surface *target, int grid_cols, int grid_rows);

#endif
