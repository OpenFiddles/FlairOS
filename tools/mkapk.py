#!/usr/bin/env python3
"""Generate stored-entry Android .apk examples with Dalvik classes.dex
   and optional C header bundle for FlairOS VFS /apps/ seeding.
"""
import os, struct, sys, zlib

def uleb128(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)

def build_dex(strings, method_names, insns):
    # method_names: list of indices into `strings` (last one is onCreate)
    hdr_sz = 112
    str_ids_off = hdr_sz
    type_ids_off = str_ids_off + len(strings) * 4
    meth_ids_off = type_ids_off + 4
    cls_defs_off = meth_ids_off + len(method_names) * 8
    data_off = cls_defs_off + 32

    data = bytearray()
    str_offs = []
    for s in strings:
        str_offs.append(data_off + len(data))
        data.extend(uleb128(len(s)))
        data.extend(s)
        data.append(0)
    while len(data) % 4:
        data.append(0)

    code_off = data_off + len(data)
    data.extend(struct.pack("<HHHHII", 4, 0, 1, 0, 0, len(insns)))
    for w in insns:
        data.extend(struct.pack("<H", w))
    while len(data) % 4:
        data.append(0)

    class_data_off = data_off + len(data)
    oncreate_idx = len(method_names) - 1
    data.extend(uleb128(0) + uleb128(0) + uleb128(0) + uleb128(1))
    data.extend(uleb128(oncreate_idx) + uleb128(1) + uleb128(code_off))

    file_size = data_off + len(data)
    out = bytearray(file_size)
    for i, off in enumerate(str_offs):
        struct.pack_into("<I", out, str_ids_off + i * 4, off)
    type_idx = strings.index(b"LMainActivity;")
    struct.pack_into("<I", out, type_ids_off, type_idx)
    for i, nidx in enumerate(method_names):
        struct.pack_into("<HHI", out, meth_ids_off + i * 8, 0, 0, nidx)
    struct.pack_into("<IIIIIIII", out, cls_defs_off, 0, 1, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0, class_data_off, 0)
    out[data_off:] = data

    out[0:8] = b"dex\n035\0"
    struct.pack_into("<II", out, 32, file_size, hdr_sz)
    struct.pack_into("<I", out, 40, 0x12345678)
    struct.pack_into("<II", out, 56, len(strings), str_ids_off)
    struct.pack_into("<II", out, 64, 1, type_ids_off)
    struct.pack_into("<II", out, 88, len(method_names), meth_ids_off)
    struct.pack_into("<II", out, 96, 1, cls_defs_off)
    struct.pack_into("<II", out, 104, len(data), data_off)
    return bytes(out)

def build_hello_dex():
    strings = [
        b"Hello World",                            # 0
        b"User Test App - WinDown Compositor",     # 1
        b"Hello, World! Welcome to FlairOS.",      # 2
        b"Package: com.flairos.helloworld",        # 3
        b"Dalvik + Binder + Ashmem Active",        # 4
        b"Say Hello",                              # 5
        b"Toast: Hello World Ready",               # 6
        b"LMainActivity;",                         # 7
        b"setTitle",                               # 8 (method 0)
        b"setSubtitle",                            # 9 (method 1)
        b"setText",                                # 10 (method 2)
        b"addButton",                              # 11 (method 3)
        b"showToast",                              # 12 (method 4)
        b"onCreate",                               # 13 (method 5)
    ]
    insns = [
        0x001A, 0x0000,  0x106E, 0x0000, 0x0000,  # setTitle(0)
        0x001A, 0x0001,  0x106E, 0x0001, 0x0000,  # setSubtitle(1)
        0x001A, 0x0002,  0x106E, 0x0002, 0x0000,  # setText(2)
        0x001A, 0x0003,  0x106E, 0x0002, 0x0000,  # setText(3)
        0x001A, 0x0004,  0x106E, 0x0002, 0x0000,  # setText(4)
        0x001A, 0x0005,  0x106E, 0x0003, 0x0000,  # addButton(5)
        0x001A, 0x0006,  0x106E, 0x0004, 0x0000,  # showToast(6)
        0x0012, 0x000F,                           # const/4 v0, 0; return v0
    ]
    return build_dex(strings, [8, 9, 10, 11, 12, 13], insns)

def build_calc_dex():
    strings = [
        b"DroidCalc",                              # 0
        b"User Test App - Dalvik Math",            # 1
        b"Computing 6 x 7 in Dalvik DEX:",         # 2
        b"Recompute",                              # 3
        b"Toast: Result 42 via DEX",               # 4
        b"LMainActivity;",                         # 5
        b"setTitle",                               # 6 (method 0)
        b"setSubtitle",                            # 7 (method 1)
        b"setText",                                # 8 (method 2)
        b"printlnInt",                             # 9 (method 3)
        b"addButton",                              # 10 (method 4)
        b"showToast",                              # 11 (method 5)
        b"onCreate",                               # 12 (method 6)
    ]
    insns = [
        0x001A, 0x0000,  0x106E, 0x0000, 0x0000,  # setTitle(0)
        0x001A, 0x0001,  0x106E, 0x0001, 0x0000,  # setSubtitle(1)
        0x001A, 0x0002,  0x106E, 0x0002, 0x0000,  # setText(2)
        0x6112,                                   # const/4 v1, 6
        0x7212,                                   # const/4 v2, 7
        0x21B2,                                   # mul-int/2addr v1, v2  (v1 = 42)
        0x106E, 0x0003, 0x0001,                   # printlnInt(v1) -> "42"
        0x001A, 0x0003,  0x106E, 0x0004, 0x0000,  # addButton(3)
        0x001A, 0x0004,  0x106E, 0x0005, 0x0000,  # showToast(4)
        0x0012, 0x000F,
    ]
    return build_dex(strings, [6, 7, 8, 9, 10, 11, 12], insns)

def build_sysinfo_dex():
    strings = [
        b"Device Info",                            # 0
        b"User Test App - FlairOS HAL",            # 1
        b"Runtime: FlairOS Dalvik 035",            # 2
        b"Compositor: WinDown + AirGraph",         # 3
        b"IPC: Binder + Ashmem Ready",             # 4
        b"Refresh",                                # 5
        b"Toast: System Healthy",                  # 6
        b"LMainActivity;",                         # 7
        b"setTitle",                               # 8 (method 0)
        b"setSubtitle",                            # 9 (method 1)
        b"setText",                                # 10 (method 2)
        b"addButton",                              # 11 (method 3)
        b"showToast",                              # 12 (method 4)
        b"onCreate",                               # 13 (method 5)
    ]
    insns = [
        0x001A, 0x0000,  0x106E, 0x0000, 0x0000,
        0x001A, 0x0001,  0x106E, 0x0001, 0x0000,
        0x001A, 0x0002,  0x106E, 0x0002, 0x0000,
        0x001A, 0x0003,  0x106E, 0x0002, 0x0000,
        0x001A, 0x0004,  0x106E, 0x0002, 0x0000,
        0x001A, 0x0005,  0x106E, 0x0003, 0x0000,
        0x001A, 0x0006,  0x106E, 0x0004, 0x0000,
        0x0012, 0x000F,
    ]
    return build_dex(strings, [8, 9, 10, 11, 12, 13], insns)

def write_stored_zip(path, entries):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    buf = bytearray()
    cdir = bytearray()
    for name, payload in entries:
        nb = name.encode("utf-8")
        crc = zlib.crc32(payload) & 0xFFFFFFFF
        l_off = len(buf)
        buf.extend(struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, 0, 0, 0, 0, crc, len(payload), len(payload), len(nb), 0))
        buf.extend(nb)
        buf.extend(payload)
        cdir.extend(struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, 0, 0, 0, 0, crc, len(payload), len(payload), len(nb), 0, 0, 0, 0, 0, l_off))
        cdir.extend(nb)
    cdir_off = len(buf)
    buf.extend(cdir)
    buf.extend(struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, len(entries), len(entries), len(cdir), cdir_off, 0))
    with open(path, "wb") as f:
        f.write(buf)
    return bytes(buf)

def c_array(name, data):
    lines = [f"static const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i+16])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)

if __name__ == "__main__":
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    hello_manifest = (
        b"<manifest package=\"com.flairos.helloworld\">"
        b"<application android:label=\"Hello World\" userTestApp=\"true\" android:testOnly=\"true\" android:debuggable=\"true\">"
        b"<meta-data android:name=\"flairos.user_test_app\" android:value=\"true\"/>"
        b"</application></manifest>"
    )
    calc_manifest = (
        b"<manifest package=\"com.flairos.calc\">"
        b"<application android:label=\"DroidCalc\" userTestApp=\"true\" android:testOnly=\"true\"/>"
        b"</manifest>"
    )
    sysinfo_manifest = (
        b"<manifest package=\"com.flairos.sysinfo\">"
        b"<application android:label=\"Device Info\" systemApp=\"true\"/>"
        b"</manifest>"
    )

    hello_apk = write_stored_zip(os.path.join(root, "examples/hello_apk/hello.apk"),
                                 [("AndroidManifest.xml", hello_manifest), ("classes.dex", build_hello_dex())])
    calc_apk = write_stored_zip(os.path.join(root, "examples/calc_apk/calc.apk"),
                                [("AndroidManifest.xml", calc_manifest), ("classes.dex", build_calc_dex())])
    sysinfo_apk = write_stored_zip(os.path.join(root, "examples/sysinfo_apk/sysinfo.apk"),
                                   [("AndroidManifest.xml", sysinfo_manifest), ("classes.dex", build_sysinfo_dex())])

    hdr_path = os.path.join(root, "include/apk_samples.h")
    with open(hdr_path, "w") as f:
        f.write("#ifndef FLAIROS_APK_SAMPLES_H\n#define FLAIROS_APK_SAMPLES_H\n\n")
        f.write(c_array("hello_apk_bytes", hello_apk) + "\n\n")
        f.write(c_array("calc_apk_bytes", calc_apk) + "\n\n")
        f.write(c_array("sysinfo_apk_bytes", sysinfo_apk) + "\n\n")
        f.write("#endif\n")
    print(f"mkapk: hello={len(hello_apk)}B, calc={len(calc_apk)}B, sysinfo={len(sysinfo_apk)}B -> include/apk_samples.h")
