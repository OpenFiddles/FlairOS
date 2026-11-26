# Plan: Installing & Running Android Apps (.apk) on FlairOS

## Current Foundation Already in FlairOS
- **Task & Memory**: Preemptive scheduler (`kernel/core/task.c`), ELF32 loader (`kernel/pkg/elf.c`), `SYS_MMAP`/`SYS_MUNMAP` (`drivers/vm.c`), `SYS_SET_TLS` (`kernel/core/kernel.c`).
- **Filesystem & UI**: Hierarchical VFS + `fd` `0`/`1`/`2` (`kernel/fs/vfs.c`), `WinDown` pixel-surface compositor + event queues (`kernel/ui/windown.c`), Phone Portrait (`make phone`, `600x750`) + `Touch Mode`.
- **POSIX / `libc` Bridge**: `sdk/flair_posix.h` and `sdk/flair_libc_stubs.c`.

## 5-Stage Roadmap to Install & Run `.apk` Apps
1. **APK Package Unpacker (`install-apk`)**:
   - Add a minimal `zlib`/DEFLATE reader + ZIP parser in userland/kernel to extract `AndroidManifest.xml`, `classes.dex`, `resources.arsc`, and `lib/x86/*.so` into `/data/app/<package>/` on a larger FAT32/ext2 disk image (512 MB+).
2. **Dynamic Linker (`ld-android.so`) & Bionic `libc` Compatibility**:
   - Extend `kernel/pkg/elf.c` to support `ET_DYN` shared libraries, GOT/PLT relocations, and `dlopen`/`dlsym`.
   - Implement Bionic `libc` symbols (`__errno`, `futex`, `clone`, `prctl`, `epoll_*`, `ashmem`).
3. **Android Kernel IPC & Memory Subsystems**:
   - Add `SYS_FUTEX` and `SYS_CLONE` (thread creation sharing `CR3`) in `kernel/core/task.c`.
   - Add **Binder IPC** (`/dev/binder`) and **ashmem** (shared anonymous memory via `vm_mmap`).
4. **Dalvik / ART DEX Runtime**:
   - Port a lightweight DEX interpreter/VM (or AOT DEX-to-ELF compiler) to execute `classes.dex` bytecode, backed by minimal `android.app.Activity`, `android.view.View`, `android.graphics.Canvas`, and `MotionEvent` Java classes.
5. **SurfaceFlinger → `WinDown` Bridge**:
   - Map each Android `ANativeWindow` / `Surface` buffer directly to a `WinDown` `ag_surface *client_surface` (`kernel/ui/windown.c`), and forward Touch/VNC pointer events via `wd_post_event()`.
