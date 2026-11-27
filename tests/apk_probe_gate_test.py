#!/usr/bin/env python3
"""Gate the real-APK report produced by build/apk_probe.

The probe prints one block per package (a line starting with "=== "), and the
lines inside a block are the measured values: the parsed binary manifest and
what the Dalvik interpreter actually ran for that package's classesN.dex files.
This gate fails when a required package is missing, when its manifest values
are absent or unparsed, or when the run produced no real bytecode evidence.

    python3 tests/apk_probe_gate_test.py apk-probe.log

The gate's own behaviour is checked by --self-test, which feeds it a good log
and then damaged copies of it and requires the right exit code each time.
"""
import re
import sys

REQUIRED = [
    "org.mozilla.fennec_fdroid",
    "org.fdroid.fdroid",
    "com.termux",
    "org.schabi.newpipe",
]

FIELD_PATTERNS = {
    "version": re.compile(r"^version\s+=\s+(.+)$"),
    "sdk": re.compile(r"^sdk\s+=\s+min\s+(\d+),\s+target\s+(\d+)$"),
    "label": re.compile(r"^label\s+=\s+(.+)$"),
    "launcher": re.compile(r"^launch_activity\s+=\s+(.+)$"),
    "application": re.compile(r"^application\s+=\s+(.+)$"),
    "axml": re.compile(r"^axml_manifest\s+=\s+rc=(-?\d+)\s+elements=(\d+)\s+attributes=(\d+)\s+unknown_chunks=(\d+)$"),
}


def split_blocks(text):
    """Return [(file_label, [lines])] using the probe's '=== ' separators."""
    blocks, current, name = [], [], None
    for line in text.splitlines():
        if line.startswith("=== "):
            if name is not None:
                blocks.append((name, current))
            name = line[4:].strip()
            current = []
        elif name is not None:
            current.append(line)
    if name is not None:
        blocks.append((name, current))
    return blocks


def check(text, required=REQUIRED):
    errors = []
    if "<unparsed>" in text:
        errors.append("report contains <unparsed>: a manifest value was not parsed")
    blocks = split_blocks(text)
    if not blocks:
        errors.append("no '=== <file>' blocks: the probe printed nothing usable")
        return errors

    by_package = {}
    for name, lines in blocks:
        for line in lines:
            m = re.match(r"^package_name\s+=\s+(.+)$", line)
            if m:
                by_package[m.group(1).strip()] = (name, lines)

    for pkg in required:
        if pkg not in by_package:
            errors.append(f"{pkg}: no package_name line in the report")
            continue
        name, lines = by_package[pkg]
        found = {}
        for key, rx in FIELD_PATTERNS.items():
            for line in lines:
                m = rx.match(line)
                if m:
                    found[key] = m.groups()
                    break
            if key not in found:
                errors.append(f"{name}: missing '{key}' line")
        if "version" in found and not found["version"][0].strip():
            errors.append(f"{name}: empty version")
        if "sdk" in found and (int(found["sdk"][0]) <= 0 or int(found["sdk"][1]) <= 0):
            errors.append(f"{name}: non-positive SDK levels")
        if "label" in found:
            label = found["label"][0]
            if not label.strip() or label.startswith("<"):
                errors.append(f"{name}: app label not resolved ({label!r})")
            elif "(unresolved" in label:
                errors.append(f"{name}: app label is an unresolved resource id")
        if "launcher" in found and (not found["launcher"][0].strip()
                                    or found["launcher"][0].startswith("<")):
            errors.append(f"{name}: no launchable activity")
        if "axml" in found and int(found["axml"][0]) != 0:
            errors.append(f"{name}: axml_manifest returned {found['axml'][0]}")
        if "axml" in found and int(found["axml"][1]) == 0:
            errors.append(f"{name}: AXML walk saw no elements")

    if not re.search(r"^\S*\s*string .* -> \"", text, re.M):
        errors.append('no real string return ("string ... -> \\"...\\"") in the report')
    if not re.search(r"^\S*\s*object .*\[object of ", text, re.M):
        errors.append("no object allocation ('object ... [object of ...]') in the report")
    if not re.search(r"^\S*\s*exception .*threw .*caught by DEX handler", text, re.M):
        errors.append("no thrown-and-caught exception in the report")
    if not re.search(r"^summary\s+=", text, re.M):
        errors.append("missing summary line")
    return errors


GOOD = """=== apks/com.termux.apk
package_name    = com.termux
version         = 0.118.3 (code 1002)
sdk             = min 24, target 28
label           = Termux (from resources.arsc)
launch_activity = com.termux.app.TermuxActivity
application     = com.termux.app.TermuxApplication
axml_manifest   = rc=0 elements=72 attributes=130 unknown_chunks=0
classes.dex       dalvik scan: static_no_arg returned=95 trapped=92, static(String) tried=13 returned=1
classes.dex       string Lcom/termux/shared/file/TermuxFileUtils;->x -> "/data/data/com.termux/files" (7 insns)
classes.dex       object Lorg/commonmark/parser/block/BlockContinue;->atColumn [object of Lorg/commonmark/parser/block/BlockContinue; with 3 fields set] (12 insns)
classes.dex       exception Lcom/termux/shared/data/DataUtils;->getIntFromString("12x") -> threw java/lang/NumberFormatException, caught by DEX handler, returned 0
summary         = 1 readable dex; 1 dex ran bytecode, 95 methods returned, 1 thrown-and-caught exceptions; 0 reached the legacy dex_run_buffer entry
=== apks/org.schabi.newpipe.apk
package_name    = org.schabi.newpipe
version         = 0.29.3 (code 1017)
sdk             = min 23, target 35
label           = NewPipe (from resources.arsc)
launch_activity = org.schabi.newpipe.MainActivity
application     = org.schabi.newpipe.App
axml_manifest   = rc=0 elements=253 attributes=355 unknown_chunks=0
=== apks/fennec.apk
package_name    = org.mozilla.fennec_fdroid
version         = 160.0.1 (code 1600120)
sdk             = min 26, target 37
label           = Fennec (from resources.arsc)
launch_activity = org.mozilla.fennec_fdroid.App
application     = org.mozilla.fenix.FenixApplication
axml_manifest   = rc=0 elements=559 attributes=1278 unknown_chunks=0
=== apks/fdroid.apk
package_name    = org.fdroid.fdroid
version         = 1.19.1 (code 1019051)
sdk             = min 23, target 28
label           = F-Droid (from resources.arsc)
launch_activity = org.fdroid.fdroid.views.main.MainActivity
application     = org.fdroid.fdroid.FDroidApp
axml_manifest   = rc=0 elements=311 attributes=488 unknown_chunks=0
"""


def self_test():
    failures = []
    if check(GOOD):
        failures.append("good log rejected: " + "; ".join(check(GOOD)))
    damaged = [
        ("unparsed package", GOOD.replace("package_name    = com.termux", "package_name    = <unparsed>")),
        ("missing label", GOOD.replace("label           = Termux (from resources.arsc)\n", "")),
        ("unresolved label", GOOD.replace("label           = Termux (from resources.arsc)",
                                          "label           = <none> (unresolved @string)")),
        ("no launcher", GOOD.replace("launch_activity = org.schabi.newpipe.MainActivity",
                                     "launch_activity = <none>")),
        ("bad axml", GOOD.replace("axml_manifest   = rc=0 elements=72 attributes=130 unknown_chunks=0",
                                  "axml_manifest   = rc=-2 elements=0 attributes=0 unknown_chunks=0")),
        ("no string run", "\n".join(l for l in GOOD.splitlines() if "-> \"" not in l)),
        ("no object run", "\n".join(l for l in GOOD.splitlines() if "[object of " not in l)),
        ("no caught exception", "\n".join(l for l in GOOD.splitlines() if "caught by DEX handler" not in l)),
        ("missing package", GOOD.replace("package_name    = org.schabi.newpipe\n", "")),
        ("empty log", ""),
    ]
    for name, text in damaged:
        if not check(text):
            failures.append(f"damaged log accepted: {name}")
    if failures:
        for f in failures:
            print("FAIL:", f)
        return 1
    print(f"apk probe gate: good log accepted, all {len(damaged)} damaged logs rejected")
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "--self-test":
        return self_test()
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[0])
        print("usage: apk_probe_gate_test.py <apk-probe.log> | --self-test", file=sys.stderr)
        return 2
    try:
        with open(argv[1], "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    errors = check(text)
    for e in errors:
        print("FAIL:", e)
    if errors:
        return 1
    print("apk probe gate: manifest values and real bytecode runs verified for "
          + ", ".join(REQUIRED))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
