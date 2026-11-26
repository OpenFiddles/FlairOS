#include "../../include/sprunkui.h"
#include "../../include/airnim.h"

/* Animated SprunkUI widgets. Drawing is immediate-mode like the rest of
   SprunkUI; Airnimation supplies the per-widget hover/press/slide values. */

static int32_t clamp_t(int32_t t) { return t < 0 ? 0 : (t > AN_ONE ? AN_ONE : t); }
static int32_t glyph_adv(const struct sp_context *ui) { return ui->text_w > 0 ? ui->text_w : 6; }
static int32_t text_h(const struct sp_context *ui) { return 7 * (glyph_adv(ui) / 6 > 0 ? glyph_adv(ui) / 6 : 1); }
static int32_t text_px(const struct sp_context *ui, const char *s) {
    int32_t n = 0;
    while (s && s[n] && n < 64) ++n;
    return n * glyph_adv(ui);
}
static void rounded(struct sp_context *ui, int32_t x, int32_t y, int32_t w, int32_t h,
                    int32_t r, uint32_t c) {
    if (w > 0 && h > 0) ag_round_rect(ui->device, ui->surface, x, y, w, h, r, c);
}
static void frame(struct sp_context *ui, int32_t x, int32_t y, int32_t w, int32_t h,
                  int32_t r, uint32_t c) {
    if (w > 0 && h > 0) ag_round_frame(ui->device, ui->surface, x, y, w, h, r, c);
}
static void label_at(struct sp_context *ui, int32_t x, int32_t y, const char *s, uint32_t c) {
    if (ui->draw_text && s) ui->draw_text(ui->surface, x, y, s, c);
}
static int ready(const struct sp_context *ui) { return ui && ui->theme && ui->surface; }

int sp_hit(const struct sp_context *ui, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!ui || ui->pointer_x < 0 || ui->pointer_y < 0) return 0;
    return ui->pointer_x >= x && ui->pointer_x < x + w &&
           ui->pointer_y >= y && ui->pointer_y < y + h;
}

void sp_button_a(struct sp_context *ui, an_id id, int32_t x, int32_t y, int32_t w, int32_t h,
                 const char *label, int primary) {
    int hit, down;
    int32_t hover, press, inset, r, tx;
    uint32_t bg, fg, edge;
    if (!ready(ui) || w <= 0 || h <= 0) return;
    hit = sp_hit(ui, x, y, w, h);
    down = hit && ui->pointer_down;
    hover = an_track(id, hit ? AN_ONE : 0, 8, AN_OUT_CUBIC);
    press = an_track(id ^ 0x9E3779B1u, down ? AN_ONE : 0, 4, AN_LINEAR);
    bg = primary ? ui->theme->color.accent : ui->theme->color.elevated;
    fg = primary ? ui->theme->color.accent_text : ui->theme->color.text;
    /* Primary buttons lighten toward their text color, others tint toward accent. */
    bg = an_mix_color(bg, primary ? ui->theme->color.accent_text : ui->theme->color.accent,
                      clamp_t(hover) * (primary ? 18 : 22) / 100 + clamp_t(press) * 10 / 100);
    inset = clamp_t(press) * 2 / AN_ONE;
    r = primary ? h / 2 : ui->theme->radius;
    rounded(ui, x + inset, y + inset, w - 2 * inset, h - 2 * inset, r, bg);
    if (!primary && ui->theme->border_width > 0) {
        edge = an_mix_color(ui->theme->color.border, ui->theme->color.accent, clamp_t(hover));
        frame(ui, x + inset, y + inset, w - 2 * inset, h - 2 * inset, r, edge);
    }
    tx = x + (w - text_px(ui, label)) / 2;
    if (tx < x + 4) tx = x + 4;
    label_at(ui, tx, y + (h - text_h(ui)) / 2, label, fg);
}

void sp_toggle_a(struct sp_context *ui, an_id id, int32_t x, int32_t y, int32_t w, int32_t h, int on) {
    int32_t d, t, knob_x;
    if (!ready(ui) || w < 12 || h < 8) return;
    t = an_track(id, on ? AN_ONE : 0, 10, AN_OUT_BACK);   /* may overshoot slightly */
    d = h - 4;
    if (d > w - 4) d = w - 4;
    rounded(ui, x, y, w, h, h / 2,
            an_mix_color(ui->theme->color.border, ui->theme->color.accent, clamp_t(t)));
    knob_x = x + 2 + (w - d - 4) * t / AN_ONE;
    rounded(ui, knob_x, y + 2, d, d, d / 2, ui->theme->color.accent_text);
}

void sp_list_item_a(struct sp_context *ui, an_id id, int32_t x, int32_t y, int32_t w, int32_t h,
                    const char *title, const char *subtitle, int selected) {
    int32_t hover, level, nudge;
    int32_t ty;
    if (!ready(ui) || w <= 0 || h <= 0) return;
    hover = clamp_t(an_track(id, sp_hit(ui, x, y, w, h) ? AN_ONE : 0, 8, AN_OUT_CUBIC));
    level = selected ? AN_ONE : hover;
    if (level > 0)
        rounded(ui, x, y, w, h, ui->theme->radius,
                an_mix_color(ui->theme->color.surface, ui->theme->color.elevated, level));
    if (selected && ui->theme->border_width > 0)
        frame(ui, x, y, w, h, ui->theme->radius, ui->theme->color.border);
    nudge = hover * 4 / AN_ONE;   /* text eases 4px right under the pointer */
    ty = subtitle ? y + 5 : y + (h - text_h(ui)) / 2;
    label_at(ui, x + ui->theme->spacing + nudge, ty, title,
             an_mix_color(ui->theme->color.text, ui->theme->color.accent_text, hover / 4));
    if (subtitle)
        label_at(ui, x + ui->theme->spacing + nudge, y + 5 + text_h(ui) + 3, subtitle,
                 ui->theme->color.muted);
}

void sp_progress_a(struct sp_context *ui, an_id id, int32_t x, int32_t y, int32_t w, int32_t h,
                   unsigned int value) {
    if (value > 100u) value = 100u;
    sp_progress(ui, x, y, w, h, (unsigned int)an_track(id, (int32_t)value, 12, AN_OUT_CUBIC));
}

void sp_tabs_a(struct sp_context *ui, an_id id, int32_t x, int32_t y, int32_t w, int32_t h,
               const char *const *labels, unsigned int count, unsigned int selected) {
    unsigned int i;
    int32_t tab_w, ind;
    if (!ready(ui) || !labels || count == 0u || w <= 0 || h < 6) return;
    tab_w = w / (int32_t)count;
    ind = an_track(id, (int32_t)selected * tab_w, 12, AN_OUT_CUBIC);
    for (i = 0; i < count; ++i) {
        int32_t tx = x + (int32_t)i * tab_w;
        int32_t hv = clamp_t(an_track(an_id_at(id, (int32_t)i, 1),
                             sp_hit(ui, tx, y, tab_w, h) ? AN_ONE : 0, 8, AN_OUT_CUBIC));
        uint32_t col = i == selected
            ? ui->theme->color.accent
            : an_mix_color(ui->theme->color.muted, ui->theme->color.text, hv);
        label_at(ui, tx + (tab_w - text_px(ui, labels[i])) / 2, y + (h - text_h(ui)) / 2 - 1,
                 labels[i], col);
    }
    ag_fill_rect(ui->device, ui->surface, x, y + h - 1, w, 1, ui->theme->color.border);
    rounded(ui, x + ind + 8, y + h - 4, tab_w - 16, 3, 1, ui->theme->color.accent);
}

int sp_tabs_hit(const struct sp_context *ui, int32_t x, int32_t y, int32_t w, int32_t h,
                unsigned int count) {
    if (!ui || count == 0u || !sp_hit(ui, x, y, w, h)) return -1;
    return (int)((ui->pointer_x - x) / (w / (int32_t)count));
}

int32_t sp_icon_battery(struct sp_context *ui, int32_t x, int32_t y, int32_t u,
                        int percent, int charging) {
    uint32_t col;
    char txt[5];
    int n = 0;
    if (!ready(ui)) return 0;
    if (u < 1) u = 1;
    if (percent < 0) col = ui->theme->color.muted;
    else if (percent <= 15) col = ui->theme->color.danger;
    else if (percent <= 30) col = ui->theme->color.warning;
    else col = ui->theme->color.success;
    frame(ui, x, y, 19 * u, 10 * u, 3 * u, col);
    ag_fill_rect(ui->device, ui->surface, x + 19 * u, y + 3 * u, 2 * u, 4 * u, col);
    if (percent >= 0) {
        int32_t fill;
        if (percent > 100) percent = 100;
        fill = 15 * u * percent / 100;
        if (fill > 0) ag_fill_rect(ui->device, ui->surface, x + 2 * u, y + 2 * u, fill, 6 * u, col);
        if (charging) {   /* small bolt */
            ag_fill_rect(ui->device, ui->surface, x + 9 * u, y + 1 * u, 2 * u, 4 * u, ui->theme->color.text);
            ag_fill_rect(ui->device, ui->surface, x + 8 * u, y + 5 * u, 2 * u, 4 * u, ui->theme->color.text);
        }
        if (percent >= 100) { txt[n++] = '1'; txt[n++] = '0'; txt[n++] = '0'; }
        else {
            if (percent >= 10) txt[n++] = (char)('0' + percent / 10);
            txt[n++] = (char)('0' + percent % 10);
        }
        txt[n++] = '%';
    } else {
        txt[n++] = '-'; txt[n++] = '-';
    }
    txt[n] = 0;
    label_at(ui, x + 25 * u, y + (10 * u - text_h(ui)) / 2, txt, col);
    return 25 * u + text_px(ui, txt);
}

int32_t sp_icon_network(struct sp_context *ui, int32_t x, int32_t y, int32_t u,
                        int present, int up, int wireless, int signal) {
    uint32_t col, dim;
    if (!ready(ui)) return 0;
    if (u < 1) u = 1;
    col = !present ? ui->theme->color.muted
                   : (up ? ui->theme->color.success : ui->theme->color.warning);
    dim = ui->theme->color.border;
    if (wireless) {
        int bars = !up ? 0 : (signal < 0 ? 4 : (signal + 24) / 25);
        int i;
        if (bars > 4) bars = 4;
        for (i = 0; i < 4; ++i) {
            int32_t bh = (3 + 3 * i) * u;
            ag_fill_rect(ui->device, ui->surface, x + i * 5 * u, y + 12 * u - bh, 3 * u, bh,
                         i < bars ? col : dim);
        }
    } else {   /* wired: three linked boxes */
        ag_fill_rect(ui->device, ui->surface, x + 7 * u, y, 5 * u, 4 * u, col);
        ag_fill_rect(ui->device, ui->surface, x + 9 * u, y + 4 * u, 1 * u, 3 * u, col);
        ag_fill_rect(ui->device, ui->surface, x + 2 * u, y + 7 * u, 15 * u, 1 * u, col);
        ag_fill_rect(ui->device, ui->surface, x + 2 * u, y + 7 * u, 1 * u, 2 * u, col);
        ag_fill_rect(ui->device, ui->surface, x + 16 * u, y + 7 * u, 1 * u, 2 * u, col);
        ag_fill_rect(ui->device, ui->surface, x, y + 9 * u, 5 * u, 4 * u, col);
        ag_fill_rect(ui->device, ui->surface, x + 14 * u, y + 9 * u, 5 * u, 4 * u, col);
    }
    return 19 * u;
}

void sp_bar_a(struct sp_context *ui, an_id id, int32_t x, int32_t y, int32_t w, int32_t h,
              unsigned int permille, uint32_t fill_color) {
    int32_t val, fill_w;
    if (!ready(ui) || w <= 0 || h <= 0) return;
    if (permille > 1000u) permille = 1000u;
    val = an_track(id, (int32_t)permille, 10, AN_OUT_CUBIC);
    if (val < 0) val = 0;
    if (val > 1000) val = 1000;
    rounded(ui, x, y, w, h, h / 2, ui->theme->color.elevated);
    fill_w = (w * val) / 1000;
    if (fill_w > 0) {
        rounded(ui, x, y, fill_w, h, h / 2, fill_color ? fill_color : ui->theme->color.accent);
    }
}
