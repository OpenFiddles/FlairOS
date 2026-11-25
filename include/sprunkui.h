#ifndef SPRUNKUI_H
#define SPRUNKUI_H

#include "airgraph.h"
#include "airnim.h"

/* SprunkUI: compact, rounded, theme-driven widgets over AirGraph surfaces.
   Text rendering is supplied by the platform; no font implementation is coupled in. */
struct sp_color {
    uint32_t background, surface, elevated, border;
    uint32_t text, muted, accent, accent_text;
    uint32_t success, warning, danger;
};
struct sp_theme { struct sp_color color; int32_t radius, spacing, border_width; };
typedef void (*sp_text_fn)(struct ag_surface *, int32_t, int32_t, const char *, uint32_t);
struct sp_context {
    struct ag_device *device;
    struct ag_surface *surface;
    const struct sp_theme *theme;
    sp_text_fn draw_text;
    /* Optional interaction state for animated widgets (all zero = no pointer).
       The platform fills these in each frame before drawing. */
    int32_t pointer_x, pointer_y;
    int pointer_down;
    int32_t text_w;   /* pixels per character of draw_text; 0 means 6 */
};

void sp_theme_default(struct sp_theme *);
void sp_panel(struct sp_context *, int32_t, int32_t, int32_t, int32_t);
void sp_card(struct sp_context *, int32_t, int32_t, int32_t, int32_t);
void sp_label(struct sp_context *, int32_t, int32_t, const char *, int);
void sp_button(struct sp_context *, int32_t, int32_t, int32_t, int32_t, const char *, int);
void sp_badge(struct sp_context *, int32_t, int32_t, const char *, uint32_t);
void sp_separator(struct sp_context *, int32_t, int32_t, int32_t);
void sp_progress(struct sp_context *, int32_t, int32_t, int32_t, int32_t, unsigned int);
void sp_toggle(struct sp_context *, int32_t, int32_t, int32_t, int32_t, int);
void sp_checkbox(struct sp_context *, int32_t, int32_t, int32_t, int);
void sp_text_field(struct sp_context *, int32_t, int32_t, int32_t, int32_t, const char *, int);
void sp_list_item(struct sp_context *, int32_t, int32_t, int32_t, int32_t,
                  const char *, const char *, int);
void sp_slider(struct sp_context *, int32_t, int32_t, int32_t, int32_t, unsigned int);

/* ---- Animated, pointer-aware widgets (sprunkui_anim.c) -------------------
   Each takes an an_id so Airnimation can remember its hover/press/slide state
   between frames. Use an_id_str("name") or an_id_at(base, x, y). With
   animations disabled they draw their final state immediately. */
int sp_hit(const struct sp_context *, int32_t x, int32_t y, int32_t w, int32_t h);
void sp_button_a(struct sp_context *, an_id, int32_t x, int32_t y, int32_t w, int32_t h,
                 const char *label, int primary);
void sp_toggle_a(struct sp_context *, an_id, int32_t x, int32_t y, int32_t w, int32_t h, int on);
void sp_list_item_a(struct sp_context *, an_id, int32_t x, int32_t y, int32_t w, int32_t h,
                    const char *title, const char *subtitle, int selected);
void sp_progress_a(struct sp_context *, an_id, int32_t x, int32_t y, int32_t w, int32_t h,
                   unsigned int value);
void sp_tabs_a(struct sp_context *, an_id, int32_t x, int32_t y, int32_t w, int32_t h,
               const char *const *labels, unsigned int count, unsigned int selected);
int sp_tabs_hit(const struct sp_context *, int32_t x, int32_t y, int32_t w, int32_t h,
                unsigned int count);   /* tab under the pointer, or -1 */
/* Status icons: u is the pixel unit (1 at 800x600). Return the width drawn. */
int32_t sp_icon_battery(struct sp_context *, int32_t x, int32_t y, int32_t u,
                        int percent /* -1 unknown */, int charging);
int32_t sp_icon_network(struct sp_context *, int32_t x, int32_t y, int32_t u,
                        int present, int up, int wireless, int signal /* 0-100, -1 unknown */);

void sp_bar_a(struct sp_context *ui, an_id id, int32_t x, int32_t y, int32_t w, int32_t h,
              unsigned int permille, uint32_t fill_color);

#endif
