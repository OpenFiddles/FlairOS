#ifndef FLAIROS_DEX_H
#define FLAIROS_DEX_H

#include <stdint.h>

#define DEX_HEADER_SIZE 112u
#define DEX_UI_MAX_LINES 10
#define DEX_UI_LINE_LEN  60
#define DEX_UI_MAX_BTNS  4

struct dex_header {
    uint8_t  magic[8];       /* "dex\n035\0" or "dex\n039\0" */
    uint32_t checksum;
    uint8_t  signature[20];
    uint32_t file_size;
    uint32_t header_size;    /* 0x70 = 112 */
    uint32_t endian_tag;     /* 0x12345678 */
    uint32_t link_size;
    uint32_t link_off;
    uint32_t map_off;
    uint32_t string_ids_size;
    uint32_t string_ids_off;
    uint32_t type_ids_size;
    uint32_t type_ids_off;
    uint32_t proto_ids_size;
    uint32_t proto_ids_off;
    uint32_t field_ids_size;
    uint32_t field_ids_off;
    uint32_t method_ids_size;
    uint32_t method_ids_off;
    uint32_t class_defs_size;
    uint32_t class_defs_off;
    uint32_t data_size;
    uint32_t data_off;
} __attribute__((packed));

struct dex_ui_ctx {
    char title[48];
    char subtitle[48];
    char badge[32];
    char lines[DEX_UI_MAX_LINES][DEX_UI_LINE_LEN];
    int  line_count;
    char buttons[DEX_UI_MAX_BTNS][24];
    int  button_count;
    char toast[48];
};

int dex_validate(const uint8_t *buf, uint32_t len, struct dex_header *out);
int dex_read_string(const uint8_t *dex, uint32_t len, uint32_t str_idx,
                    char *out, uint32_t max_out);
int dex_exec_bytecode(const uint16_t *insns, uint32_t insn_count, int32_t *ret_out);
int dex_run_buffer(const uint8_t *dex, uint32_t len, struct dex_ui_ctx *ui,
                   int32_t *ret_out);

#endif
