/* SPDX-License-Identifier: AGPL-3.0-only */
/* Ten-foot layout scale: one 1280x720 design, drawn correctly from 720p to 4K.
   The ratio math is AirGraph's; only the clamp bounds are session policy. */
#include "tv_layout.h"
#include "../../include/airgraph.h"

int32_t tv_layout_scale_q16(int32_t width, int32_t height) {
    uint32_t scale = ag_scale_ratio_q16(width, height,
                                        TV_LAYOUT_BASE_W, TV_LAYOUT_BASE_H);

    if (scale == 0u)
        return TV_LAYOUT_UNIT_Q16;
    if (scale < TV_LAYOUT_UNIT_Q16)
        scale = TV_LAYOUT_UNIT_Q16;
    if (scale > 3u * TV_LAYOUT_UNIT_Q16)
        scale = 3u * TV_LAYOUT_UNIT_Q16;
    return (int32_t)scale;
}

int32_t tv_layout_px(int32_t base, int32_t scale_q16) {
    if (base < 0)
        return -ag_px(-base, (uint32_t)scale_q16);
    return ag_px(base, (uint32_t)scale_q16);
}

int32_t tv_layout_text(int32_t base, int32_t scale_q16) {
    int32_t v = tv_layout_px(base, scale_q16);
    return v < 1 ? 1 : v;
}
