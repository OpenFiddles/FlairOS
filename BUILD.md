# Building FlareOS

A small x86 hobby kernel: a 512-byte boot sector loads a 64-sector kernel, switches to 32-bit protected mode, and runs a shell.

- Hardware it talks to: VGA text (0xB8000 and cursor ports), PS/2 keyboard (IRQ1), PIT timer (IRQ0), PIC, CMOS clock (ports 0x70/0x71), RTL8139 network card (PCI config and I/O ports, detection only for now), COM1 serial (mirrors the screen for tests), and any I/O port through `in` and `out`.
- Shell commands: `help`, `clear`, `echo`, `time`, `uptime`, `in PORT`, `out PORT VALUE`, `reboot`, `net`, `sqr X Y`.
- Boot shows a short Saturn splash with a pulsing background before the shell starts.

## Build and run

The product is the 64-bit line: desktop, mobile and TV shells on x86_64 and
aarch64. It needs clang, ld.lld, llvm-objcopy and python3.

    make                      # builds all six 64-bit preset images
    make verify-disk          # boots each x86_64 disk image, checks the screen
    ./flairos-start.sh verify # boots the -kernel images headless
    make test                 # host tests over the shared kernel sources

The i386 image is the legacy reference build. It is kept bootable but is no
longer built by default; it needs nasm, gcc with 32-bit support (gcc-multilib on
Debian/Ubuntu), ld, objcopy, xorriso and qemu-system-i386.

    make legacy      # builds build/textile.img, the LiveCD and the Setup boot CD
    make legacy-test # host tests plus the i386 boot, layout and media tests
    make run         # boots the i386 image in QEMU, serial output in the terminal

## Building on the phone, in Termux

The aarch64 target builds on the device itself: Termux's clang emits aarch64
freestanding code, so no cross toolchain is involved.

    pkg install cmake ninja clang binutils
    tools/build-termux.sh              # -> build-arm64/flairos-arm64.bin
    LEGACY=1 tools/build-termux.sh     # also build-termux-i386/textile.img

The script is the same CMake + Ninja build CI uses (`cmake --preset
arm64-phone`); it only adds the dependency checks. Verified here on an x86_64
Linux host with clang 18; the Termux run itself has not been done on a phone.

## APK support: what is real today

`make apk-probe APK=path/to/app.apk` runs a real package through the same code
the kernel runs (the ZIP walk in `kernel/pkg/apk.c`, the raw-DEFLATE decoder in
`ported/developer/libs/flair_zlib`, the DEX reader in `kernel/pkg/dex.c`) plus
two host-side pieces: the binary-manifest reader in `kernel/pkg/apk_manifest.c`
and the Dalvik interpreter in `runtime/art/dalvik_vm.c`.

### The binary manifest (AXML + resources.arsc)

Real packages ship `AndroidManifest.xml` as compiled AXML (chunked, string pool,
resource-id map), not text, and it is DEFLATE-compressed inside the ZIP. The
reader inflates it into a caller-supplied scratch buffer, walks every chunk
(unknown chunk types are counted and skipped), resolves `android:` attribute
names through the resource map, and resolves `android:label` through
`resources.arsc`, following resource-to-resource references. Launcher aliases
are kept only when `android:enabled` is not `false`, which is what a real icon
picker needs.

Measured on Fennec 160.0.1 (`org.mozilla.fennec_fdroid_1600120.apk`, 129,832,312
bytes) and the F-Droid client 1.19.1 (`org.fdroid.fdroid_1019051.apk`, 12,930,505
bytes):

| | Fennec | F-Droid |
|---|---|---|
| package | `org.mozilla.fennec_fdroid` | `org.fdroid.fdroid` |
| version | 160.0.1 (code 1600120) | 1.19.1 (code 1019051) |
| SDK | min 26, target 37 | min 23, target 28 |
| label | `Fennec` (via `resources.arsc`) | `F-Droid` (via a resource reference chain) |
| launcher | `org.mozilla.fennec_fdroid.App` | `org.fdroid.fdroid.views.main.MainActivity` |
| application | `org.mozilla.fenix.FenixApplication` | `org.fdroid.fdroid.FDroidApp` |
| AXML walk | 559 elements, 1278 attributes, 0 unknown chunks | 311 elements, 488 attributes, 0 unknown chunks |
| native ABIs | arm64-v8a: 15 `lib/*.so` | none |

Both manifests were cross-checked against an independent parser (`androguard`
4.1.4): package, version, SDK, label and launcher activity agree. Fennec
declares 24 `MAIN`/`LAUNCHER` aliases, one per icon theme, 23 of them
`android:enabled="false"`; only the enabled one is reported. F-Droid's
`panic.CalculatorActivity` is a disguise launcher that is disabled in the
shipped manifest, so it is filtered out too.

### Running real bytecode

`runtime/art/dalvik_vm.c` decodes the real DEX tables (strings, types, protos,
methods, `class_data`, `code_item`, `try_item` / `encoded_catch_handler_list`)
and interprets instructions: moves, `const`/`const-string`, arithmetic (including
the `/2addr`, `/lit16` and `/lit8` forms), comparisons and branches,
`iget`/`iput`, `sget`/`sput`, `new-instance` (a real heap object with field
slots), `check-cast`, `instance-of`, `invoke-*` (both 35c and 3rc) with
interpreted calls into methods of the same DEX, `return*`, `move-result*`,
`move-exception`, and `throw` with real handler lookup. Calls into `java.lang`
land in a small host library (`Object`, `String`, `StringBuilder`, `Integer`,
`Math`); calls to anything else stop the run with a diagnostic naming the
missing method, and unimplemented opcodes stop it naming the opcode and pc.
Nothing is invented silently.

What the probe reports per dex, measured on both packages:

- Fennec `classes.dex`: 15,363 classes, 54,646 methods with code, 5,372 try
  blocks; 158 static no-argument methods returned a value; string results such
  as `FocusDirection.toString-impl -> "Invalid FocusDirection"` and
  `BlendMode.toString-impl -> "Unknown"`;
  `BasicMarqueeKt.basicMarquee-1Mj1MLw$default` allocates an
  `androidx.compose.ui.Modifier` object and writes 4 fields into it; and a real
  caught exception:
  `Landroidx/media3/extractor/text/ssa/SsaStyle;->parseBooleanValue("12x")`
  throws `java/lang/NullPointerException`, a DEX `encoded_catch_handler` takes
  it, and the call returns 0.
- F-Droid `classes.dex`: 9,138 classes, 50,791 methods with code, 3,264 try
  blocks; 827 static no-argument methods returned; `FabPosition.toString-impl ->
  "FabPosition.End"`; `ComplexColorCompat.from` allocates an object with 3
  fields set. `classes2.dex`: 486 returned, and
  `Lorg/bouncycastle/util/IPAddress;->isMaskValue("12x")` throws
  `java/lang/NumberFormatException`, is caught by the DEX handler, and returns 0.

### Limits (stated, not implied)

- There is no Activity launch and no Android framework: no `Activity`, no
  `Context`, `Binder`, `View` or `Window`. What runs is individual methods, not
  an app. Wiring a real Activity launch is out of reach today.
- No arrays (`aget`/`aput`, `new-array`, `array-length` trap), no `wide` types
  (long/double), no floats, no `const-class`, no `packed-switch` /
  `sparse-switch`, no JNI, no JIT, no native `.so` loading, and only exact-type
  `instance-of`. Each of those stops the run with a diagnostic instead of
  guessing.
- The host class library is deliberately tiny; a method that calls
  `ConcurrentHashMap`, `Class.forName` or any other framework type stops there.
  This is why the probe reports returned vs trapped counts.
- Chrome is proprietary and not redistributable, so no Chrome APK is included
  or tested here; nothing in this repo substitutes another package for it.
- `tools/build-termux.sh` is run here on x86_64 Linux, not on a phone.
  On-device Termux remains unverified without hardware.
