/* RGB565 -> PPM / PNG, 见 imgout.h */
#include "imgout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t img_crc32(uint32_t c, const uint8_t *p, size_t n){
    c = ~c;
    while (n--){
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

static int write_all(const char *path, const uint8_t *b, size_t n){
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t k = fwrite(b, 1, n, f);
    return fclose(f) == 0 && k == n ? 0 : -1;
}

int img_write_ppm(const char *path, const uint16_t *fb, int w, int h, int stride){
    char hdr[32];
    int hl = snprintf(hdr, sizeof hdr, "P6\n%d %d\n255\n", w, h);
    size_t n = (size_t)hl + (size_t)w * h * 3u;
    uint8_t *b = malloc(n);
    if (!b) return -1;
    memcpy(b, hdr, (size_t)hl);
    uint8_t *d = b + hl;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++, d += 3) img_565_to_888(fb[(size_t)y * stride + x], d);
    int r = write_all(path, b, n);
    free(b);
    return r;
}

/* 每行 1 字节滤波类型 0 + 3w 字节; 存储块每块 ≤ 65535 字节 */
size_t img_png_size(int w, int h){
    size_t raw = (size_t)h * (1u + 3u * (size_t)w);
    size_t nblk = (raw + 65534u) / 65535u;
    if (!nblk) nblk = 1;
    return 8 + (12 + 13) + (12 + 2 + raw + 5 * nblk + 4) + 12;
}

static uint8_t *be32(uint8_t *p, uint32_t v){ p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; return p + 4; }
static uint8_t *chunk_end(uint8_t *start, uint8_t *p){      /* start 指向长度字段 */
    uint32_t len = (uint32_t)(p - start - 8);
    be32(start, len);
    return be32(p, img_crc32(0, start + 4, len + 4));
}

size_t img_png_mem(uint8_t *buf, size_t cap, const uint16_t *fb, int w, int h, int stride){
    size_t need = img_png_size(w, h);
    if (cap < need) return 0;
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    uint8_t *p = buf, *c;
    memcpy(p, sig, 8); p += 8;
    c = p; p += 4; memcpy(p, "IHDR", 4); p += 4;
    p = be32(p, (uint32_t)w); p = be32(p, (uint32_t)h);
    *p++ = 8; *p++ = 2; *p++ = 0; *p++ = 0; *p++ = 0;        /* 8 位 RGB, 不隔行 */
    p = chunk_end(c, p);
    c = p; p += 4; memcpy(p, "IDAT", 4); p += 4;
    *p++ = 0x78; *p++ = 0x01;                                  /* zlib 头: deflate, 32K 窗口 */
    size_t raw = (size_t)h * (1u + 3u * (size_t)w), left = raw, blk_left = 0;
    uint32_t a1 = 1, a2 = 0;                                   /* adler32 */
    for (int y = 0; y < h; y++){
        for (int x = -1; x < w; x++){
            uint8_t px[3]; int k = 1;
            if (x < 0) px[0] = 0; else { img_565_to_888(fb[(size_t)y * stride + x], px); k = 3; }
            for (int i = 0; i < k; i++){
                if (!blk_left){
                    blk_left = left > 65535u ? 65535u : left;
                    *p++ = left == blk_left ? 1 : 0;           /* BFINAL, BTYPE=00 */
                    *p++ = (uint8_t)blk_left; *p++ = (uint8_t)(blk_left >> 8);
                    *p++ = (uint8_t)~blk_left; *p++ = (uint8_t)(~blk_left >> 8);
                }
                *p++ = px[i]; blk_left--; left--;
                a1 = (a1 + px[i]) % 65521u; a2 = (a2 + a1) % 65521u;
            }
        }
    }
    p = be32(p, (a2 << 16) | a1);
    p = chunk_end(c, p);
    c = p; p += 4; memcpy(p, "IEND", 4); p += 4;
    p = chunk_end(c, p);
    return (size_t)(p - buf);
}

int img_write_png(const char *path, const uint16_t *fb, int w, int h, int stride){
    size_t n = img_png_size(w, h);
    uint8_t *b = malloc(n);
    if (!b) return -1;
    size_t k = img_png_mem(b, n, fb, w, h, stride);
    int r = k ? write_all(path, b, k) : -1;
    free(b);
    return r;
}
