# Desktop architecture

The desktop is a session shell: it owns the screen, the input loop and the
application launcher. On i386 it is implemented in the kernel
(`kernel/ui/desktop.c`, `kernel/ui/desktop_gfx.c`, `kernel/ui/desktop_render.c`,
`kernel/ui/desktop_chrome.c`).

## Design language

Material 3-inspired, with a distinct FlairOS identity:

| Property | Value | Reason |
| --- | --- | --- |
| Corner radius | 12 px default, 28 max | Rounded surfaces without losing small widgets |
| Spacing | 10 px default, 40 max | Consistent rhythm across panels |
| Border width | 1 px default, 8 max | Visible edges at 1x scaling |
| Palette | Dark tonal surfaces | Matches the shipped `theme.json` |
| Accent | Violet default, teal alternate | One accent drives selection and focus |

All four bounds are enforced: a theme outside them is rejected rather than
clamped, so a broken package cannot produce an unusable session.

## Structure

| Element | Responsibility |
| --- | --- |
| Background | Gradient and wallpaper rendering |
| Dock | Application launcher strip |
| Taskbar | Open windows, clock, status icons |
| Start menu | Categorised application list |
| Panels | Settings and system surfaces |
| App context | Title, body region and window chrome for a running app |

## Device experiences

The shell is not one layout stretched across devices. Profiles select both
resolution and behaviour:

| Profile | Target | Differences |
| --- | --- | --- |
| Desktop | Mouse, keyboard, landscape | Dock, taskbar, start menu, window chrome |
| Mobile | Touch, portrait | Full-screen apps, no window chrome, touch targets |
| Tablet | Touch, portrait/landscape | Rotatable, larger panels, dock retained |
| TV | Remote, fixed landscape | Large focus targets, fixed orientation, no rotation |

`FLAIROS_SHELL_MOBILE` selects the mobile path at build time
(`CMakeLists.txt` presets, `Makefile` `FLAIROS_SHELL`). Rotation is refused on TV
because there is no rotate affordance on a television.

## Session package

`FlairDesktop.airp` is a real package, built by the normal build and staged at
boot:

| Resource | Type | Content |
| --- | --- | --- |
| `theme.json` | `THEME` | Palette, radius, spacing, border width |
| `launcher.json` | `UI` | Dock order, pinned apps, start menu |
| `strings.json` | `STRINGS` | User-visible labels |

It is a **component package**: `AIRP_FLAG_RESOURCES_ONLY`, no executable. The
session shell implementation is in the kernel, so the package carries the
declarative definition rather than a duplicate binary. This is the concrete
demonstration that a container and an executable are separate things.

Build and staging are real, and the package validates from inside the boot image:

```
dd if=build/textile.img bs=512 skip=2591 count=16 of=/tmp/staged.airp
tools/airpkg.py list /tmp/staged.airp   # 3 resources, checksums valid
```

**Honest status:** on i386 the theme still comes from the built-in
`sp_theme_default` palette. `kernel/ui/desktop_theme.c` implements loading the
package theme, with validation, bounds checking, contrast measurement and
fallback, and it is covered by 24 host checks — but it is not linked into the
i386 image yet because the kernel is at its static-memory ceiling. The package is
staged and validated at boot; it is not yet the source of the palette.

## Input

| Device | Path |
| --- | --- |
| Keyboard | PS/2 IRQ1, scancode set 1 |
| Mouse | PS/2 IRQ12, with click and drag state |
| Touch | `drv_touch`, mapped to pointer events |

`Ctrl+Alt+D` switches between the desktop and the developer console. This is the
primary runtime recovery path, so it must always work, even when an application
is misbehaving.

## Text mode fallback

The developer console is real VGA text mode, not a bitmap-rendered imitation.
`desktop_gfx_shutdown` clears the display, shuts the device down and restores
text mode. That ordering matters: releasing the framebuffer before restoring text
mode leaves the machine with no visible output.

## What is not here

No real window drag-and-drop between applications, no per-window scaling, no
multi-monitor support, no notification centre, and no accessibility features
beyond contrast checking. Applications are launched one at a time through a
shared image slot.
