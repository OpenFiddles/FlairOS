# Application runtime

FlairOS has four distinct execution paths, and it is important not to confuse
them. Only the first executes untrusted application code on i386 today.

| Path | Status | What it does |
| --- | --- | --- |
| Native AIRP apps | **Working** | Flat i386 image in ring 3 with syscalls |
| ELF loader | Working (32-bit) | Loads ELF executables into an address space |
| APK/DEX analysis | **Analysis only** | Recognises and inspects Android packages |
| ART-style runtime | Host-test only | 64-bit sandbox, no bootable target |

## Native AIRP applications

A native application is a flat binary linked for `0x40000` whose first two words
are:

| Offset | Content |
| --- | --- |
| 0 | `TX_APP_MAGIC` (`0x54505041`) |
| 4 | Absolute address of the entry function |

Syscalls are `int 0x80` with the number in `EAX` and arguments in `EBX`, `ECX`,
`EDX`. `include/axi.h` wraps this as `ax_call` plus typed helpers.

### Launch sequence

1. `airp_open` validates the container, including checksums.
2. `airp_load` copies the payload to `0x40000` and clears the rest of the slot.
3. `vm_run_app` builds a private address space and copies frames.
4. Execution enters ring 3 with a stack at `0x7F000`.

Nothing is copied before validation succeeds, and the slot is cleared after the
image so `.bss` starts zeroed on every launch.

### Why the slot is shared

One image slot means one application image at a time. It keeps loading simple
and avoids relocation. The cost is that the desktop must reload a package before
each launch, and that the developer console's `run` command needed a private
snapshot of the boot app to survive the desktop having used the slot.

## AIRP container

The format is specified in [../airp/FORMAT.md](../airp/FORMAT.md). The parts that
matter to a runtime:

- The container is separate from the executable inside it, and a container may
  hold none.
- The header and payload are checksummed, and each resource is checksummed
  separately.
- The architecture is recorded, so a loader can refuse foreign code instead of
  attempting it.
- Version 1 packages still load; the loader reports them as version 1 rather
  than inventing integrity data they do not carry.

## ELF

`kernel/pkg/elf.c` and `include/elf.h` parse and load 32-bit ELF executables into a
process address space: header validation, program headers, segment mapping. This
is used for tooling and tests, not for the desktop applications, which use AIRP.

There is no 64-bit ELF loader on any bootable target.

## APK and DEX: research, staged honestly

Android packages are recognised and analysed, never executed. The staged roadmap
is deliberate, and each stage is a separate claim:

| Stage | Status | Meaning |
| --- | --- | --- |
| 1. File recognition | **Done** | ZIP magic and structure identified, PKG-1 sample format |
| 2. Metadata | **Done** | Manifest fields, package name, version, permissions parsed |
| 3. Analysis | **Done** | `classes.dex` located, header validated, structure inspected |
| 4. Runtime integration | **Partial** | DEX bytecode interpreter (`kernel/pkg/dex.c`) verified on a 5-instruction program |
| 5. Execution | **Not claimed** | No Android application has been executed |

`kernel/pkg/dex.c` executes a small, defined subset of Dalvik bytecode. It is a
verified interpreter, not an Android runtime: no `android.*` framework, no
resources, no native libraries.

**Do not describe FlairOS as running Android applications.** It recognises and
analyses APK files and can interpret a subset of DEX instructions.

## 64-bit runtime and desktop

`runtime/art/`, `ui/shells/`, `kernel/boot/kmain64.c` and `kernel/pkg/elf64.c` contain a
64-bit application runtime with surface flinger, sandbox and a desktop shell.
This code compiles and is exercised by the `runtime64_airp_test` host test. It
has **no bootable target**: `CMakeLists.txt` rejects `FLAIROS_ARCH=x86_64`.

Current host-test state: the suite runs and reports one failure in a Python
script test (`flair_py_run_script` with `itertools`), unrelated to the AIRP or
desktop paths.

This is the code the 64-bit direction builds on. The shell renders into an
`ag_surface` and contains no x86-specific code, so the missing piece is a boot
path and memory management underneath it, not a rewrite of the desktop.

## MicroPython

`ports/micropython` builds a separate AIRP package staged at LBA 2623 and loaded
only if present. It is experimental. The boot path probes for it and continues
normally when absent.
