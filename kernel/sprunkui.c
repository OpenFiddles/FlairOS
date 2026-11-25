#include "../include/sprunkui.h"

static int32_t percent_of(int32_t span, unsigned int value) {
    if (span <= 0) return 0;
    if (value > 100) value = 100;
    return (span / 100) * (int32_t)value +
           ((span % 100) * (int32_t)value) / 100;
}

static void rounded(struct sp_context *ui, int32_t x, int32_t y, int32_t w,
                    int32_t h, int32_t radius, uint32_t color) {
    if (!ui || !ui->surface || w <= 0 || h <= 0) return;
    ag_round_rect(ui->device, ui->surface, x, y, w, h, radius, color);
}
static void frame(struct sp_context *ui, int32_t x, int32_t y, int32_t w,
                  int32_t h, int32_t radius, uint32_t color) {
    if (!ui || !ui->surface || w <= 0 || h <= 0) return;
    ag_round_frame(ui->device, ui->surface, x, y, w, h, radius, color);
}
static void label_at(struct sp_context *ui, int32_t x, int32_t y,
                     const char *text, uint32_t color) {
    if (ui && ui->surface && ui->draw_text && text)
        ui->draw_text(ui->surface, x, y, text, color);
}
void sp_theme_default(struct sp_theme *t) {
    if (!t) return;
    /* Material 3 dark tonal surface palette */
    t->color.background = 0x00141218;   /* surface */
    t->color.surface = 0x00211F26;      /* surfaceContainer */
    t->color.elevated = 0x002B2930;     /* surfaceContainerHigh */
    t->color.border = 0x0049454F;       /* outlineVariant */
    t->color.text = 0x00E6E0E9;         /* onSurface */
    t->color.muted = 0x00CAC4D0;        /* onSurfaceVariant */
    t->color.accent = 0x006750A4;       /* primary */
    t->color.accent_text = 0x00FFFFFF;  /* onPrimary */
    t->color.success = 0x002E9E6B;
    t->color.warning = 0x00E6A23C;
    t->color.danger = 0x00E5534B;
    t->radius = 12;                     /* M3 medium/large corner radius */
    t->spacing = 10;
    t->border_width = 1;
}
void sp_panel(struct sp_context *ui, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!ui || !ui->theme || !ui->surface) return;
    rounded(ui, x, y, w, h, ui->theme->radius, ui->theme->color.surface);
    if (ui->theme->border_width > 0)
        frame(ui, x, y, w, h, ui->theme->radius, ui->theme->color.border);
}
void sp_card(struct sp_context *ui, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!ui || !ui->theme || !ui->surface) return;
    rounded(ui, x, y, w, h, ui->theme->radius, ui->theme->color.elevated);
    if (ui->theme->border_width > 0)
        frame(ui, x, y, w, h, ui->theme->radius, ui->theme->color.border);
}
void sp_label(struct sp_context *ui, int32_t x, int32_t y,
              const char *text, int muted) {
    if (!ui || !ui->theme) return;
    label_at(ui, x, y, text, muted ? ui->theme->color.muted : ui->theme->color.text);
}
void sp_button(struct sp_context *ui, int32_t x, int32_t y, int32_t w, int32_t h,
               const char *label, int primary) {
    uint32_t bg, fg;
    if (!ui || !ui->theme || !ui->surface || w <= 0 || h <= 0) return;
    bg = primary ? ui->theme->color.accent : ui->theme->color.elevated;
    fg = primary ? ui->theme->color.accent_text : ui->theme->color.text;
    int32_t btn_r = primary ? (h / 2) : ui->theme->radius;
    rounded(ui, x, y, w, h, btn_r, bg);
    if (!primary && ui->theme->border_width > 0)
        frame(ui, x, y, w, h, btn_r, ui->theme->color.border);
    if (h > 8)
        label_at(ui, x + ui->theme->spacing, y + (h - 8) / 2, label, fg);
    else
        label_at(ui, x + ui->theme->spacing, y, label, fg);
}
void sp_badge(struct sp_context *ui, int32_t x, int32_t y,
              const char *label, uint32_t color) {
    int32_t width = 16;
    if (!ui || !ui->theme || !ui->surface || !label) return;
    for (int i = 0; label[i] && i < 24; ++i) width += 8;
    rounded(ui, x, y, width, 20, 10, color);
    label_at(ui, x + 8, y + 6, label, ui->theme->color.accent_text);
}
void sp_separator(struct sp_context *ui, int32_t x, int32_t y, int32_t w) {
    if (!ui || !ui->theme || !ui->surface || w <= 0) return;
    ag_fill_rect(ui->device, ui->surface, x, y, w, 1, ui->theme->color.border);
}
void sp_progress(struct sp_context *ui, int32_t x, int32_t y, int32_t w,
                 int32_t h, unsigned int value) {
    int32_t inner;
    if (!ui || !ui->theme || !ui->surface || w < 5 || h < 5) return;
    if (value > 100) value = 100;
    rounded(ui, x, y, w, h, h / 2, ui->theme->color.border);
    inner = percent_of(w - 4, value);
    if (inner > 0)
        rounded(ui, x + 2, y + 2, inner, h - 4, (h - 4) / 2,
                ui->theme->color.accent);
}
void sp_toggle(struct sp_context *ui, int32_t x, int32_t y, int32_t w,
               int32_t h, int enabled) {
    int32_t d, knob_x;
    if (!ui || !ui->theme || !ui->surface || w < 12 || h < 8) return;
    d = h - 4;
    if (d > w - 4) d = w - 4;
    rounded(ui, x, y, w, h, h / 2,
            enabled ? ui->theme->color.accent : ui->theme->color.border);
    knob_x = enabled ? x + w - d - 2 : x + 2;
    rounded(ui, knob_x, y + 2, d, d, d / 2, ui->theme->color.accent_text);
}
void sp_checkbox(struct sp_context *ui, int32_t x, int32_t y,
                 int32_t size, int checked) {
    if (!ui || !ui->theme || !ui->surface || size < 4) return;
    rounded(ui, x, y, size, size, 4,
            checked ? ui->theme->color.accent : ui->theme->color.surface);
    frame(ui, x, y, size, size, 4, checked ? ui->theme->color.accent :
          ui->theme->color.border);
    if (checked && size >= 8) {
        ag_fill_rect(ui->device, ui->surface, x + size / 4, y + size / 2,
                     size / 2, 2, ui->theme->color.accent_text);
    }
}
void sp_text_field(struct sp_context *ui, int32_t x, int32_t y, int32_t w,
                   int32_t h, const char *text, int focused) {
    if (!ui || !ui->theme || !ui->surface || w <= 0 || h <= 0) return;
    rounded(ui, x, y, w, h, ui->theme->radius, ui->theme->color.surface);
    frame(ui, x, y, w, h, ui->theme->radius,
          focused ? ui->theme->color.accent : ui->theme->color.border);
    label_at(ui, x + ui->theme->spacing, y + (h > 8 ? (h - 8) / 2 : 0),
             text, text && text[0] ? ui->theme->color.text : ui->theme->color.muted);
}
void sp_list_item(struct sp_context *ui, int32_t x, int32_t y, int32_t w,
                  int32_t h, const char *title, const char *subtitle, int selected) {
    if (!ui || !ui->theme || !ui->surface || w <= 0 || h <= 0) return;
    if (selected)
        rounded(ui, x, y, w, h, ui->theme->radius, ui->theme->color.elevated);
    label_at(ui, x + ui->theme->spacing, y + (subtitle ? 5 : (h > 8 ? (h - 8) / 2 : 0)),
             title, ui->theme->color.text);
    if (subtitle)
        label_at(ui, x + ui->theme->spacing, y + 17, subtitle, ui->theme->color.muted);
    if (selected && ui->theme->border_width > 0)
        frame(ui, x, y, w, h, ui->theme->radius, ui->theme->color.border);
}
void sp_slider(struct sp_context *ui, int32_t x, int32_t y, int32_t w,
               int32_t h, unsigned int value) {
    int32_t filled;
    if (!ui || !ui->theme || !ui->surface || w < 8 || h <= 0) return;
    if (value > 100) value = 100;
    rounded(ui, x, y + h / 2 - 2, w, 4, 2, ui->theme->color.border);
    filled = percent_of(w - 4, value);
    if (filled > 0)
        rounded(ui, x, y + h / 2 - 2, filled + 4, 4, 2, ui->theme->color.accent);
    rounded(ui, x + 2 + filled, y, h, h, h / 2, ui->theme->color.accent);
}
