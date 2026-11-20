/* 只读块缓存, 见 rcache.h */
#include "rcache.h"
#include <string.h>

void rc_init(rc_t *rc, uint8_t *mem, uint32_t nblk, rc_read_fn fn, void *ctx, uint32_t size){
    memset(rc, 0, sizeof *rc);
    rc->mem = mem; rc->nblk = nblk > RC_MAXB ? RC_MAXB : nblk;
    rc->read = fn; rc->ctx = ctx; rc->size = size;
}

/* 块 b 在缓存里的下标; 不在就读进来 (换出最老的)。-1 = 后端出错 */
static int rc_get(rc_t *rc, uint32_t b){
    int victim = 0;
    for (uint32_t i = 0; i < rc->nblk; i++){
        if (rc->tag[i] == b + 1){ rc->stamp[i] = ++rc->clock; rc->hits++; return (int)i; }
        if (rc->stamp[i] < rc->stamp[victim]) victim = (int)i;
    }
    rc->misses++;
    uint8_t *p = rc->mem + (uint32_t)victim * RC_BLK;
    int r = rc->read(rc->ctx, b * RC_BLK, p, RC_BLK);
    if (r <= 0){ rc->tag[victim] = 0; rc->errors++; return -1; }
    rc->tag[victim] = b + 1; rc->len[victim] = (uint32_t)r; rc->stamp[victim] = ++rc->clock;
    return victim;
}

uint32_t rc_read(rc_t *rc, uint32_t off, uint8_t *dst, uint32_t n){
    uint32_t done = 0;
    if (off >= rc->size) return 0;
    if (n > rc->size - off) n = rc->size - off;
    if (!rc->nblk){                        /* 没给缓存: 直接读 */
        int r = rc->read(rc->ctx, off, dst, n);
        if (r < 0){ rc->errors++; return 0; }
        return (uint32_t)r;
    }
    while (done < n){
        uint32_t o = off + done, b = o / RC_BLK, in = o % RC_BLK;
        int i = rc_get(rc, b);
        if (i < 0 || rc->len[i] <= in) break;
        uint32_t k = rc->len[i] - in;
        if (k > n - done) k = n - done;
        memcpy(dst + done, rc->mem + (uint32_t)i * RC_BLK + in, k);
        done += k;
    }
    return done;
}
