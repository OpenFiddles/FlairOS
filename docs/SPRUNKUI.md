# SprunkUI

SprunkUI is FlairOS's small, theme-driven widget layer built on AirGraph. It provides rounded primitives and common desktop controls without owning a font renderer or input/event loop.

## Rendering contract

A platform creates an `ag_surface`, initializes an `sp_theme` with `sp_theme_default()`, and fills an `sp_context` with the graphics device, surface, theme, and optional text callback.

The text callback receives the target surface, pixel coordinates, a NUL-terminated string, and a 0x00RRGGBB color. If it is null, widgets still draw their shapes but omit text. SprunkUI does not allocate memory, retain widget state, or dispatch input; the caller supplies state such as whether a toggle is enabled or a field is focused.

## Widgets

- `sp_panel`, `sp_card`: rounded containers with theme borders.
- `sp_label`: normal or muted text.
- `sp_button`: primary or secondary rounded action surface.
- `sp_badge`: compact colored status pill.
- `sp_separator`: one-pixel divider.
- `sp_progress`: determinate progress bar; values are clamped to 0–100.
- `sp_toggle`, `sp_checkbox`: state-rendering controls; the caller owns the boolean state and input handling.
- `sp_text_field`: rounded field with focus border; the caller owns editing and cursor behavior.
- `sp_list_item`: title/subtitle row with optional selected treatment.
- `sp_slider`: horizontal value track with a thumb; the caller owns value changes and pointer/keyboard handling.

All coordinates and dimensions are pixels. Widgets clip through AirGraph's surface drawing primitives. Very small or non-positive dimensions are ignored where a useful shape cannot be drawn.

## Theme

The default palette is centralized in `struct sp_theme`: background, surface, elevated surface, border, text, muted text, accent, and semantic success/warning/danger colors. A platform can copy the default theme and override any colors, corner radius, spacing, or border width before drawing.

## Example

```c
struct sp_theme theme;
struct sp_context ui = {
    .device = &graphics,
    .surface = &window_surface,
    .theme = &theme,
    .draw_text = platform_draw_text
};

sp_theme_default(&theme);
sp_panel(&ui, 12, 12, 320, 200);
sp_label(&ui, 28, 28, "Network", 0);
sp_toggle(&ui, 280, 26, 32, 20, wifi_enabled);
sp_button(&ui, 28, 160, 112, 28, "Apply", 1);
```

The controls are intentionally presentational. A future window/compositor integration should route pointer and keyboard events to the owning app and update the state passed back to these draw functions.
