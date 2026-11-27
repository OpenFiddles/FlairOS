# Sessions, packages and kernel components

Every user-facing **session entry point** in FlairOS is a package: the image
carries a `.airp` component that describes the shell's home screen, dock and
wording, the kernel validates and loads it through `kernel/ui/shell_launcher.c`,
and the shell's own C file only draws what the kernel handed it.

## Packaged sessions

| Preset(s) | Shell source | Package | Root object | Kind |
| --- | --- | --- | --- | --- |
| `presets/desktop`, `presets/desktop4k`, `presets/template` | `ui/shells/shell_desktop.c` | `FlairDesktop.airp` (`desktop/`) | `desktop` | `SHELL_KIND_DESKTOP` |
| `presets/mobile` | `ui/shells/shell_mobile.c` | `FlairMobile.airp` (`mobile/`) | `mobile` | `SHELL_KIND_MOBILE` |
| `presets/tv`, `presets/tv4k` | `ui/shells/shell_tv.c` | `FlairTV.airp` (`tv/`) | `tv` | `SHELL_KIND_TV` |

Each package carries `launcher.json` (title, hint, tiles with id/label/target/
glyph/icon/accent, and the dock order) and `strings.json` (every label the shell
draws). A preset opts in with `"session_package": "<dir>"`; `tools/build_preset.py`
packs that directory with `tools/airpkg.py` and links the bytes into the kernel
image, and `kernel/boot/kmain64.c` validates the package at boot and prints:

```
flairos64: session=pkg-load ok tiles=8 dock=8 strings=39
```

If the package is missing or invalid the line reads `session=pkg-load FAILED
<reason>` and the shell keeps the built-in fallback layout in
`kernel/ui/shell_launcher.c`. That fallback is boot-safety data, not a second
authored layout: every shipped preset loads its package, and
`tools/verify_disk_boot.py` refuses a frame for a preset that declares a package
but never logs the `ok` line.

Layout scale is shared too: `kernel/ui/tv_layout.c` (1280x720 design) and
`kernel/ui/mobile_layout.c` (480x800 design) turn panel size into a Q16 scale.
The desktop session keeps its own window-manager coordinates; its dock, menu bar,
dropdowns, window titles and tooltips come from `FlairDesktop.airp`.

## Kernel components that stay in the kernel

These are shared mechanisms rather than session entry points, so they are not
packaged:

- **AirStinUI** (`ui/systemui/airstin_ui.c`) — the control centre, status
  cluster, power modal and installer prompts. Desktop, TV and mobile all render
  the same drawer through a scale; only its *labels* are resolved per session
  (`airstin_ui_set_labels` on TV).
- **WinDown** (`kernel/ui/windown.c`) — window arrangement and z-order.
- **AirGraph** (`kernel/gfx/airgraph.c`, `glass_fx.c`, `font5x7.c`, `bmp.c`) —
  pixel drawing primitives.
- **AirBrowse** (`ui/shells/flair_browser.c`) — the in-shell web surface the
  packaged launcher tiles can target.
- **ART / SurfaceFlinger / the Android compatibility runtime** — app execution,
  not shell chrome.
- **The i386 reference desktop** (`kernel/ui/desktop.c`, `kernel/ui/mobile.c`,
  `legacy/`) — the retired reference session; it reads `FlairDesktop.airp` for
  its theme only and is not a 64-bit product target.

## Building and booting the image

The 64-bit images need `clang`, `ld.lld`, `llvm-objcopy` and `nasm`; booting
them needs QEMU. In this workspace they live under `~/local/bin`, so:

```sh
export PATH=$HOME/local/bin:$PATH
export LD_LIBRARY_PATH=$HOME/local/usr/lib/x86_64-linux-gnu:$HOME/local/lib/x86_64-linux-gnu

python3 tools/build_preset.py --preset tv --arch x86_64      # one preset
make presets                                                 # desktop, mobile, tv (both arches)
make verify-disk                                             # boots every preset from disk
```

Both commands **fail loudly** if the toolchain is absent: `build_preset.py`
names the missing binary and the packages it needs, and `verify_disk_boot.py`
refuses to report a boot when QEMU is missing or unrunnable instead of quietly
passing. x86_64 boots its raw BIOS disk image under KVM when available; aarch64
boots through AAVMF/EDK2 under TCG on an x86 host (KVM is only used for the
host's own architecture).

`tools/verify_disk_boot.py` requires, per booted preset: the kernel's
`arch=<arch> preset=<preset>` line, the `render=ok WxH` line, a `session=pkg-load
ok` line when the preset declares a package, and a screendump of the preset's
exact size that is neither blank nor single-colour.

The 4K presets build and link, but this QEMU's default VGA cannot return a
3840x2160 screendump (it reports a 32-pixel-wide capture), so the automated boot
gate covers the 1024x768, 480x800 and 1280x720 panels.

`make test` additionally builds all three packages and runs
`tests/session_pkg_host_test`, which loads the shipped `build/FlairMobile.airp`,
`build/FlairTV.airp` and `build/FlairDesktop.airp` through the real loader and
renders the phone home screen from the shipped bytes (not a fixture).
