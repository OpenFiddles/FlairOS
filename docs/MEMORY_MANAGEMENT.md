# FlairOS memory-management architecture

Reference model: ReactOS / Windows NT separation of memory responsibilities.
This is an architectural study, not an NT ABI or a promise of Windows compatibility.

## Subsystem map

```text
Boot firmware (E820)
       |
       v
Boot memory handoff
       |
       v
Physical Memory Manager (PMM) ----> frame inventory / reservations
       |                                  |
       v                                  v
Virtual Memory Manager (VMM) <---- HAL page-table and TLB operations
       |
       +---- Kernel address space
       +---- User address spaces (currently one fixed app slot)
       +---- Virtual regions / mappings (planned)
       +---- Kernel heap (kmalloc/kfree)
       +---- Page-fault manager (planned)
       +---- Memory-mapped I/O (planned)

Future consumers:
  Process Manager -> per-process address spaces
  I/O Manager     -> pinned buffers, DMA-safe pages, device mappings
  Cache Manager   -> file-backed cached pages
  AirGraph        -> graphics buffers / mapped framebuffers
```

## Responsibilities and boundaries

| Layer | Owns | Must not own |
|---|---|---|
| PMM | Physical page inventory, free/allocated/reserved frame state | Virtual addresses or process policy |
| VMM | Address spaces, virtual regions, page tables, permissions, faults | Physical RAM discovery |
| Kernel heap | Small dynamic kernel allocations backed by pages | User virtual-address policy |
| Memory objects | Region ownership, backing, access and lifetime metadata | Architecture-specific page-table instructions |
| HAL | CPU-specific page-table operations, TLB invalidation and barriers | General allocation policy |
| Cache manager | File-backed page cache and writeback coordination | Raw physical frame ownership |

## Current implementation (main)

- i386 BIOS is the only complete boot path.
- PMM consumes the E820 handoff and manages physical memory from 1 MiB
  through 256 MiB.
- Availability and managed bitmaps track usable frames; a third bitmap now
  distinguishes permanently reserved frames.
- VMM identity maps the 32-bit address space with supervisor 4 MiB pages and
  splits selected regions into 4 KiB page tables.
- User execution uses one fixed low-memory app/stack window and a bounded heap.
  This is not a general multi-process address-space implementation.
- Kernel heap obtains contiguous frame runs and suballocates blocks; empty
  chunks are not yet returned to PMM.
- AArch64 is a separate bring-up scaffold and does not use this i386 PMM/VMM.

## Implementation sequence

### Phase 1 — physical frame ownership
- Separate free, allocated and reserved states.
- Prevent reserved frames from being returned through the normal free API.
- Expose free, managed and reserved frame counts.
- Extract bitmap/range logic into a host-testable core before expanding the
  physical range.

### Phase 2 — region-based virtual memory
- Introduce explicit address-space and virtual-region structures.
- Add bounded map/unmap operations with ownership and permission checks.
- Keep x86 page-table mechanics behind an architecture interface.
- Add a page-fault dispatcher with separate not-present and protection-fault
  paths; never silently map arbitrary faults.

### Phase 3 — process memory
- Give each process its own address-space object and user/kernel boundary.
- Replace the single fixed app slot with separately mapped image, stack, heap,
  and shared-memory regions.
- Add reference-counted lifetime and teardown before adding concurrency.

### Phase 4 — advanced memory services
- Memory-mapped device regions and DMA pinning.
- File-backed mappings and cache manager.
- Copy-on-write, demand paging, swapping and compression only after storage,
  process isolation and fault handling are reliable.

## Invariants

1. PMM returns only usable, free, non-reserved frames.
2. VMM never assumes virtual equals physical.
3. User mappings cannot grant access to supervisor-only kernel memory.
4. Every mapping has explicit ownership and teardown rules.
5. Device memory is not ordinary allocatable RAM.
6. Architecture-specific page-table instructions stay behind the HAL.
7. Preserve the legacy AIRP v1 and floppy image layouts.

## Validation notes

The current PMM/VMM has boot-time self-tests but no dedicated host-test suite.
The next validation step is to extract frame-bitmap and range transitions into
a host-testable core. Do not expand the 256 MiB cap or claim AArch64 memory
support until boot handoff, page-table backend and allocator tests cover it.
