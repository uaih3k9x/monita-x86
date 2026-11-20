/* 逐级降配 (core/memplan.h): 假堆 = 一段连续地址空间, 首次适配, 释放后合并; 能算"最大连续块"。 */
#include "tcheck.h"
#include "../core/memplan.h"
#include <string.h>

#define NB 64
typedef struct { uint32_t base[NB], size[NB]; int n; uint32_t heap; uint32_t peak; } fheap_t;

/* 已分配块按地址有序, 空闲 = 缝隙 */
static void *fh_alloc(void *c, uint32_t n){
    fheap_t *h = c; uint32_t at = 0x1000;          /* 地址 0 留给 NULL */
    for (int i = 0; i <= h->n; i++){
        uint32_t end = i < h->n ? h->base[i] : h->heap;
        if (end >= at && end - at >= n){
            memmove(&h->base[i + 1], &h->base[i], sizeof(uint32_t) * (h->n - i));
            memmove(&h->size[i + 1], &h->size[i], sizeof(uint32_t) * (h->n - i));
            h->base[i] = at; h->size[i] = n; h->n++;
            return (void *)(uintptr_t)at;
        }
        if (i < h->n) at = h->base[i] + h->size[i];
    }
    return NULL;
}
static void fh_free(void *c, void *p){
    fheap_t *h = c;
    for (int i = 0; i < h->n; i++) if (h->base[i] == (uint32_t)(uintptr_t)p){
        memmove(&h->base[i], &h->base[i + 1], sizeof(uint32_t) * (h->n - i - 1));
        memmove(&h->size[i], &h->size[i + 1], sizeof(uint32_t) * (h->n - i - 1));
        h->n--; return;
    }
}
static uint32_t fh_largest(fheap_t *h){
    uint32_t at = 0x1000, best = 0;
    for (int i = 0; i <= h->n; i++){
        uint32_t end = i < h->n ? h->base[i] : h->heap;
        if (end - at > best) best = end - at;
        if (i < h->n) at = h->base[i] + h->size[i];
    }
    return best;
}

static void run(uint32_t heap, int want_rc, uint32_t want_tier, uint32_t want_reserve){
    fheap_t h; memset(&h, 0, sizeof h); h.heap = 0x1000 + heap;
    mp_ops_t o = { fh_alloc, fh_free, &h };
    mp_plan_t p; memset(&p, 0, sizeof p);
    int rc = mp_alloc(&o, 2048, &p);
    CHECK(rc == want_rc);
    if (rc == 0){
        CHECK(p.tier == want_tier && p.reserve == want_reserve);
        CHECK(p.mem_size == mp_tiers[want_tier].mem && p.vga_size == MP_VGA);
        CHECK(fh_largest(&h) >= p.reserve + MP_SMALL); /* 余量真的在 (tiny386 小块之后还剩 reserve) */
        CHECK(h.n == 4);                             /* 只剩这 4 块, 试探块已还 */
        mp_free(&o, &p);
        CHECK(h.n == 0);
    } else CHECK(h.n == 0);                          /* 失败时什么都没留 */
}

int main(void){
    /* 每档总占用 (含 tiny386 小块峰值) 不超过 Monita 三缓冲实机验证过的量 */
    for (uint32_t i = 0; i < MP_NTIERS; i++)
        CHECK(mp_tiers[i].mem + MP_VGA + mp_tiers[i].ov + mp_tiers[i].rc + MP_SMALL <= MP_BUDGET);
    CHECK(mp_tiers[0].mem >= (2u << 20));            /* Win3.1 标准模式最少 2 MB */
    /* 实测: 堆最大块 ≈ 4.35 MB */
    run(4350u << 10, 0, 0, MP_RESERVE);
    run(3000u << 10, 0, 1, MP_RESERVE);              /* 2 MB 档 + 余量放不下 -> 1.5 MB */
    run(2400u << 10, -1, 0, 0);                      /* 1.5 MB 档放得下但余量 < 256 KiB: 不启动 (没有更低的保底档) */
    run(2300u << 10, -1, 0, 0);                      /* 都放不下: 不启动客户机 */
    CHECK(MP_RESERVE >= 256u * 1024u);               /* 给内核 / 基带的保底不低于 256 KiB */
    /* 试探块失败要退档: 恰好够 tier0 四块但不够余量 */
    uint32_t t0 = mp_tiers[0].mem + MP_VGA + mp_tiers[0].ov + 2048 + mp_tiers[0].rc;
    run(t0 + 1000, 0, 1, MP_RESERVE);
    return tc_done("memplan");
}
