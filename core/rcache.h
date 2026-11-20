/*
 * 只读文件的块缓存: nblk 个 4 KiB 块, 最近最少使用淘汰。外挂 NOR 上 SFFS 的随机小读很贵, 客户机读扇区又多是相邻的。
 * 后端 read_fn(ctx, off, buf, n) 返回读到的字节数 (到文件尾可以少)。纯逻辑, 主机单测 tests/test_rcache.c。
 */
#ifndef MX_RCACHE_H
#define MX_RCACHE_H
#include <stdint.h>

#define RC_BLK   4096u
#define RC_MAXB  32u

typedef int (*rc_read_fn)(void *ctx, uint32_t off, void *buf, uint32_t n);

typedef struct {
    uint8_t *mem;                 /* nblk * RC_BLK */
    uint32_t nblk, size;          /* size = 文件大小 */
    uint32_t tag[RC_MAXB];        /* 块号 + 1, 0 = 空 */
    uint32_t len[RC_MAXB];        /* 这块里有效字节 */
    uint32_t stamp[RC_MAXB], clock;
    rc_read_fn read; void *ctx;
    uint32_t hits, misses, errors;
} rc_t;

void rc_init(rc_t *rc, uint8_t *mem, uint32_t nblk, rc_read_fn fn, void *ctx, uint32_t size);
/* 从 off 读 n 字节进 dst, 返回实际字节数 (文件尾截断; 后端出错时到出错处为止) */
uint32_t rc_read(rc_t *rc, uint32_t off, uint8_t *dst, uint32_t n);

#endif
