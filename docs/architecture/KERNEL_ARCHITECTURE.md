# Kernel architecture

The kernel is a single-address-space freestanding C program linked at `0x10000`.
It runs privileged code and user applications in ring 3.

## Entry and initialisation

`kernel/kernel.c` `kmain` runs a fixed sequence. Early steps cannot be reordered
because later ones depend on memory the earlier ones establish:

| Step | What it does | Why here |
| --- | --- | --- |
| Serial | COM1 configured | Everything after can log |
| GDT/IDT | Descriptor tables | Before any fault may be taken |
| PMM | Physical frames from E820 | Nothing allocates before this |
| VM | Paging, identity map | Heap and kernel mappings need it |
| Heap | `kmalloc`/`kfree` | Driver registration allocates |
| Boot app snapshot | Copies the boot app aside | Before the desktop reuses the app slot |
| Drivers | Register and initialise | Needs the heap |
| Boot mode | Resolve and log | Needs `boot.c` only |
| MicroPython | Optional staging probe | Independent of the session |
| Interrupts | Enable | After handlers are installed |
| Tasks | Scheduler self-test | Needs interrupts for preemption |
| VFS | Root filesystem | Needs drivers and heap |
| Storage | Persistent FAT32 or RAM mode | Needs ATA result |
| IPC | ashmem and binder | Needs heap |
| Display | `desktop_gfx_init` | Needs VFS for `UI.CFG` |
| Session | Desktop or mobile shell loop | Last |

The boot app snapshot is required: stage 1 loads the boot app at `0x40000`, but
the desktop launches Settings, Notes, Explorer and Terminal through that same
slot. Without a snapshot, `run` would execute whichever application happened to
be there last.

## Interrupts and syscalls

Interrupts are handled in `kernel/isr.asm` with C dispatch. Applications call
`int 0x80` with the syscall number in `EAX` and arguments in `EBX`, `ECX`, `EDX`;
the dispatcher is `syscall_dispatch` in `kernel/kernel.c`.

Syscall groups:

| Range | Group | Examples |
| --- | --- | --- |
| 1–13 | Console and input | `PUTC`, `PUTAT`, `GETKEY`, `CURSOR` |
| 14–20 | AXI window API | `WIN_OPEN`, `WIN_TEXT`, `WIN_PRESENT` |
| 21–39 | Session and UI | `UI_ACCENT`, `UI_DISPLAY`, `UI_TOUCH` |
| 41–46 | Process and memory | `GETPID`, `MMAP`, `MUNMAP` |
| 47–56 | Filesystem | `VFS_OPEN`, `VFS_READ`, `VFS_UNLINK` |
| 57–58 | Network | `NET_PING`, `NET_INFO` |
| 62–67 | POSIX-ish | `SET_TLS`, `NANOSLEEP`, `LSEEK`, futex |
| 68–73 | IPC and UI | `ASHMEM_*`, `BINDER_*`, `UI_STATUS` |
| 74+ | Packages | `APK_LAUNCH`, `APK_INSTALL` |

`include/axi.h` is the authoritative list. A syscall with an unknown number
returns `AX_FAIL` rather than faulting.

## Application isolation

`drivers/vm.c` builds a private address space per application: a page directory
copying the kernel's first entry, per-process page tables, and copied frames for
the app image. Applications run in ring 3 and cannot read kernel memory.

The app image slot is fixed at `0x40000`. This is a deliberate simplicity
trade-off: one application image at a time, no relocation, no ASLR.

## Failure policy

| Situation | Behaviour |
| --- | --- |
| Kernel invariant broken | `kernel_panic`, halts with a serial message |
| Layer 3 self-test fails | `kernel_panic` during boot |
| Missing hardware | Driver reports unavailable, boot continues |
| Application faults | Application terminated, kernel continues |
| Compositor call with no display | Returns `AX_FAIL`, logs, does not panic |
| Corrupt package | Loader returns an `AIRP_LOAD_*` code, nothing executes |

The compositor rule exists because it was violated: `axk_present()` used to call
`kernel_panic` when AirGraph was inactive, so any application calling a window
function from the developer console took down the machine. A user-facing
operation returning an error is always preferable to halting.

## Scheduler

`kernel/task.c` provides a round-robin preemptive scheduler with per-task context
switching, `SYS_YIELD`, `SYS_GETPID` and `SYS_TASK_COUNT`. It self-tests during
boot and panics if the self-test fails, because a broken scheduler makes every
later failure untrustworthy.

## What is not here

No SMP. No kernel preemption boundaries. No demand paging, swap or copy-on-write.
No capabilities model beyond ring separation and per-process address spaces.
Applications are cooperative in practice because the UI is a single-threaded
render loop.
