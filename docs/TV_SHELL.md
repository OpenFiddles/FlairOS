# TV Shell (10-Foot UI)

The TV session (`FLAIROS_DEVICE_TV`) is the lean-back, D-pad-driven shell. Its
whole surface — launcher and control drawer alike — is authored once and scaled
to the panel, and every label it draws comes from a packaged resource.

## One layout, every panel

`ui/shells/shell_tv.c` is laid out once in 1280x720 **design coordinates** and
scaled to the surface it draws on by `kernel/ui/tv_layout.c`:

```c
int32_t scale = tv_layout_scale_q16(s->width, s->height); /* Q16, 1.0 = 65536 */
int32_t margin = tv_layout_px(48, scale);                 /* 48 -> 144 at 4K */
int32_t title_size = tv_layout_text(3, scale);            /* never below 1     */
```

The scale is the smaller of the horizontal and vertical ratios, clamped to
`[1.0, 3.0]`:

| Surface | Scale | Margin (design 48 px) |
| --- | --- | --- |
| 1280x720 (720p) | 1.0 | 48 |
| 1920x1080 (1080p) | 1.5 | 72 |
| 2560x1440 (1440p) | 2.0 | 96 |
| 3840x2160 (4K) | 3.0 | 144 |
| 4096x2160 | 3.0 (clamped) | 144 |
| below 720p | 1.0 (clamped) | 48 |

Two properties are deliberate:

- **The smaller axis wins**, so a 21:9 or portrait surface never pushes content
  off the edge.
- **3.0 is the ceiling.** The design stops scaling at 4K; beyond it the layout
  keeps 4K geometry rather than growing off-screen.

Text sizes are scaled with a floor of 1 px so a rounding step can never erase a
glyph. The focus spring, hit testing and the focus ring all read the same
scaled geometry, so they cannot disagree at any scale. The spring target is
derived from the rendered surface width — never from the compile-time
`PRESET_W` — which is what previously broke focus on a 4K panel.

Overscan safety: the design keeps a 48 px (design) margin on every side, so the
scaled layout keeps the equivalent inset on a panel that crops edges.

## Control drawer

The control drawer is AirStinUI, shared with the desktop and mobile shells, so
it cannot simply assume a TV. Instead:

- it keeps its existing 1280x720 pixel design and draws through a pen that
  scales design units to the surface (`ui/systemui/airstin_ui.c`);
- the scale defaults to **1.0**, so desktop and mobile draw exactly the pixels
  they always have — the identity path is a plain pass-through, not a rounding
  of `x * 65536 >> 16`;
- a TV session calls `airstin_ui_set_scale_q16()` with its ten-foot scale, once
  per frame and once per input event, so the drawer, the status cluster, the
  power modal and the installer all grow together;
- `airstin_cc_geometry_for()` publishes the panel rect, row height and glyph
  sizes, and both the renderer and `airstin_cc_handle_click()` read it, so a
  click lands on what was drawn at every scale.

| Surface | Drawer rect | Row | Title / label glyph |
| --- | --- | --- | --- |
| 720p (no scale set) | 988,40 280x336 | 38 | 2 / 1 |
| 1080p | 1482,60 420x504 | 57 | 3 / 2 |
| 1440p | 1976,80 560x672 | 76 | 4 / 2 |
| 4K | 2964,120 840x1008 | 114 | 6 / 3 |
| 5K (clamped) | 4244,120 840x1008 | 114 | 6 / 3 |

Sub-720p surfaces (for example 800x600) hold the 1x drawer rather than shrinking
below the design. The panel's drop shadow inside `gfx_glass_panel` stays a fixed
3/6 px; that is a shared visual, not layout.

## Launcher as a component package

The home-screen tiles are a resource, not kernel tables:

```
tv/manifest/tv.json        session manifest (id org.flairos.tv, entry mode "tv")
tv/resources/launcher.json ten-foot layout: title, hint, tiles
tv/resources/strings.json  every shell label
```

`make` packs these into `build/FlairTV.airp` — a *component* package: the
`resources-only` flag means it is a validated container with typed resources and
**no executable**. `kernel/ui/tv_launcher.c` reads `launcher.json` and
`strings.json` out of a staged package with the same `airp_open` /
`airp_find_resource` path the desktop theme uses, and `AirJSON` iteration
(`airjson_array_count`/`airjson_array_at`, `airjson_object_count`/
`airjson_object_at`) walks the tile array and the string map.

```json
{
  "tv": {
    "title": "FlairTV Living Room",
    "hint": "UP: Control Drawer  |  LEFT/RIGHT: Focus  |  ENTER: Launch",
    "tiles": [
      { "id": "hello", "label": "Hello World", "subtitle": "Android APK",
        "target": "app:0", "accent": "EC4899" }
    ]
  }
}
```

Tile targets are declarative; the shell resolves them and ignores anything it
does not recognise:

| Target | Meaning |
| --- | --- |
| `app:N` | launch Android (ART) package index `N`, if it exists |
| `airp:cast` | start the AirP screen stream (`AIRP_PAY_SCREEN`) |

### Strings

`strings.json` is a flat object of key → text. The shell asks for a fixed key
set, so a package restyles the wording without the kernel knowing the words:

| Key | Where it is drawn | Built-in wording |
| --- | --- | --- |
| `app` | top-left session name | `FlairTV` |
| `home` | home pill | `HOME` |
| `sections` | section strip | `Apps   Android   AirP   Status` |
| `focus` | hero badge | `10-FOOT FOCUS` |
| `status_card` | hero status card | `AIRP + BT AUDIO` |
| `focused` / `open` | tile state pill | `FOCUSED` / `OPEN` |

Keys the package adds are kept but drawn nowhere; keys it omits keep the
built-in wording, because the package starts from the built-in string set. The
tile labels, title and hint come from `launcher.json`.

Retiling or rewording the TV session is therefore a packaging change: edit the
JSON, repack, reboot. No kernel rebuild to move a tile or change a label.

### Where the package comes from at boot

`tools/build_preset.py` packs the package for any preset that declares
`"session_package": "tv"`, validates it with `tools/airpkg.py`, and emits it as a
C array into the preset build directory. The image links those bytes and
`kmain64` hands them to the shell through
`struct shell_boot_info.session_pkg`. The container is architecture-neutral data,
so the package is opened with `AIRP_ARCH_ANY`.

### When the package is wrong

The loader is all-or-nothing and bounds every value:

- container fails validation, wrong flags, missing `launcher.json` or
  `strings.json`, wrong resource type, malformed JSON, missing `tv` object,
  empty or over-long tile array (limit 8), missing `id`/`label`/`target`, a label
  longer than its buffer, an over-long string value, or a string map with no
  keys → the load is refused and the **built-in layout and wording stay in
  place**;
- `struct tv_launcher_report` carries a reason string for diagnostics.

A refused package never half-applies: the shell keeps either the packaged
layout/text or the built-in one, never a mixture.

## Presets

| Preset | Panel | Session package |
| --- | --- | --- |
| `presets/tv` | 1280x720 | `FlairTV.airp` |
| `presets/tv4k` | 3840x2160, `FLAIROS_4K_UHD`, 90 Hz | `FlairTV.airp` |

Both build with `python3 tools/build_preset.py --preset tv4k --arch aarch64`
(or `--all`).

## Verification

Two host tests are wired into `make test` and CMake, and drive the real code
paths rather than reporting constants back:

`tests/tv_host_test.c`
- the layout scale at 720p, 1080p, 1440p, 4K, 5K and sub-720p surfaces, plus
  pixel and text scaling;
- AirJSON array and object iteration over the **shipped**
  `tv/resources/launcher.json` and `tv/resources/strings.json`;
- loading a package built the way `tools/airpkg.py` builds it, including tile
  labels, targets, accent colours and every string;
- a package string overriding built-in wording, and undeclared keys keeping
  theirs;
- every rejection path above, including bad strings, and that a refusal leaves
  layout **and** text untouched;
- the unstaged fallback declaring every label the shell draws.

`tests/tv_drawer_host_test.c`
- the drawer geometry at 720p, 1080p, 1440p, 4K, 800x600 and the 5K clamp,
  including row height and glyph sizes;
- the default (no scale set) drawer staying the legacy 988,40 280x336 rect with
  2/1 glyphs — the desktop and mobile case;
- the real renderer painting exactly the scaled panel rect at 720p, 1080p and
  4K, measured from the pixels;
- the status cluster scaling from the right edge on 4K;
- the real hit test: a click inside the 4K drawer's Wi-Fi row toggles it, the
  same click at unscaled 720p coordinates misses, and the 720p coordinates still
  work at 720p.

`make test` also builds and validates `build/FlairTV.airp`, so a broken resource
fails the test run rather than the TV.

## Known gaps

- The desktop session still lays out its own launcher tables; only the TV
  session reads its layout from a package.
- The 64-bit TV images build and link only where `clang`, `ld.lld` and the
  linker scripts are available; the host tests cover layout, drawer, hit testing
  and package paths, not a booted 4K panel.
- Session icons still come from `.rodata`, and the drawer's own labels
  ("AirStinUI Control", "Search", "Power", …) are the shared SystemUI wording
  rather than session strings.
