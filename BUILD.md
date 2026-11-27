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

Widening the corpus from two packages to twelve exposed three interpreter
defects the first two happened not to hit, all fixed here: several instruction
formats take 4-bit register fields (`const/4`, `if-*`, `iget`/`iput`, `/2addr`,
`/lit16`) and were decoded as 8-bit, indexing outside the register file and
segfaulting on NewPipe and Termux; `invoke-*/range` took its argument count from
an 8-bit field without bounding it against the register window; and `invoke-*`
(35c) read its argument count and fifth register from the wrong code unit, so
arguments were passed by luck. Each of those now either executes correctly or
stops with a named diagnostic, and all twelve packages are clean under
`-fsanitize=address`.

### The corpus, measured

Every package below was downloaded from F-Droid (which publishes redistributable
APKs) at the time of measurement and probed with `make apk-probe APK=...`. Each
value is a line the probe printed, not a summary written by hand: `dex` counts
`classesN.dex`, `methods returned` is the number of static methods the
interpreter actually executed to a return, and `caught` counts runs where a real
DEX `encoded_catch_handler` took an exception the code raised (the scanner tries
two argument shapes per candidate method, so this is a sample, not a maximum).

| package | version | sdk | label | launcher | dex | classes | methods w/ code | try blocks | methods returned | caught |
|---|---|---|---|---|---|---|---|---|---|---|
| `com.termux` | 0.118.3 (1002) | 24/28 | Termux | `com.termux.app.TermuxActivity` | 1 | 2,696 | 17,643 | 1,104 | 95 | 1 |
| `org.mozilla.fennec_fdroid` | 160.0.1 (1600120) | 26/37 | Fennec | `org.mozilla.fennec_fdroid.App` | 3 | 41,300 | 147,416 | 10,127 | 333 | 0 |
| `org.fdroid.fdroid` | 1.19.1 (1019051) | 23/28 | F-Droid | `org.fdroid.fdroid.views.main.MainActivity` | 2 | 18,629 | 104,899 | 7,047 | 1,315 | 1 |
| `org.schabi.newpipe` | 0.29.3 (1017) | 23/35 | NewPipe | `org.schabi.newpipe.MainActivity` | 3 | 11,501 | 64,931 | 6,983 | 247 | 0 |
| `org.wikipedia` | r/50619 (50619) | 24/37 | Wikipedia | `org.wikipedia.main.MainActivity` | 3 | 13,155 | 60,875 | 6,963 | 260 | 0 |
| `de.danoeh.antennapod` | 3.13.0 (3130095) | 23/36 | AntennaPod | `de.danoeh.antennapod.activity.SplashActivity` | 1 | 8,427 | 42,792 | 6,073 | 152 | 0 |
| `com.fsck.k9` | 25.0 (39047) | 23/36 | K-9 Mail | `net.thunderbird.app.common.MainActivity` | 2 | 12,531 | 56,861 | 8,321 | 119 | 0 |
| `com.aurora.store` | 4.8.4 (76) | 23/37 | Aurora Store | `com.aurora.store.ComposeActivity` | 2 | 16,451 | 92,427 | 7,734 | 3,453 | 0 |
| `app.organicmaps` | 2026.11.11-39 (26111139) | 30/36 | Organic Maps | `app.organicmaps.DownloadResourcesActivity` | 2 | 7,286 | 41,896 | 2,937 | 335 | 0 |
| `eu.faircode.email` | 1.2341 (2341) | 37/37 | FairEmail | `eu.faircode.email.ActivityMain` | 3 | 20,595 | 112,012 | 15,202 | 592 | 0 |
| `me.zhanghai.android.files` | 1.7.5 (40) | 23/36 | Material Files | `me.zhanghai.android.files.filelist.FileListActivity` | 2 | 7,907 | 38,207 | 7,648 | 44 | 0 |
| `com.keylesspalace.tusky` | 33.1 (145) | 24/36 | Tusky | `com.keylesspalace.tusky.MainActivity` | 1 | 8,094 | 38,110 | 6,244 | 268 | 0 |

All twelve match an independent parser (`androguard` 4.1.4) on package, label and
launcher activity.

The CI job resolves versions from the F-Droid index rather than pinning files, so
it measured a newer F-Droid client than the row above: 2.0.3 (code 2000053, three
DEX files, 124,044 methods with code, 6,983 try blocks, 2,418 methods returned,
0 caught). Its enabled launcher entry is the `org.fdroid.IconActivity`
`activity-alias`, and `androguard` reports that same alias as the main activity,
so the probe's answer is right for that build. Fennec 160.0.1, Termux 0.118.3 and
NewPipe 0.29.3 probe the same on the runner as locally.

Selected runs, verbatim from probe output:

- Termux: `DataUtils.getIntFromString("12x")` throws
  `java/lang/NumberFormatException`, a DEX handler catches it, and the call
  returns 0; `TermuxSharedProperties.getExtraKeysInternalPropertyValueFromValue`
  returns a real string; `ComplexColorCompat.from` allocates an object and writes
  3 fields.
- F-Droid: `FabPosition.toString-impl -> "FabPosition.End"`;
  `DataIndex.toString-impl -> "DataIndex(value=741"`;
  `IPAddress.isMaskValue("12x")` throws `java/lang/NumberFormatException`, caught
  by a DEX handler, returns 0.
- NewPipe: `TestTagKt.toLegacyClassName-V4PA4sw -> "android.widget.NumberPicker"`;
  `IconCompat.createWithResource` allocates an object and writes 10 fields.
- Fennec: `BlendMode.toString-impl -> "SrcOut"` and
  `FocusDirection.toString-impl -> "Enter"` (the argument reaches the method now
  that 35c is decoded correctly); `scaleOut-L8ZKh-E$default` allocates an
  `ExitTransitionImpl`. No caught exception is sampled for Fennec any more: with
  correct argument passing the try/catch candidates it used to hit are no longer
  reached, so the probe reports 0 rather than a stale success.
- Wikipedia: `ResultKt.toString-impl -> "Invalid"`;
  `snap$default` allocates a `SnapSpec`.

### The APK path in CI

The `Real APKs (binary manifest + real bytecode)` job in `.github/workflows/ci.yml`
runs on a clean runner: it builds `build/apk_probe`, resolves the current
`versionCode` of Fennec, F-Droid client, Termux and NewPipe through the F-Droid
index, downloads those four packages (nothing is committed; Fennec alone is
130 MB), prints the full probe report into the job log and the job summary, then
runs `tests/apk_probe_gate_test.py` over that log. The gate fails on
`<unparsed>`, on a missing or unresolved label, on a missing launch activity, on
a failed AXML walk, and on a report without a real string return, object
allocation or thrown-and-caught exception. `--self-test` feeds the gate ten
damaged copies of a known-good report and requires each to be rejected, so the
gate itself is proven able to fail.

On the first run of that job (27 Nov 2026) the runner downloaded Fennec
(129,832,312 bytes), F-Droid (12,774,751), Termux (113,880,067) and NewPipe
(16,980,865), printed every manifest field and every string, object and
exception line into the log, and its gate step passed. That is the difference
between "it worked in my session" and "it works on a clean machine": the
numbers in the log above are the runner's, not this machine's.

### Limits (stated, not implied)

- There is no Activity launch and no Android framework: no `Activity`, no
  `Context`, `Binder`, `View` or `Window`. What runs is individual methods, not
  an app. Wiring a real Activity launch is out of reach today.
- No arrays (`aget`/`aput`, `new-array`, `array-length` trap), no `wide` types
  (long/double), no floats, no `const-class`, no `packed-switch` /
  `sparse-switch`, no JNI, no JIT, no native `.so` loading, and only exact-type
  `instance-of`. Each of those stops the run with a diagnostic instead of
  guessing. That is why some packages show few returned methods, and why ten of
  the twelve packages sample no caught exception at all.
- The host class library is deliberately tiny; a method that calls
  `ConcurrentHashMap`, `Class.forName` or any other framework type stops there.
  This is why the probe reports returned vs trapped counts.
- Chrome is proprietary and not redistributable. It was not obtained and is not
  tested here; no other package is substituted for it and no result is invented
  for it.
- `tools/build-termux.sh` is run in CI on x86_64 Linux, not on a phone.
  On-device Termux remains unverified without hardware.
