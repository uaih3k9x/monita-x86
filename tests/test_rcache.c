/* EFS 读缓存 (core/rcache.h) 单测: 假后端按字节偏移生成内容, 记读了几次。 */
#include "tcheck.h"
#include "../core/rcache.h"
#include <stdlib.h>
#include <string.h>

static uint32_t g_reads, g_size = 100000;
static uint8_t at(uint32_t off){ return (uint8_t)(off * 7u + (off >> 9)); }
static int fake_read(void *ctx, uint32_t off, void *buf, uint32_t n){
    (void)ctx; g_reads++;
    if (off >= g_size) return 0;
    if (n > g_size - off) n = g_size - off;
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)buf)[i] = at(off + i);
    return (int)n;
}
static int ok_range(const uint8_t *b, uint32_t off, uint32_t n){
    for (uint32_t i = 0; i < n; i++) if (b[i] != at(off + i)) return 0;
    return 1;
}

int main(void){
    rc_t rc; static uint8_t mem[RC_BLK * 4]; uint8_t buf[20000];
    rc_init(&rc, mem, 4, fake_read, 0, g_size);
    CHECK(rc_read(&rc, 0, buf, 512) == 512 && ok_range(buf, 0, 512));
    CHECK(g_reads == 1);
    CHECK(rc_read(&rc, 512, buf, 512) == 512 && ok_range(buf, 512, 512));
    CHECK(g_reads == 1 && rc.hits == 1);                       /* 同一块: 命中 */
    CHECK(rc_read(&rc, 4000, buf, 200) == 200 && ok_range(buf, 4000, 200));   /* 跨块 */
    CHECK(g_reads == 2);
    /* 4 块 LRU: 再读 3 个新块, 最老的 (块 0? 块 0 最近刚用过) 被换出 */
    rc_read(&rc, 0, buf, 1);                                   /* 块 0 变最新 */
    g_reads = 0;
    rc_read(&rc, RC_BLK * 10, buf, 1); rc_read(&rc, RC_BLK * 11, buf, 1); rc_read(&rc, RC_BLK * 12, buf, 1);
    CHECK(g_reads == 3);
    g_reads = 0; rc_read(&rc, 0, buf, 1); CHECK(g_reads == 0);  /* 块 0 还在 */
    g_reads = 0; rc_read(&rc, RC_BLK, buf, 1); CHECK(g_reads == 1);  /* 块 1 被换出了 */
    /* 大读 + 文件尾 */
    CHECK(rc_read(&rc, 3, buf, 20000) == 20000 && ok_range(buf, 3, 20000));
    CHECK(rc_read(&rc, g_size - 10, buf, 100) == 10 && ok_range(buf, g_size - 10, 10));
    CHECK(rc_read(&rc, g_size + 5, buf, 100) == 0);
    /* 随机对拍 */
    uint32_t r = 7;
    for (int i = 0; i < 3000; i++){
        r = r * 1664525u + 1013904223u;
        uint32_t off = (r >> 4) % g_size, n = 1 + (r >> 20) % 9000;
        uint32_t want = off + n > g_size ? g_size - off : n;
        CHECK(rc_read(&rc, off, buf, n) == want && ok_range(buf, off, want));
    }
    /* 没有缓存块: 直通 */
    rc_t z; rc_init(&z, 0, 0, fake_read, 0, g_size); g_reads = 0;
    CHECK(rc_read(&z, 10, buf, 100) == 100 && ok_range(buf, 10, 100) && g_reads == 1);
    return tc_done("rcache");
}
