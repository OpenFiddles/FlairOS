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

| Preset | Kind | Progress | Ticks | Ease | Direction | Distance |
| --- | --- | --- | --- | --- | --- | --- |
| `fade-in` | fade | 0 → 1000 | 12 | out-cubic | — | — |
| `fade-out` | fade | 1000 → 0 | 10 | in-out | — | — |
| `slide-in` | slide | 0 → 1000 | 16 | out-cubic | left | 25% |
| `slide-out` | slide | 1000 → 0 | 14 | in-out | right | 25% |
| `pop` | pop | 0 → 1000 | 14 | out-back (overshoots) | — | 80→100% scale |
| `settle` | fade | 0 → 1000 | 18 | in-out | — | — |
| `slide-in-left` / `-right` / `-top` / `-bottom` | slide | 0 → 1000 | 16 | out-cubic | edge | 25% of width/height |
| `slide-out-left` / `-right` / `-top` / `-bottom` | slide | 1000 → 0 | 14 | in-out | edge | 25% of width/height |
| `crossfade` | cross-fade | 0 → 1000 | 30 | in-out | — | two screen weights |

Each preset is data, not a call-site decision: `kind`, `edge`, `distance_pm`
(permille of the element's own extent) and `AN_PRESET_COMPOSE_VIEWPORT` live in
the table. A caller asks for a preset by name and resolves it with
`an_preset_motion()`:

```c
int n = an_preset_count();                 /* enumerate the API */
for (int i = 0; i < n; i++)
    puts(an_preset_at(i)->name);

const struct an_preset *p = an_preset_lookup("slide-in-right");
if (!p) return;                            /* unknown name: caller decides */
if (an_preset_play(id, "slide-in-right") != 0) return;
struct an_motion m;
an_preset_frame(p, an_preset_value(id, p),
                x, y, w, h, panel_w, panel_h, ag_scale, &m);
draw_at(x + m.dx, y + m.dy);               /* alpha = m.alpha */
```

`an_preset_count()` / `an_preset_at()` list the table, `an_preset_exists()`
reports whether a name is present, `an_preset_lookup()` and `an_preset_play()`
resolve a name (both return NULL/-1 for an unknown name and leave the engine
untouched), and `an_preset_frame()` resolves the motion from the requested
rectangle and clamps it to the requested viewport. `an_preset_finished(id)` is
0 while that target is producing in-between values and 1 once it has settled.
The same call works for 800x600, 1920x1080, 3840x2160, portrait and landscape
panels; there is no panel-specific geometry in the caller.

`distance_pm` is a fraction of the element, so 25% of a 100 px card is 25 px
and 25% of the same card at 4K is 3x that: the visual fraction is identical on
800x600, 1080p, 4K, portrait and landscape. `AN_PRESET_COMPOSE_VIEWPORT` marks
presets whose distance is authored as a fraction of the element, so they stay
correct when AirGraph scales the panel; the motion code must not multiply that
fraction by the viewport scale a second time. `an_motion_clamp()` keeps the
animated rectangle inside the panel, so a slide cannot leave the viewport.
For a screen transition, `an_preset_motion()` returns `alpha_a`/`alpha_b` and
`an_crossfade_rect()` draws the real pixel mix of the two content screens.

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

## Booted preset proof

`ui/shells/anim_demo.c` enumerates the preset table and plays every entry by
name, one per phase, from the desktop and mobile shells after startup. It
resolves each frame against the card's requested rectangle and clamps it to the
panel. The desktop also uses presets for real UI: visible windows enter with
`slide-in` and the dock rises with `slide-in-bottom` on first boot. `make
verify-disk` captures successive frames while the tour runs, requires a visible
frame delta, at least one captured in-between frame that differs from both the
start and the settled end, a settled final frame, and checks the serial log
shows every enumerated index (currently 15/15) was requested on every animated
image. The 800x600, 1024x768, 1080p, 4K, 480x800 portrait and 800x480
phone-landscape images all run it.

## Wired into the shipping desktop

`ui/shells/desk_windows.c` is the desktop shell's window layer and the headless
harness's subject. It configures density from the framebuffer the shell is
handed (`dw_configure`), draws chrome with `desktop_chrome_draw_scaled()` when
the panel is above 720p, routes close/minimize/zoom/title-bar input through
`wd_hit_chrome()`, and plays the `slide-in` preset for a window open. At 720p
and below `dw_active()` is false, so the shell keeps its original 1 px chrome
and pixel hit regions; the i386 reference compositor never configures density
at all.

When the framebuffer size changes, the shell calls `dw_rescale()`: every window
and its saved geometry scale with the panel, are clamped on-screen below the
menu bar, and the compositor adopts the new metrics before the next frame.

### Content, not just chrome

Window content is authored in 720p coordinates. In density mode the shell asks
`dw_client_rect()` for the scaled client area and installs one transform around
that window's content:

```c
gfx_transform_set(scale_q16, ox, oy, client.x, client.y, client.w, client.h);
/* existing content drawing, unchanged coordinates */
gfx_transform_clear();
```

Every glass/AirGraph primitive maps design pixels into the client rectangle and
clips there, so a 20 px inset becomes a 60 px inset at 4K, text scales with its
glyph boxes, and content cannot paint over chrome. The transform is identity by
default, which is why 720p output (and every other caller of these primitives)
stays byte-identical; clicks are mapped back into the same design space before
the content handlers see them.

### One scale, three sessions

The TV and phone layouts no longer carry their own ratio math: both call
`ag_scale_ratio_q16()` from `airgraph.h` and apply their own 3x clamp. Their
host tests assert the layout scale equals that shared ratio and that
`*_layout_px()` equals `ag_px()`.

`tests/desk_windows_host_test.c` renders that layer's chrome into a real
3840x2160 surface and asserts the title bar height, traffic-light boxes, grip
pixel, a close click at the scaled button centre, a title-bar drag, and a
720p→4K rescale that keeps all eight windows on-screen and 3x proportional.

## Where each piece is tested

| Area | Host test |
| --- | --- |
| Density scale, `ag_px`, strokes, scaled radii | `tests/airgraph_host_test.c` |
| Legacy and 4K chrome metrics, hit regions, scaled composite, display adoption | `tests/windown_host_test.c` |
| Preset table, lookup, play, disabled snap, offsets, timing/easing | `tests/airnim_host_test.c` |
| Direction, distance fraction, viewport clamping, cross-fade mix | `tests/airnim_host_test.c` |
| Animated widgets on top of the engine | `tests/sprunkui_anim_host_test.c` |
| Shipping desktop density, scaled clicks, preset open, 4K rescale | `tests/desk_windows_host_test.c` |

All of these run under `make test`; `make verify-disk` still gates the eighteen
64-bit preset images, so a rendering regression that changes a booted frame
fails the gate rather than passing quietly.
