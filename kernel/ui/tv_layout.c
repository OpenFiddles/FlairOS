/* SPDX-License-Identifier: AGPL-3.0-only */
/* Ten-foot layout scale: one 1280x720 design, drawn correctly from 720p to 4K. */
#include "tv_layout.h"

int32_t tv_layout_scale_q16(int32_t width, int32_t height) {
    int32_t by_w, by_h, scale;

    if (width <= 0 || height <= 0)
        return TV_LAYOUT_UNIT_Q16;
    by_w = (int32_t)(((int64_t)width << 16) / TV_LAYOUT_BASE_W);
    by_h = (int32_t)(((int64_t)height << 16) / TV_LAYOUT_BASE_H);
    scale = by_w < by_h ? by_w : by_h;
    if (scale < TV_LAYOUT_UNIT_Q16)
        scale = TV_LAYOUT_UNIT_Q16;
    if (scale > 3 * TV_LAYOUT_UNIT_Q16)
        scale = 3 * TV_LAYOUT_UNIT_Q16;
    return scale;
}

int32_t tv_layout_px(int32_t base, int32_t scale_q16) {
    int64_t v;
    if (scale_q16 <= 0)
        scale_q16 = TV_LAYOUT_UNIT_Q16;
    v = ((int64_t)base * scale_q16 + (TV_LAYOUT_UNIT_Q16 / 2)) >> 16;
    return (int32_t)v;
}

int32_t tv_layout_text(int32_t base, int32_t scale_q16) {
    int32_t v = tv_layout_px(base, scale_q16);
    return v < 1 ? 1 : v;
}
