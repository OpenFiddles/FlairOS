# AIRP applications

FlairOS applications are distributed as `.airp` containers. The current container
format is version 1 with API level 2.

## Container and API

The 36-byte little-endian header identifies the image base, entry address, image
size, and application name. The payload is a flat i386 application image linked
for `TX_APP_BASE`. The loader accepts older API levels and rejects levels newer
than the kernel supports.

API level 2 retains the AXI window and filesystem calls and adds the session
accent-colour call (`SYS_UI_ACCENT`, syscall 38). Settings uses it to switch
between the default violet and teal accent, and stores the selected value in
`UI.CFG` when persistent storage is available.

## Built-in applications

- **Settings** — desktop accent, graphics/compositor information, and FlairOS
  version information. The accent preference is loaded from and saved to
  `UI.CFG`; LiveCD and RAM-only sessions report that the setting is temporary.
- **Notes** — a small note editor that loads and saves `NOTE.TXT`. It reports
  whether a save is persistent or only held in the current session.

Both are built from `userland/`, packaged by `tools/airpack.py`, and included
in disk, live, and setup images. The bootloader stages them before the kernel
starts the desktop. Their reserved disk ranges are LBA 2543–2574 (Settings) and
2575–2590 (Notes).

## Build and validation

```sh
make build/settings.airp build/notes.airp
python3 tests/airp_apps_test.py build/settings.airp build/notes.airp
```

The boot-layout test also checks that the application staging buffers do not
overlap the physical-memory allocator bitmaps.
