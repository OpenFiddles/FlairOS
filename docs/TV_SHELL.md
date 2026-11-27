# TV Shell (10-Foot UI)

The TV session (`FLAIROS_DEVICE_TV`) is the lean-back, D-pad-driven shell. It has
two pieces of state that must not be compiled into the kernel: how large the
ten-foot layout should be, and which tiles the launcher shows.

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
scaled geometry from one helper, so they cannot disagree at any scale. The
spring target is derived from the rendered surface width — never from the
compile-time `PRESET_W` — which is what previously broke focus on a 4K panel.

Overscan safety: the design keeps a 48 px (design) margin on every side, so the
scaled layout keeps the equivalent inset on a panel that crops edges.

## Launcher as a package

The home-screen tiles are a resource, not kernel tables:

```
tv/manifest/tv.json        session manifest (id org.flairos.tv, entry mode "tv")
tv/resources/launcher.json ten-foot layout: title, hint, tiles
tv/resources/strings.json  session strings
```

`make` packs these into `build/FlairTV.airp` — a *component* package: the
`resources-only` flag means it is a validated container with typed resources and
**no executable**. `kernel/ui/tv_launcher.c` reads `launcher.json` out of a
staged package with the same `airp_open` / `airp_find_resource` path the desktop
theme uses, and `AirJSON` array iteration (`airjson_array_count` /
`airjson_array_at`) walks the `tiles` array.

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

Retiling the TV session is therefore a packaging change: edit
`tv/resources/launcher.json`, repack, reboot. No kernel rebuild to move a tile.

### Where the package comes from at boot

`tools/build_preset.py` packs the package for any preset that declares
`"session_package": "tv"`, validates it with `tools/airpkg.py`, and emits it as a
C array into the preset build directory. The image links those bytes and
`kmain64` hands them to the shell through
`struct shell_boot_info.session_pkg`. The container is architecture-neutral data,
so the package is opened with `AIRP_ARCH_ANY`.

### When the package is wrong

The loader is all-or-nothing and bounds every value:

- container fails validation, wrong flags, missing `launcher.json`, wrong
  resource type, malformed JSON, missing `tv` object, empty or over-long tile
  array (limit 8), missing `id`/`label`/`target`, or a label longer than the
  31-character tile buffer → the load is refused and the **built-in 10-foot
  layout stays in place**.
- `struct tv_launcher_report` carries a reason string for diagnostics.

A refused package never half-applies: the shell keeps either the packaged
layout or the built-in one, never a mixture.

## Presets

| Preset | Panel | Session package |
| --- | --- | --- |
| `presets/tv` | 1280x720 | `FlairTV.airp` |
| `presets/tv4k` | 3840x2160, `FLAIROS_4K_UHD`, 90 Hz | `FlairTV.airp` |

Both build with `python3 tools/build_preset.py --preset tv4k --arch aarch64`
(or `--all`).

## Verification

`tests/tv_host_test.c` (wired into `make test` and CMake) covers:

- the scale at 720p, 1080p, 1440p, 4K, 5K and sub-720p surfaces, plus pixel and
  text scaling;
- AirJSON array count/at over the **shipped** `tv/resources/launcher.json`;
- loading a package built the way `tools/airpkg.py` builds it, including tile
  labels, targets and accent colours;
- every rejection path above, and that a refusal leaves the layout untouched;
- the unstaged fallback.

`make test` also builds and validates `build/FlairTV.airp`, so a broken resource
fails the test run rather than the TV.

## Known gaps

- The control drawer (AirStinUI) is shared with the desktop and mobile shells
  and still draws at its own pixel sizes; it is not yet scaled by
  `tv_layout_scale_q16`.
- The 64-bit TV images build and link only where `clang`, `ld.lld` and the
  linker scripts are available; this repository's host tests cover the layout
  and package paths, not a booted 4K panel.
- Icons still come from `.rodata`; `strings.json` is packaged and validated but
  the shell does not read its text yet.
