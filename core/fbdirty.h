/*
 * 帧缓冲脏区: vga_refresh 的 redraw 回调 (x, y, w, h) 并成一个外接矩形; 刷完一轮后按它出 D-cache clean 的地址段。
 * 矩形宽 >= 一半行宽时整行连续 clean (一段), 否则逐行只 clean [x0, x1) 那截 (按 32 B 缓存行取整)。
 * clean 函数由调用方给 (固件 = DCCMVAC 循环 + dsb, 主机 = 空)。纯逻辑, 单测 tests/test_dirty.c。
 */
#ifndef MX_FBDIRTY_H
#define MX_FBDIRTY_H
#include <stdint.h>

typedef struct { int x0, y0, x1, y1; uint32_t rects, lines; } fbd_t;   /* x1/y1 不含; 空 = x1 <= x0 */

static inline void fbd_clear(fbd_t *d){ d->x0 = d->y0 = 1 << 30; d->x1 = d->y1 = 0; }
static inline int  fbd_empty(const fbd_t *d){ return d->x1 <= d->x0 || d->y1 <= d->y0; }
static inline void fbd_add(fbd_t *d, int x, int y, int w, int h, int fw, int fh){
    if (w <= 0 || h <= 0) return;
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0; if (y < 0) y = 0; if (x1 > fw) x1 = fw; if (y1 > fh) y1 = fh;
    if (x1 <= x || y1 <= y) return;
    if (x < d->x0) d->x0 = x; if (y < d->y0) d->y0 = y;
    if (x1 > d->x1) d->x1 = x1; if (y1 > d->y1) d->y1 = y1;
    d->rects++;
}

typedef void (*fbd_clean_fn)(void *ctx, uintptr_t addr, uint32_t len);
/* fb: 首地址, stride: 行跨 (字节), bpp: 每像素字节。出完段清空。返回出了几段 */
static inline uint32_t fbd_flush(fbd_t *d, uintptr_t fb, uint32_t stride, uint32_t bpp, int fw,
                                 fbd_clean_fn fn, void *ctx){
    if (fbd_empty(d)) return 0;
    uint32_t n = 0;
    if ((d->x1 - d->x0) * 2 >= fw){
        fn(ctx, fb + (uintptr_t)d->y0 * stride, (uint32_t)(d->y1 - d->y0) * stride); n = 1;
        d->lines += (uint32_t)(d->y1 - d->y0);
    } else {
        uintptr_t a0 = (uintptr_t)d->x0 * bpp, a1 = (uintptr_t)d->x1 * bpp;
        a0 &= ~(uintptr_t)31; a1 = (a1 + 31) & ~(uintptr_t)31;
        if (a1 > stride) a1 = stride;
        for (int y = d->y0; y < d->y1; y++, n++) fn(ctx, fb + (uintptr_t)y * stride + a0, (uint32_t)(a1 - a0));
        d->lines += (uint32_t)(d->y1 - d->y0);
    }
    fbd_clear(d);
    return n;
}

#endif
