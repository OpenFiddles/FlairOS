# Airnimation, AirGraph density and WinDown metrics

Three layers cooperate so one authored layout survives every panel, from a
480x800 phone to a 3840x2160 TV:

- **AirGraph** draws pixels and owns the panel-to-device scale.
- **WinDown** arranges windows: geometry, chrome metrics and hit regions.
- **Airnimation** supplies the motion values those layers read each frame.

## AirGraph density

Geometry is authored in design pixels. `ag_scale_q16_for()` returns the smaller
of the panel/design ratios in Q16, clamped to `[1.0, 4.0]`:

```c
uint32_t scale;
ag_scale_q16_for(3840, 2160, 1280, 720, &scale);   /* 3.0 = 196608 */
int32_t bar = ag_px(12, scale);                     /* 36 device pixels */
```

`ag_px()` rounds to whole pixels and never turns a positive design length into
0, so a hairline stays visible at 1x and becomes 3 crisp pixels at 4K instead of
a resampled blur.

Two helpers keep scaled shapes crisp:

```c
ag_stroke_rect(dev, s, x, y, w, h, thickness, color);       /* integer stroke */
ag_round_rect_scaled(dev, s, x, y, w, h, design_radius, scale, color);
```

`ag_stroke_rect` draws inside the box with four fills; a thickness at or above
half the box fills it. `ag_round_rect_scaled` converts the design radius once so
a 4K corner is a real 4x radius.

## WinDown density

An unconfigured compositor keeps the legacy 1 px chrome, which is why the
retired i386 session stays pixel-identical. A compositor can adopt a panel:

```c
wd_init(&comp);
wd_set_display(&comp, 3840, 2160);   /* 3x chrome from a 1280x720 design */
struct wd_metrics m = wd_chrome_metrics(&comp);
/* m.title_h == 36, m.inset == 24, m.button_w == 36, m.button_gap == 18,
   m.grip == 30 */
```

Configured compositors arrange windows in **device pixels**. `wd_composite()`
then draws the scaled chrome through `desktop_chrome_draw_scaled()` (36 px title
bar, 36 px traffic lights, real resize grip) and reserves that header height for
the client surface instead of one grid row. `wd_hit_chrome()` computes the close,
minimize and zoom regions from the same metrics, so input and pixels cannot
drift.

Adopting a larger panel can rescale everything already open:

```c
wd_rescale_display(&comp, 1280, 720, 3840, 2160);
```

Every window and its saved (un-zoomed) geometry scales in proportion, and the
chrome scale follows. Passing `0` for a size is refused.

`wd_set_scale_q16()` is the direct override for panels that already know their
density.

## Animation presets

Airnimation tracks integer values per `an_id`. `an_track(id, target, ticks,
ease)` glides toward a target; `an_play()` runs a one-shot tween. Springs stay
available through `an_spring_*` for physics-driven motion.

Named presets remove the per-call choice of duration and curve:

| Preset | Progress | Ticks | Ease |
| --- | --- | --- | --- |
| `fade-in` | 0 → 1000 | 12 | out-cubic |
| `fade-out` | 1000 → 0 | 10 | in-out |
| `slide-in` | 0 → 1000 | 16 | out-cubic |
| `slide-out` | 1000 → 0 | 14 | in-out |
| `pop` | 0 → 1000 | 14 | out-back (overshoots) |
| `settle` | 0 → 1000 | 18 | in-out |

```c
an_preset_play(an_id_str("drawer"), "slide-in");
int32_t offset = an_preset_offset(an_id_str("drawer"),
                                  an_preset_lookup("slide-in"), span);
```

- `an_preset_count()` / `an_preset_at()` enumerate the table.
- `an_preset_lookup()` returns `NULL` for an unknown name; `an_preset_play()`
  returns `-1` and leaves the engine untouched rather than guessing.
- `an_preset_value()` clamps progress to `0..AN_ONE` (out-back overshoot is
  clamped for consumers that need a fraction).
- `an_preset_offset()` returns `span` at progress 0 and 0 at `AN_ONE`, which is
  the direction a slide-in wants.
- With animations disabled every preset snaps to its end state, like the rest of
  the engine.

Add a preset to `kernel/gfx/airnim.c` and extend `tests/airnim_host_test.c`
rather than teaching a shell a new curve.

## Where each piece is tested

| Area | Host test |
| --- | --- |
| Density scale, `ag_px`, strokes, scaled radii | `tests/airgraph_host_test.c` |
| Legacy and 4K chrome metrics, hit regions, scaled composite, display adoption | `tests/windown_host_test.c` |
| Preset table, lookup, play, disabled snap, offsets | `tests/airnim_host_test.c` |
| Animated widgets on top of the engine | `tests/sprunkui_anim_host_test.c` |

All of these run under `make test`; `make verify-disk` still gates the twelve
preset images, so a rendering regression that changes a booted frame fails the
gate rather than passing quietly.
