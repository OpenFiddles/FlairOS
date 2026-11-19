#include "../include/image.h"
#include "../include/bmp.h"

int image_probe(const unsigned char *data, uint32_t length,
                struct image_info *out) {
    struct bmp_info bmp;
    if (!data || !out)
        return IMAGE_ERR_ARGUMENT;
    if (bmp_parse(data, length, &bmp) != BMP_OK)
        return IMAGE_ERR_FORMAT;
    out->width = (uint32_t)bmp.width;
    out->height = (uint32_t)bmp.height;
    out->bits_per_pixel = (uint16_t)bmp.bpp;
    out->format = IMAGE_FORMAT_BMP;
    return IMAGE_OK;
}

int image_decode(const unsigned char *data, uint32_t length,
                 struct image_buffer *destination,
                 struct image_info *out) {
    struct bmp_info bmp;
    uint32_t row_bytes;
    if (!data || !destination || !destination->pixels)
        return IMAGE_ERR_ARGUMENT;
    if (bmp_parse(data, length, &bmp) != BMP_OK)
        return IMAGE_ERR_FORMAT;
    row_bytes = (uint32_t)bmp.width * 4u;
    if (destination->width < (uint32_t)bmp.width ||
        destination->height < (uint32_t)bmp.height ||
        destination->pitch < row_bytes || (destination->pitch & 3u) != 0 ||
        destination->height == 0 ||
        destination->pitch > destination->capacity ||
        (uint32_t)bmp.height > destination->capacity / destination->pitch)
        return IMAGE_ERR_BUFFER;

    for (uint32_t y = 0; y < (uint32_t)bmp.height; ++y) {
        uint32_t *row = (uint32_t *)((uint8_t *)destination->pixels +
                                     y * destination->pitch);
        for (uint32_t x = 0; x < (uint32_t)bmp.width; ++x) {
            unsigned int rgb;
            if (bmp_pixel(data, length, &bmp, (int)x, (int)y, &rgb) != BMP_OK)
                return IMAGE_ERR_FORMAT;
            row[x] = rgb;
        }
    }
    if (out) {
        out->width = (uint32_t)bmp.width;
        out->height = (uint32_t)bmp.height;
        out->bits_per_pixel = (uint16_t)bmp.bpp;
        out->format = IMAGE_FORMAT_BMP;
    }
    return IMAGE_OK;
}
