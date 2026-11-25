#ifndef FLAIROS_OPENJDK_JVM64_H
#define FLAIROS_OPENJDK_JVM64_H

#include <stdint.h>

#define JVM_MAGIC 0xCAFEBABEu
#define JVM_MAX_OUT_LINES 8
#define JVM_LINE_LEN      64

struct jvm_run_result {
    uint16_t minor_ver;
    uint16_t major_ver;
    char class_name[64];
    char lines[JVM_MAX_OUT_LINES][JVM_LINE_LEN];
    int line_count;
    int32_t ret_val;
};

int jvm_run_class(const uint8_t *buf, uint32_t len, struct jvm_run_result *out);

#endif
