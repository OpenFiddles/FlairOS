# AirGraph

AirGraph is FlairOS's architecture-neutral graphics API. WinDown will use it for
surface drawing and, later, hardware-accelerated composition.

## Current v1 scope

- 32-bit XRGB8888 and ARGB8888 surfaces.
- Caller-owned pixel buffers; pitch is in bytes and may include row padding.
- Pixel writes, clear, clipped rectangle fill, rectangle outline, alpha blend,
  clipped blit, and framebuffer presentation.
- Optional backend callbacks for fill, blend, blit, and present operations.
- Any operation not supplied or not successfully handled by a backend falls
  back to the CPU renderer.
- Generic driver classes distinguish GPU devices from display devices.

The public structures use fixed-width integer types. They do not store pointers
in 32-bit integers or assume the kernel's pointer width, so the interface can
remain stable when an x86-64 boot layer is added.

## Backend contract

A backend provides struct ag_backend_ops and advertises only the capabilities
it implements. A callback returns zero when it handled the operation; a
non-zero result asks AirGraph to use the software implementation instead.
init also returns zero on success. Backend state belongs in
ag_device.private_data.

The generic driver registry has DRV_CLASS_GPU and DRV_CLASS_DISPLAY.
drv_find_class() returns the first initialized device in the requested class.
A concrete hardware driver can expose its AirGraph backend through this
interface in a later change.

## Current hardware status

The current kernel still initializes the existing Bochs/QEMU linear framebuffer
through vbe_init(). The boot-time color-bar test now draws through AirGraph's
software path. This proves the API is linked into and used by the kernel, but
it is NOT GPU acceleration: no accelerated GPU backend is registered yet.
The CPU path remains the reliable fallback for v86 and older 32-bit machines.

## Build and tests

make builds kernel/airgraph.c. make test runs dedicated host tests for
surface validation, clipping, pitch padding, blending, blitting, presentation,
and generic GPU/display driver discovery before the QEMU smoke tests.


## Adaptive display and resource reporting

AirGraph now exposes adaptive viewport mapping. The caller supplies logical
design sizes for landscape and portrait; orientation is selected from physical
display dimensions. Fit preserves aspect ratio and centers the canvas, Fill
preserves aspect ratio and crops overflow, and Stretch fills the display.
Point and rectangle mapping lets a UI keep its logical layout while targeting
different physical screen sizes, including portrait emulator screens.

The resource query reports the active renderer, advertised backend
capabilities, CPU software fallback availability, and whether an initialized
backend explicitly identifies itself as a GPU backend. The CPU renderer is
always available. This is capability reporting and dispatch infrastructure,
not a claim that a physical GPU driver already exists: the current Bochs/QEMU
linear framebuffer is a display target, not a GPU compute or 3D accelerator.
A real GPU driver must register a backend before GPU availability can report
true. Applications do not receive unrestricted CPU/GPU register access;
hardware access remains behind kernel drivers and validated backend calls.
