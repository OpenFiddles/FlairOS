#ifndef FLAIROS_GLASS_FX_H
#define FLAIROS_GLASS_FX_H

#include <stdint.h>
#include "../../include/airgraph.h"

void gfx_blend_pixel(struct ag_surface *s, int32_t x, int32_t y, uint32_t rgb, uint8_t alpha);
void gfx_fill_rect_alpha(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t rgb, uint8_t alpha);
void gfx_gradient_v(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                    uint32_t top_rgb, uint32_t bot_rgb);
void gfx_fill_round_rect(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                         int32_t r, uint32_t rgb, uint8_t alpha);
void gfx_stroke_round_rect(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t r, uint32_t rgb, uint8_t alpha);
void gfx_glass_panel(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                     int32_t r, uint32_t tint_rgb, uint8_t alpha);
void gfx_traffic_lights(struct ag_surface *s, int32_t x, int32_t y);
void gfx_draw_char_scaled(struct ag_surface *s, int32_t x, int32_t y, char ch,
                          uint32_t rgb, int32_t scale);
void gfx_draw_text_scaled(struct ag_surface *s, int32_t x, int32_t y, const char *text,
                          uint32_t rgb, int32_t scale);
int32_t gfx_text_width(const char *text, int32_t scale);
void gfx_pill_badge(struct ag_surface *s, int32_t x, int32_t y, const char *text,
                    uint32_t bg_rgb, uint32_t fg_rgb, int32_t scale);
void gfx_app_icon(struct ag_surface *s, int32_t x, int32_t y, int32_t size,
                  uint32_t top_rgb, uint32_t bot_rgb, const char *glyph);

/* Whole-pixel cursor scale for a panel: 1x at 720p and below, 2x at 1080p,
   3x at 4K, clamped by AirGraph's density scale. */
int32_t gfx_cursor_scale(int32_t panel_w, int32_t panel_h);
/* Draw the pointer at the panel's density. The sprite footprint is
   12*scale x 16*scale pixels. */
void gfx_draw_cursor(struct ag_surface *s, int32_t x, int32_t y);

void gfx_draw_wifi_icon(struct ag_surface *s, int32_t x, int32_t y, int bars, uint32_t rgb);
void gfx_draw_eth_icon(struct ag_surface *s, int32_t x, int32_t y, uint32_t rgb);
void gfx_draw_battery_icon(struct ag_surface *s, int32_t x, int32_t y, int pct, int ac, uint32_t rgb);
void gfx_draw_bt_icon(struct ag_surface *s, int32_t x, int32_t y, int active, int connected, uint32_t rgb);
void gfx_draw_chevron_right(struct ag_surface *s, int32_t x, int32_t y, int32_t size, uint32_t rgb);
void gfx_draw_chevron_left(struct ag_surface *s, int32_t x, int32_t y, int32_t size, uint32_t rgb);
void gfx_draw_text_aa(struct ag_surface *s, int32_t x, int32_t y, const char *text, uint32_t rgb, int32_t scale);

void gfx_draw_bmp_scaled(struct ag_surface *s, const uint8_t *bmp, uint32_t len,
                         int32_t dx, int32_t dy, int32_t dw, int32_t dh);
void gfx_copy_fast(struct ag_surface *dst, const struct ag_surface *src);

/* Window-content transform.

   The desktop shell authors window content in 720p design coordinates. From
   1080p up it installs one transform per window: every primitive maps design
   pixels into the scaled client rectangle, clipped to that rectangle, so text
   and spacing grow with the panel instead of staying 1x inside a 3x frame.
   Identity by default, which is why 720p and every other caller stay
   byte-identical. `clip_*` is in device pixels and inclusive of x/y only. */
void gfx_transform_set(uint32_t scale_q16, int32_t ox, int32_t oy,
                       int32_t clip_x, int32_t clip_y,
                       int32_t clip_w, int32_t clip_h);
void gfx_transform_clear(void);
int gfx_transform_active(void);

#endif
