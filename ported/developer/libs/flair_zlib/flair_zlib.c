/* SPDX-License-Identifier: AGPL-3.0-only */
/* Freestanding zlib-compatible CRC32 + Stored + RFC 1951 DEFLATE Inflate */
#include "flair_zlib.h"

uint32_t flair_crc32(const uint8_t *buf, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

int flair_uncompress_stored(const uint8_t *src, uint32_t src_len,
                            uint8_t *dst, uint32_t dst_cap, uint32_t *out_len) {
    if (!src || !dst || src_len > dst_cap) return -1;
    for (uint32_t i = 0; i < src_len; i++) dst[i] = src[i];
    if (out_len) *out_len = src_len;
    return 0;
}

struct inf_bs {
    const uint8_t *src;
    uint32_t len;
    uint32_t pos;
    uint32_t bitbuf;
    int bitcnt;
};

static int bs_need(struct inf_bs *bs, int n) {
    while (bs->bitcnt < n) {
        if (bs->pos >= bs->len) return -1;
        bs->bitbuf |= ((uint32_t)bs->src[bs->pos++]) << bs->bitcnt;
        bs->bitcnt += 8;
    }
    return 0;
}

static int bs_bits(struct inf_bs *bs, int n, uint32_t *out) {
    if (n == 0) { *out = 0; return 0; }
    if (bs_need(bs, n) != 0) return -1;
    *out = bs->bitbuf & ((1u << n) - 1u);
    bs->bitbuf >>= n;
    bs->bitcnt -= n;
    return 0;
}

struct huff_tab {
    uint16_t counts[16];
    uint16_t syms[320];
};

static int huff_build(struct huff_tab *t, const uint8_t *lens, int nsym) {
    uint16_t offs[16];
    for (int i = 0; i < 16; i++) t->counts[i] = 0;
    for (int i = 0; i < nsym; i++) if (lens[i] < 16) t->counts[lens[i]]++;
    t->counts[0] = 0;
    uint16_t sum = 0;
    for (int l = 1; l < 16; l++) { offs[l] = sum; sum = (uint16_t)(sum + t->counts[l]); }
    for (int i = 0; i < nsym; i++) {
        if (lens[i] > 0 && lens[i] < 16) t->syms[offs[lens[i]]++] = (uint16_t)i;
    }
    return 0;
}

static int huff_dec(struct inf_bs *bs, const struct huff_tab *t, uint16_t *sym) {
    int code = 0, first = 0, idx = 0;
    for (int len = 1; len <= 15; len++) {
        uint32_t b = 0;
        if (bs_bits(bs, 1, &b) != 0) return -1;
        code |= (int)b;
        int cnt = t->counts[len];
        if (code - cnt < first) {
            *sym = t->syms[idx + (code - first)];
            return 0;
        }
        idx += cnt;
        first = (first + cnt) << 1;
        code <<= 1;
    }
    return -1;
}

static int inf_block(struct inf_bs *bs, const struct huff_tab *lt, const struct huff_tab *dt,
                     uint8_t *dst, uint32_t dst_cap, uint32_t *op) {
    static const uint16_t lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint8_t  lext[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint8_t  dext[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

    for (;;) {
        uint16_t sym = 0;
        if (huff_dec(bs, lt, &sym) != 0) return -1;
        if (sym < 256u) {
            if (*op >= dst_cap) return -1;
            dst[(*op)++] = (uint8_t)sym;
        } else if (sym == 256u) {
            return 0;
        } else {
            uint16_t li = (uint16_t)(sym - 257u);
            if (li >= 29u) return -1;
            uint32_t extra = 0;
            if (bs_bits(bs, lext[li], &extra) != 0) return -1;
            uint32_t length = lbase[li] + extra;

            uint16_t dsym = 0;
            if (huff_dec(bs, dt, &dsym) != 0 || dsym >= 30u) return -1;
            if (bs_bits(bs, dext[dsym], &extra) != 0) return -1;
            uint32_t dist = dbase[dsym] + extra;
            if (dist == 0 || dist > *op || *op + length > dst_cap) return -1;
            for (uint32_t i = 0; i < length; i++) {
                dst[*op] = dst[*op - dist];
                (*op)++;
            }
        }
    }
}

int flair_inflate_raw(const uint8_t *src, uint32_t src_len,
                      uint8_t *dst, uint32_t dst_cap, uint32_t *out_len) {
    if (!src || !dst) return -1;
    struct inf_bs bs = { src, src_len, 0, 0, 0 };
    uint32_t op = 0;
    uint32_t bfinal = 0;

    do {
        uint32_t btype = 0;
        if (bs_bits(&bs, 1, &bfinal) != 0 || bs_bits(&bs, 2, &btype) != 0) return -1;
        if (btype == 0u) {
            bs.bitbuf = 0; bs.bitcnt = 0;
            if (bs.pos + 4u > bs.len) return -1;
            uint16_t len = (uint16_t)bs.src[bs.pos] | ((uint16_t)bs.src[bs.pos + 1] << 8);
            bs.pos += 4u;
            if (bs.pos + len > bs.len || op + len > dst_cap) return -1;
            for (uint16_t i = 0; i < len; i++) dst[op++] = bs.src[bs.pos++];
        } else if (btype == 1u) {
            uint8_t ll[288], dl[32];
            for (int i = 0; i <= 143; i++) ll[i] = 8;
            for (int i = 144; i <= 255; i++) ll[i] = 9;
            for (int i = 256; i <= 279; i++) ll[i] = 7;
            for (int i = 280; i <= 287; i++) ll[i] = 8;
            for (int i = 0; i < 32; i++) dl[i] = 5;
            struct huff_tab lt, dt;
            huff_build(&lt, ll, 288);
            huff_build(&dt, dl, 32);
            if (inf_block(&bs, &lt, &dt, dst, dst_cap, &op) != 0) return -1;
        } else if (btype == 2u) {
            static const uint8_t clord[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint32_t hlit = 0, hdist = 0, hclen = 0;
            if (bs_bits(&bs, 5, &hlit) != 0 || bs_bits(&bs, 5, &hdist) != 0 || bs_bits(&bs, 4, &hclen) != 0) return -1;
            hlit += 257u; hdist += 1u; hclen += 4u;
            uint8_t clens[19];
            for (int i = 0; i < 19; i++) clens[i] = 0;
            for (uint32_t i = 0; i < hclen; i++) {
                uint32_t v = 0;
                if (bs_bits(&bs, 3, &v) != 0) return -1;
                clens[clord[i]] = (uint8_t)v;
            }
            struct huff_tab ct;
            huff_build(&ct, clens, 19);
            uint8_t lens[320];
            uint32_t total = hlit + hdist, idx = 0;
            while (idx < total) {
                uint16_t sym = 0;
                if (huff_dec(&bs, &ct, &sym) != 0) return -1;
                if (sym < 16u) {
                    lens[idx++] = (uint8_t)sym;
                } else {
                    uint32_t rep = 0;
                    uint8_t val = 0;
                    if (sym == 16u) {
                        if (idx == 0 || bs_bits(&bs, 2, &rep) != 0) return -1;
                        val = lens[idx - 1]; rep += 3u;
                    } else if (sym == 17u) {
                        if (bs_bits(&bs, 3, &rep) != 0) return -1;
                        rep += 3u;
                    } else {
                        if (bs_bits(&bs, 7, &rep) != 0) return -1;
                        rep += 11u;
                    }
                    while (rep-- && idx < total) lens[idx++] = val;
                }
            }
            struct huff_tab lt, dt;
            huff_build(&lt, lens, (int)hlit);
            huff_build(&dt, lens + hlit, (int)hdist);
            if (inf_block(&bs, &lt, &dt, dst, dst_cap, &op) != 0) return -1;
        } else {
            return -1;
        }
    } while (!bfinal);

    if (out_len) *out_len = op;
    return 0;
}
