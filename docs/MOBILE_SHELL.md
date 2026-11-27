# Mobile Shell (phone session)

The phone session (`FLAIROS_DEVICE_PHONE`, `ui/shells/shell_mobile.c`) is a
SpringBoard-style home screen with full-screen apps. Like the TV session, its
home screen and every label are a **package**, and its geometry is authored once
in 480x800 design coordinates and scaled to the panel.

## One layout, every phone

`kernel/ui/mobile_layout.c` turns the panel size into a Q16 scale:

```c
int32_t scale = mobile_layout_scale_q16(s->width, s->height); /* 1.0 = 65536 */
int32_t x     = mobile_layout_px(24, scale);                  /* design px     */
int32_t size  = mobile_layout_text(2, scale);                 /* never below 1 */
```

The scale is the smaller of the horizontal and vertical ratios, clamped to
`[1.0, 3.0]`:

| Surface | Scale | Design 68 px icon | Design 24 px margin |
| --- | --- | --- | --- |
| 480x800 (design) | 1.0 | 68 | 24 |
| 720x1280 | 1.5 | 102 | 36 |
| 1080x1920 | 2.25 | 153 | 54 |
| 1440x2560 | 3.0 | 204 | 72 |
| below 480x800 | 1.0 (clamped) | 68 | 24 |

Rendering and hit testing read the same helpers, so a tap always lands on what
was drawn. Nothing in the shell uses a compile-time panel size.

## The home screen is a package

```
mobile/manifest/mobile.json    session manifest (id org.flairos.mobile, entry "mobile")
mobile/resources/launcher.json tiles, dock, title, hint
mobile/resources/strings.json  every visible shell label (29 keys)
```

`make` packs these into `build/FlairMobile.airp`: a resources-only *component*
package — a validated container with typed resources and no executable.
`kernel/ui/shell_launcher.c` (shared with the TV session, selected by
`SHELL_KIND_MOBILE`) reads the `mobile` root object out of a staged package with
`airp_open`/`airp_find_resource`, and `kmain64` hands the staged bytes to the
shell through `struct shell_boot_info.session_pkg`.

```json
{
  "mobile": {
    "title": "FlairOS Mobile",
    "tiles": [
      { "id": "browser", "label": "Browser", "target": "screen:browser",
        "glyph": "BR", "icon": 5, "accent": "3B82F6" }
    ],
    "dock": ["browser", "files", "activity", "settings"]
  }
}
```

Tile targets are declarative:

| Target | Meaning |
| --- | --- |
| `screen:browser` | AirBrowse surface |
| `screen:files` | VFS browser (`/`, `/apps`, `/home`) |
| `screen:calc` | calculator |
| `screen:notes` | VFS notes editor |
| `screen:activity` | ART activity plus SurfaceFlinger surface |
| `screen:settings` | Wi-Fi, cellular, airplane mode, animations, theme |
| `app:N` | launch Android (ART) package index `N`, if it exists |

Unknown targets are ignored rather than guessed. The dock must name declared
tiles, so a package cannot produce a dock button that opens nothing.

The grid takes up to eight tiles in three columns. The widget's `y` is computed
from the row count, so a third row pushes it down instead of overlapping it.

## Strings

`strings.json` is a flat key → text map carrying every label the shell draws:
status bar (`app`, `status_pill`), widget (`widget_title`, `widget_hint`), files
(`files_root`, `files_apps`, `files_home`), notes (`save`, `notes_*`), calculator
(`calc_error`), activity (`activity_*`), and settings (`settings_title`,
`wifi_*`, `cell_*`, `airplane_*`, `anim_*`, `theme_*`).

Layering: the package wins, the draw site supplies its own fallback, and a key
the package omits keeps the built-in wording. The layout keeps a *view* of the
packaged JSON rather than a copy, because those bytes already live in the image.
The built-in key table is per session, so a phone never inherits a ten-foot
label (and vice versa).

## Control centre

The phone control centre is the shared AirStinUI drawer — the same renderer,
geometry and hit test the desktop and TV use — with the drawer scale set to the
phone scale:

```c
airstin_ui_set_scale_q16(scale);
airstin_render_control_center(s, w, cc_open, cc_anim, anim_on, theme_idx);
airstin_cc_handle_click(mx, my, w, &anim, &theme, &spot, &cc_open);
```

Because `airstin_cc_geometry_for()` is the single source of truth for the panel
rect and row heights, the overlay and its hit rect cannot drift apart. Tapping
outside the panel closes it. Tapping the right side of the status bar toggles it.

## Build and test

64-bit phone preset (aarch64 by default, also x86_64):

```sh
python3 tools/build_preset.py mobile aarch64   # or: make presets
```

`presets/mobile/preset.json` declares 480x800, `FLAIROS_DEVICE_PHONE`,
`FLAIROS_DISPLAY_PORTRAIT` and `"session_package": "mobile"`, so the packaging
step also embeds `FlairMobile.airp` in the image.

Host tests and the packaged airp:

```sh
make test        # includes mobile_host_test and build/FlairMobile.airp
```

`make mobile` is a different, legacy i386 reference image that uses
`kernel/ui/mobile.c`; it is not this session.

## Package rejection

The loader is all-or-nothing and bounds every value. A package is refused when
the container fails validation, the `resources-only` flag is absent,
`launcher.json`/`strings.json` is missing or of the wrong resource type, the
JSON is malformed, the `mobile` object is absent, the tile array is empty or
longer than eight, a tile is missing `id`/`label`/`target`, an `icon` is negative,
an `accent` is invalid, a dock entry names no tile or the dock is longer than
four, or a string key/value is over-long. A refused package leaves the previous
home screen and wording exactly as they were, and
`struct shell_launcher_report.reason` says why.

## Validation status

Verified in this environment:

- `build/mobile_host_test` passes: design/720p/1080p/3x-clamp/sub-design scale,
  built-in layout, the shipped `FlairMobile.airp` (6 tiles, 4-slot dock, glyphs,
  icons, accents, 29 strings), TV-layout rejection, unknown dock id, invalid
  icon, over-long dock, malformed strings, and the untouched-on-rejection rule.
- `build/tv_host_test` and `build/tv_drawer_host_test` still pass after the
  loader rename to `shell_launcher`.
- `ui/shells/shell_mobile.c`, `kernel/ui/shell_launcher.c`,
  `kernel/ui/mobile_layout.c` and `kernel/boot/kmain64.c` compile freestanding
  with `-Os -Wall -Wextra`.
- `make test` builds and validates `build/FlairMobile.airp`.

Not verified here, and not claimed:

- No 64-bit phone image was linked or booted: this environment has no `clang`,
  `ld.lld`, NAND/disk tooling or QEMU, so `make presets` and `make verify-disk`
  cannot run. The 480x800 panel geometry and the packaged strings have therefore
  not been seen on a real framebuffer.
- No physical phone touch hardware was exercised.
- The shell draws only what the kernel resolves; installing or updating a
  `FlairMobile.airp` on a running device is still a boot-image concern.
