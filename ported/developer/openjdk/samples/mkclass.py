#!/usr/bin/env python3
"""Generate a real Java 21 (major=65, 0xCAFEBABE) HelloFlair.class and include/jvm_sample.h"""
import os, struct

def build_hello_class():
    # Constant pool (1-based)
    # 1: Utf8 "HelloFlair"
    # 2: Class -> 1
    # 3: Utf8 "java/lang/Object"
    # 4: Class -> 3
    # 5: Utf8 "main"
    # 6: Utf8 "([Ljava/lang/String;)V"
    # 7: Utf8 "Code"
    # 8: Utf8 "Hello from OpenJDK 21 on FlairOS!"
    # 9: String -> 8
    # 10: Utf8 "java/lang/System"
    # 11: Class -> 10
    # 12: Utf8 "out"
    # 13: Utf8 "Ljava/io/PrintStream;"
    # 14: NameAndType -> 12, 13
    # 15: Fieldref -> 11, 14
    # 16: Utf8 "java/io/PrintStream"
    # 17: Class -> 16
    # 18: Utf8 "println"
    # 19: Utf8 "(Ljava/lang/String;)V"
    # 20: NameAndType -> 18, 19
    # 21: Methodref -> 17, 20
    # 22: Utf8 "(I)V"
    # 23: NameAndType -> 18, 22
    # 24: Methodref -> 17, 23
    # 25: Utf8 "OpenJDK 21 Bytecode: 6 x 7 ="
    # 26: String -> 25
    cp = [
        (1, b"HelloFlair"),
        (7, 1),
        (1, b"java/lang/Object"),
        (7, 3),
        (1, b"main"),
        (1, b"([Ljava/lang/String;)V"),
        (1, b"Code"),
        (1, b"Hello from OpenJDK 21 on FlairOS!"),
        (8, 8),
        (1, b"java/lang/System"),
        (7, 10),
        (1, b"out"),
        (1, b"Ljava/io/PrintStream;"),
        (12, (12, 13)),
        (9, (11, 14)),
        (1, b"java/io/PrintStream"),
        (7, 16),
        (1, b"println"),
        (1, b"(Ljava/lang/String;)V"),
        (12, (18, 19)),
        (10, (17, 20)),
        (1, b"(I)V"),
        (12, (18, 22)),
        (10, (17, 23)),
        (1, b"OpenJDK 21 Bytecode: 6 x 7 ="),
        (8, 25),
    ]
    out = bytearray()
    out.extend(struct.pack(">IHHI", 0xCAFEBABE, 0, 65, len(cp) + 1)[:10])
    # fix pack: >IHH + >H
    out = bytearray(struct.pack(">IHHH", 0xCAFEBABE, 0, 65, len(cp) + 1))
    for tag, val in cp:
        out.append(tag)
        if tag == 1:
            out.extend(struct.pack(">H", len(val)))
            out.extend(val)
        elif tag in (7, 8):
            out.extend(struct.pack(">H", val))
        elif tag in (9, 10, 12):
            out.extend(struct.pack(">HH", val[0], val[1]))

    # access=0x0021, this=2, super=4, ifaces=0, fields=0, methods=1
    out.extend(struct.pack(">HHHHHH", 0x0021, 2, 4, 0, 0, 1))

    # Bytecode:
    # getstatic #15, ldc #9, invokevirtual #21
    # getstatic #15, ldc #26, invokevirtual #21
    # bipush 6, bipush 7, imul, istore_1, getstatic #15, iload_1, invokevirtual #24, return
    code = bytes([
        0xB2, 0x00, 15,  0x12, 9,   0xB6, 0x00, 21,
        0xB2, 0x00, 15,  0x12, 26,  0xB6, 0x00, 21,
        0x10, 6,  0x10, 7,  0x68,  0x3C,
        0xB2, 0x00, 15,  0x1B,  0xB6, 0x00, 24,
        0xB1
    ])
    # Code attribute: attr_name=7, attr_len=12+len(code), max_stack=4, max_locals=2, code_len, code, exc=0, attrs=0
    code_attr = struct.pack(">HIHHI", 7, 12 + len(code), 4, 2, len(code)) + code + struct.pack(">HH", 0, 0)
    # Method: public static main, name=5, desc=6, attrs=1
    out.extend(struct.pack(">HHHH", 0x0009, 5, 6, 1))
    out.extend(code_attr)
    out.extend(struct.pack(">H", 0)) # class attrs
    return bytes(out)

if __name__ == "__main__":
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
    cls = build_hello_class()
    cls_path = os.path.join(root, "ported/developer/openjdk/samples/HelloFlair.class")
    open(cls_path, "wb").write(cls)
    hdr_path = os.path.join(root, "include/jvm_sample.h")
    lines = ["#ifndef FLAIROS_JVM_SAMPLE_H\n#define FLAIROS_JVM_SAMPLE_H\n\nstatic const unsigned char hello_flair_class_bytes[] = {"]
    for i in range(0, len(cls), 16):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in cls[i:i+16]) + ",")
    lines.append("};\n\n#endif\n")
    open(hdr_path, "w").write("\n".join(lines))
    print(f"mkclass: wrote {cls_path} ({len(cls)} bytes) -> include/jvm_sample.h")
