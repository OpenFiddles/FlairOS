# Getting started

## Repository

The source repository is [OpenFiddles/FlairOS](https://github.com/OpenFiddles/FlairOS).
The complete BIOS boot path currently documented by the project is i386.

## Build requirements

The main build uses a GNU toolchain, NASM, Make, and Python 3. The CI workflow
also installs QEMU and xorriso for image creation and tests.

On Ubuntu:

```sh
sudo apt-get update
sudo apt-get install nasm gcc-multilib binutils make python3 qemu-system-x86 xorriso
```

## Build boot media

From the repository root:

```sh
make ARCH=i386 boot-media
```

The build produces the legacy disk image and optical boot media under `build/`.
See [Boot Media](BOOT_MEDIA.md) for the distinction between the normal image,
LiveCD, and Setup BootCD.

## Run checks

```sh
make fs-test
make test
```

The GitHub Actions workflow also runs architecture guards, boot-layout and
AIRP boundary tests, and a desktop integration test.

## Development boundaries

- Work on the supported i386 boot path unless a port is explicitly marked experimental.
- Keep architecture-specific page-table and hardware operations behind their interfaces.
- Treat planned features as planned until implementation and tests demonstrate them.
- Preserve existing disk and AIRP format compatibility unless a format change is intentional.
