#ifndef FLAIROS_PORTED_ZLIB_H
#define FLAIROS_PORTED_ZLIB_H

#include <stdint.h>

uint32_t flair_crc32(const uint8_t *buf, uint32_t len);
int flair_uncompress_stored(const uint8_t *src, uint32_t src_len,
                            uint8_t *dst, uint32_t dst_cap, uint32_t *out_len);
int flair_inflate_raw(const uint8_t *src, uint32_t src_len,
                      uint8_t *dst, uint32_t dst_cap, uint32_t *out_len);

#endif
