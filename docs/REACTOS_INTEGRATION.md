# ReactOS-inspired architecture work in FlairOS

**Status:** Phase 1 I/O object model implemented; broader subsystem work remains planned.

## Licensing and implementation boundary

The repository's current top-level `LICENSE` is GNU Affero General Public License
version 3 (AGPL-3.0), not plain GPL-3.0. ReactOS contains files under multiple
licenses; its repository metadata reports GPL-2.0 and additional licenses.
A file's license must be checked individually before source reuse. This first
FlairOS implementation is independently authored from the documented
architecture model and does not copy ReactOS source.

## Architectural study map

ReactOS is useful as a reference for separating kernel responsibilities into
well-defined subsystems. FlairOS should adopt the separation of concerns, not
pretend to be binary-compatible with NT.

| ReactOS concept | FlairOS direction | Current status |
| --- | --- | --- |
| HAL | Architecture-specific hardware boundary | i386 hardware access still has direct port I/O; AArch64 bring-up is separate |
| Kernel / Executive | Core kernel services and resource ownership | Early kernel; no general scheduler yet |
| Memory Manager | PMM + VM + future virtual-region manager | PMM and paging exist; fixed address-space model remains |
| Object Manager | Named kernel objects and references | Not implemented |
| I/O Manager | Driver objects, device objects, request dispatch | First synchronous model implemented in `include/io.h` and `drivers/io_manager.c` |
| Process Manager | Process and thread lifecycle | One bounded user app context; no general process scheduler |
| Configuration Manager | Persistent system configuration | Not implemented as a registry-like service |
| Win32 subsystem / Win32k | Not an API target; use native FlairOS APIs | Keep AirGraph and WinDown responsibilities separate |
| Explorer shell | Native FlairOS desktop shell | Existing `userland/explorer.c` and desktop code are early-stage |

## Phase 1: I/O manager foundation

The initial model introduces:

- `io_driver`: registered dispatch table for major request types.
- `io_device`: named device instance associated with a driver and extension.
- `io_request`: request packet containing major function, buffer, length,
  offset, control code, completion status and transferred-byte count.
- Fixed-size registries to avoid requiring a working heap during early boot.
- Synchronous CREATE, CLOSE, READ, WRITE and DEVICE_CONTROL dispatch.
- Open counts that prevent device deletion while it is open.

The API is deliberately not an NT ABI. It has no asynchronous completion,
cancellation, queuing, power/PnP state machine, IRQL model, or locking for SMP.
Those features should only arrive with scheduler, synchronization, and
lifetime-management support.

### Host validation

`make test` now builds and runs `io_host_test`, covering driver/device
registration, duplicate rejection, lookup, open/close accounting, request
dispatch, transfer accounting, unsupported major functions, and safe deletion.

## Next implementation order

1. Adapt one low-risk existing device (serial console or RAM-backed test device)
   to the new I/O request path, then migrate ATA without breaking the existing
   filesystem format.
2. Replace the current PMM's fixed 1 MiB–256 MiB range with validated E820
   region descriptors and reservation tracking; retain allocator host tests.
3. Evolve VM into explicit address-space and region objects with bounded
   mapping/unmapping and per-process ownership.
4. Add process/thread objects and a scheduler before introducing asynchronous
   I/O or APC/DPC-like mechanisms.
5. Add a small object namespace and reference-counted object lifetime rules.
6. Split x86 port I/O and interrupt-controller specifics behind a real HAL
   interface before claiming cross-architecture driver portability.
7. Implement WinDown as a separate window/surface compositor; AirGraph remains
   the renderer and graphics-resource interface.

## Validation status

The latest inspected GitHub Actions run built and linked the i386 image and
passed the existing host tests, but the QEMU smoke test timed out waiting for
the serial marker `ui: text mode`. The CMake/Ninja job hit the same runtime
smoke timeout after successful compile/link. The experimental MicroPython
package exceeded the AIRP v1 128 KiB limit. These are existing baseline issues,
not failures introduced by the I/O manager; rerun Actions after this commit.
