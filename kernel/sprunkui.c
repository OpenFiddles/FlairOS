#include "../include/sprunkui.h"

static void rounded(struct sp_context *ui, int32_t x, int32_t y, int32_t w,
                    int32_t h, int32_t radius, uint32_t color) {
    if (!ui || !ui->surface) return;
    ag_round_rect(ui->device, ui->surface, x, y, w, h, radius, color);
}
void sp_theme_default(struct sp_theme *t) {
    if (!t) return;
    t->color.background = 0x00131A2A;
    t->color.surface = 0x001E293B;
    t->color.elevated = 0x002B3A52;
    t->color.border = 0x003B4B65;
    t->color.text = 0x00F1F5F9;
    t->color.muted = 0x0094A3B8;
    t->color.accent = 0x007C83FF;
    t->color.accent_text = 0x00FFFFFF;
    t->color.success = 0x0034D399;
    t->color.warning = 0x00FBBF24;
    t->color.danger = 0x00FB7185;
    t->radius = 10;
    t->spacing = 8;
    t->border_width = 1;
}
void sp_panel(struct sp_context *ui, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!ui || !ui->theme) return;
    rounded(ui, x, y, w, h, ui->theme->radius, ui->theme->color.surface);
    if (ui->theme->border_width > 0)
        ag_round_frame(ui->device, ui->surface, x, y, w, h,
                       ui->theme->radius, ui->theme->color.border);
}
void sp_card(struct sp_context *ui, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!ui || !ui->theme) return;
    rounded(ui, x, y, w, h, ui->theme->radius, ui->theme->color.elevated);
    ag_round_frame(ui->device, ui->surface, x, y, w, h,
                   ui->theme->radius, ui->theme->color.border);
}
void sp_button(struct sp_context *ui, int32_t x, int32_t y, int32_t w, int32_t h,
               const char *label, int primary) {
    uint32_t bg, fg;
    if (!ui || !ui->theme) return;
    bg = primary ? ui->theme->color.accent : ui->theme->color.elevated;
    fg = primary ? ui->theme->color.accent_text : ui->theme->color.text;
    rounded(ui, x, y, w, h, ui->theme->radius, bg);
    if (!primary)
        ag_round_frame(ui->device, ui->surface, x, y, w, h,
                       ui->theme->radius, ui->theme->color.border);
    if (label && ui->draw_text)
        ui->draw_text(ui->surface, x + ui->theme->spacing,
                      y + (h > 8 ? (h - 8) / 2 : 0), label, fg);
}
void sp_badge(struct sp_context *ui, int32_t x, int32_t y,
              const char *label, uint32_t color) {
    int32_t width = 16;
    if (!ui || !ui->theme || !label) return;
    for (int i = 0; label[i] && i < 24; ++i) width += 8;
    rounded(ui, x, y, width, 20, 10, color);
    if (ui->draw_text)
        ui->draw_text(ui->surface, x + 8, y + 6, label, ui->theme->color.accent_text);
}
void sp_separator(struct sp_context *ui, int32_t x, int32_t y, int32_t w) {
    if (!ui || !ui->theme) return;
    ag_fill_rect(ui->device, ui->surface, x, y, w, 1, ui->theme->color.border);
}
void sp_progress(struct sp_context *ui, int32_t x, int32_t y, int32_t w,
                 int32_t h, unsigned int value) {
    int32_t inner;
    if (!ui || !ui->theme || w <= 0 || h <= 0) return;
    if (value > 100) value = 100;
    rounded(ui, x, y, w, h, h / 2, ui->theme->color.border);
    inner = (w - 4) * (int32_t)value / 100;
    if (inner > 0)
        rounded(ui, x + 2, y + 2, inner, h - 4, (h - 4) / 2,
                ui->theme->color.accent);
}
