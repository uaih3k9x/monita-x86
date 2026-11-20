/* RGB565 帧缓冲 -> PPM / PNG (PNG 用不压缩的 deflate 存储块, 不依赖 zlib)。主机侧用, 单测 tests/test_imgout.c */
#ifndef MX_IMGOUT_H
#define MX_IMGOUT_H
#include <stdint.h>
#include <stddef.h>

/* 565 -> 888, 高位复制到低位 (0x1f -> 0xff) */
static inline void img_565_to_888(uint16_t c, uint8_t *rgb){
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    rgb[0] = (uint8_t)((r << 3) | (r >> 2));
    rgb[1] = (uint8_t)((g << 2) | (g >> 4));
    rgb[2] = (uint8_t)((b << 3) | (b >> 2));
}
uint32_t img_crc32(uint32_t c, const uint8_t *p, size_t n);   /* zlib.crc32 增量: 初值 0 */
/* stride 以像素计。返回 0 成功 */
int img_write_ppm(const char *path, const uint16_t *fb, int w, int h, int stride);
int img_write_png(const char *path, const uint16_t *fb, int w, int h, int stride);
/* 写进内存 (单测用): 返回字节数, buf 太小返回 0 */
size_t img_png_mem(uint8_t *buf, size_t cap, const uint16_t *fb, int w, int h, int stride);
size_t img_png_size(int w, int h);

#endif
