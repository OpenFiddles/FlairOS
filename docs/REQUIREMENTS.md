# FlairOS requirements: memory and I/O

Each requirement has an ID, how it is verified, and an honest status.

- **Boot** = a line the kernel writes to the serial log at every boot. `tests/acceptance.sh` boots the image headless and fails if a line is missing or the log contains `PANIC`.
- **Host** = a unit test built with the host compiler (`make test`).
- **UI** = driven through QEMU with `tools/qemu_ui.py` (absolute pointer, keys, screenshots).
- Status: **Met** (verified), **Partial** (works, verification incomplete), **Open** (not done).

## Memory

| ID | Requirement | Verified by | Status |
|----|-------------|-------------|--------|
| MEM-1 | Usable RAM comes from the BIOS E820 map and is managed in 4 KiB frames. | Boot `pmm: E820 regions`, `pmm: selftest passed`; Host `pmm_host_test` | Met |
| MEM-2 | Ranges in use by the kernel, boot structures and framebuffer can be reserved and are never handed out. | Host `pmm_host_test` (`pmm_reserve_range`) | Met |
| MEM-3 | Paging is enabled with a kernel identity map before any user code runs. | Boot `vm: paging on`, `vm: selftest passed` | Met |
| MEM-4 | Kernel virtual regions have an owner; freeing with the wrong owner fails. | Host `vm_region_host_test` | Met |
| MEM-5 | The kernel heap (`kmalloc`/`kfree`) passes a self-test and exposes live-byte accounting for leak checks. | Boot `kheap: selftest passed` | Met |
| MEM-6 | User apps run with isolated permissions and get a private heap through `SYS_BRK`. | Boot `vm: app permissions isolated`; UI launching Settings and Notes | Partial: isolation is exercised by launching apps, not by a hostile-app test |
| MEM-7 | An app that faults or exhausts memory is stopped without taking the kernel down. | none yet | Open: needs fault-injection test apps (null deref, wild write, `SYS_BRK` past the limit) |
| MEM-8 | Out-of-memory in a driver or UI path returns an error instead of a panic. | none yet | Open: several init paths still call `kernel_panic` by design ("fail fast") |

## I/O

| ID | Requirement | Verified by | Status |
|----|-------------|-------------|--------|
| IO-1 | Drivers and devices register with the I/O manager; names are unique; duplicate registration is rejected. | Boot `io: selftest passed`; Host `io_host_test` | Met |
| IO-2 | Requests use the five major functions (create, close, read, write, device-control) with the documented status codes. | Boot `io: selftest passed` | Met |
| IO-3 | A device that is open cannot be deleted (`IO_STATUS_BUSY`). | Boot `io: selftest passed` | Met |
| IO-4 | Out-of-range reads and writes fail with a status code and never corrupt memory. | Boot `io: selftest passed` | Met |
| IO-5 | Unknown control codes return `IO_STATUS_NOT_SUPPORTED`. | Boot `io: selftest passed` | Met |
| IO-6 | The boot self-test leaves the manager exactly as it found it. | Boot `io: selftest passed` (it checks driver and device counts) | Met |
| IO-7 | Pointer input is absolute (VMware protocol) with PS/2 relative as fallback; a click that is pressed and released between two frames is not lost. | Boot `mouse: vmmouse absolute pointer`; UI `tools/qemu_ui.py` | Met on QEMU; unverified on real hardware |
| IO-8 | The primary ATA disk is detected and usable as the system volume. | Boot `ata: primary disk ready`; Host `ata_identify_host_test` | Met |
| IO-9 | The kernel mirrors its log to COM1 so every failure has a serial trace. | Every Boot line above | Met |
| IO-10 | Networking: NIC driver, IPv4, ICMP echo (`ping`) in the terminal. | Host (net core only) | Partial: RTL8139 driver only. QEMU's default NIC is an e1000, so run with `-nic user,model=rtl8139` until an e1000 driver exists |
| IO-11 | Full network stack: UDP, DHCP, DNS, TCP, HTTP client. | none | Open |
| IO-12 | Asynchronous requests and DMA. | none | Open: the I/O manager is synchronous by design (see `include/io.h`) |

## Graphics and input behaviour

| ID | Requirement | Verified by | Status |
|----|-------------|-------------|--------|
| GFX-1 | A frame is presented once, fully composed. No half-drawn frame reaches the screen. | UI screenshots show no ghosting | Met |
| GFX-2 | The cursor is drawn at its true pixel position, as an original arrow, hand (over clickable items) or I-beam (over the terminal), and `CURSOR.BMP` on the system volume can replace the arrow. | Host `cursor_host_test`; UI screenshots | Met |
| GFX-3 | PNG images can be loaded. | none | Open: only BMP is decoded today (`kernel/gfx/bmp.c`) |

## Running the checks

```sh
make test                     # host unit tests
make acceptance               # boots headless QEMU, checks the Boot lines, fails on PANIC
python3 tools/qemu_ui.py --help   # drive the UI and take screenshots
bash tools/run-x11.sh         # run full-screen in Termux:X11
```
