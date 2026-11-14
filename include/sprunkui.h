#ifndef SPRUNKUI_H
#define SPRUNKUI_H

#include "airgraph.h"

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

#endif
