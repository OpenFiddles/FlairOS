# Issues

- [OPEN] Working tree has many uncommitted modifications (Makefile, drivers/, include/, kernel/, sdk/, tests/) from earlier work. Ownership/intent not verified by Claude. Review before committing.
- [OPEN] Remote command execution via Desktop Commander waits at most 3s; long builds must be run in the background and polled.
- [OPEN] 4K unsupported in practice: only profiles 0-3 exist; QEMU default VRAM (16 MiB) < 4K framebuffer (~31.6 MiB); software rendering at 4K untested.
- [OPEN] No battery source; no Wi-Fi hardware/driver; no real GPU backend (inventory only). "Airnimation" name not present in repo.
