#ifndef FLAIROS_IMAGE_H
#define FLAIROS_IMAGE_H

#include <stdint.h>

#define IMAGE_OK 0
#define IMAGE_ERR_ARGUMENT (-1)
#define IMAGE_ERR_FORMAT (-2)
#define IMAGE_ERR_BUFFER (-3)

enum image_format {
    IMAGE_FORMAT_UNKNOWN = 0,
    IMAGE_FORMAT_BMP = 1
};

/* Decoded images use packed 0x00RRGGBB pixels. Pitch and capacity are bytes. */
struct image_info {
    uint32_t width;
    uint32_t height;
    uint16_t bits_per_pixel;
    enum image_format format;
};

struct image_buffer {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t capacity;
};

/* Detect a supported image format and return its dimensions without allocation. */
int image_probe(const unsigned char *data, uint32_t length,
                struct image_info *out);

/* Decode into caller-owned storage. The destination must fit the full image. */
int image_decode(const unsigned char *data, uint32_t length,
                 struct image_buffer *destination,
                 struct image_info *out);

#endif
