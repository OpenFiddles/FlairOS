# Getting started

## Repository

The source repository is [OpenFiddles/FlairOS](https://github.com/OpenFiddles/FlairOS).

The product is the 64-bit line: the `desktop`, `mobile` and `tv` shells on
x86_64 and aarch64, built by `tools/build_preset.py`. The i386 BIOS path is the
legacy reference build, kept in `legacy/i386/` and still buildable and bootable.

## Build requirements

The 64-bit build uses clang, LLD, llvm-objcopy, Make and Python 3. The legacy
i386 build additionally needs NASM and gcc with 32-bit support; its media tests
use xorriso and QEMU.

On Ubuntu:

```sh
sudo apt-get update
sudo apt-get install clang lld llvm binutils make python3
# only for the legacy i386 image:
sudo apt-get install nasm gcc-multilib xorriso qemu-system-x86
```

## Build

From the repository root:

```sh
make                    # all six 64-bit preset images
make verify-disk        # boot each preset from its disk image, assert the screen
make legacy             # legacy i386 floppy, LiveCD and Setup boot CD
```

The x86_64 presets produce two media: a Multiboot ELF for `qemu -kernel`, and a
raw disk image (`flairos-<preset>-x86_64.img`) whose boot sector and loader read
the kernel off disk sectors. `make verify-disk` boots the disk image under QEMU,
waits for the kernel's own `render=ok` line and checks the captured framebuffer
dimensions and content.

`make` writes the 64-bit images to `build-presets/<preset>-<arch>/`; `make
legacy` writes the i386 image and optical media to `build/`. See
[Boot Media](BOOT_MEDIA.md) for the distinction between the normal image,
LiveCD, and Setup BootCD.

## Run checks

```sh
make test         # host tests over the shared sources
make legacy-test  # host tests plus the i386 boot, layout and media tests
```

CI gates on the 64-bit preset build and the host tests. The i386 job is manual
and non-blocking.

The scripts under `tools/` build the images, package apps and drive the
emulators; `tools/README.md` says what each one does and what calls it.

## Development boundaries

- Work on the 64-bit path. i386 changes belong in `legacy/i386/` and exist to keep the reference image reproducible.
- Keep architecture-specific page-table and hardware operations behind their interfaces.
- Treat planned features as planned until implementation and tests demonstrate them.
- Preserve existing disk and AIRP format compatibility unless a format change is intentional.
