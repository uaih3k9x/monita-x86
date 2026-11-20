/* 帧缓冲脏区 -> cache clean 段 (core/fbdirty.h): 每个脏像素都被某段覆盖, 段数 / 字节数不乱涨 */
#include "tcheck.h"
#include "../core/fbdirty.h"
#include <string.h>

#define W 800
#define H 1280
#define STRIDE (W * 2)
static uint8_t cov[STRIDE * H / 32];          /* 每个 32 B 缓存行有没有被 clean */
static uint32_t bytes;
static void mark(void *c, uintptr_t a, uint32_t n){
    (void)c; bytes += n;
    for (uintptr_t p = a & ~(uintptr_t)31; p < a + n; p += 32) if (p / 32 < sizeof cov) cov[p / 32] = 1;
}
static int covered(int x, int y, int w, int h){
    for (int r = y; r < y + h; r++) for (int c = x; c < x + w; c++)
        if (!cov[((uintptr_t)r * STRIDE + (uintptr_t)c * 2) / 32]) return 0;
    return 1;
}

int main(void){
    fbd_t d; fbd_clear(&d);
    CHECK(fbd_empty(&d));
    CHECK(fbd_flush(&d, 0, STRIDE, 2, W, mark, 0) == 0 && bytes == 0);
    /* 640x480 居中 (80, 400): 宽 >= 一半 -> 整行一段 */
    fbd_add(&d, 80, 400, 640, 480, W, H);
    CHECK(fbd_flush(&d, 0, STRIDE, 2, W, mark, 0) == 1);
    CHECK(bytes == 480u * STRIDE && covered(80, 400, 640, 480));
    CHECK(fbd_empty(&d));
    /* 两个小块: 并成外接矩形, 逐行 */
    memset(cov, 0, sizeof cov); bytes = 0;
    fbd_add(&d, 100, 500, 9, 16, W, H); fbd_add(&d, 130, 520, 8, 8, W, H);
    uint32_t n = fbd_flush(&d, 0, STRIDE, 2, W, mark, 0);
    CHECK(n == 28);                                      /* 行 500..527 */
    CHECK(covered(100, 500, 9, 16) && covered(130, 520, 8, 8));
    CHECK(bytes == 28u * 96u);                           /* x 100..138 = 字节 200..276, 按 32 B 取整成 192..288 */
    /* 越界裁剪, 空矩形忽略 */
    memset(cov, 0, sizeof cov); bytes = 0;
    fbd_add(&d, -5, -5, 0, 10, W, H); CHECK(fbd_empty(&d));
    fbd_add(&d, 790, 1270, 50, 50, W, H);
    CHECK(fbd_flush(&d, 0, STRIDE, 2, W, mark, 0) == 10 && covered(790, 1270, 10, 10));
    CHECK(cov[(STRIDE * H - 1) / 32]);
    return tc_done("dirty");
}
