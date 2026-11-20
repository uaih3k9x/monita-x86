/*
 * 开机一次性申请大块内存, 逐级降配 (设计: 客户机内存 3 MB -> 2 MB -> 1.5 MB; 覆盖层池 384 -> 256 -> 128 KiB; 读缓存 64 -> 32 KiB)。
 * 每一档按 客户机内存 -> 显存 -> 覆盖层 -> 读缓存 的顺序申请, 任何一块拿不到、或拿完后试探 malloc(reserve) 失败
 * (= 内核 / 基带剩的连续块不够), 就把这一档全部还掉试下一档。reserve 先按 MP_RESERVE (256 KiB) 试,
 * 所有档都不行再按硬下限 MP_FLOOR (128 KiB) 试一轮; 还不行就拒绝启动客户机。
 * 分配函数由调用方给 (固件 = 内核 malloc, 单测 = 假堆), 纯逻辑, 单测 tests/test_memplan.c。
 */
#ifndef MX_MEMPLAN_H
#define MX_MEMPLAN_H
#include <stdint.h>
#include <stddef.h>

#define MP_RESERVE (256u * 1024u)
#define MP_FLOOR   (128u * 1024u)
#define MP_VGA     (256u * 1024u)

typedef struct {
    void *(*alloc)(void *ctx, uint32_t n);
    void  (*release)(void *ctx, void *p);
    void *ctx;
} mp_ops_t;

typedef struct {
    void *mem, *vga, *ov, *rc;
    uint32_t mem_size, vga_size, ov_size, rc_size;
    uint32_t tier;          /* 用的哪一档 (0 最大) */
    uint32_t reserve;       /* 实际保证的余量 (MP_RESERVE 或 MP_FLOOR) */
} mp_plan_t;

typedef struct { uint32_t mem, ov, rc; } mp_tier_t;
static const mp_tier_t mp_tiers[] = {
    { 3u << 20,          384u << 10, 64u << 10 },
    { 2u << 20,          256u << 10, 64u << 10 },
    { (3u << 20) / 2u,   128u << 10, 32u << 10 },
};
#define MP_NTIERS (sizeof mp_tiers / sizeof mp_tiers[0])

/* ov_bytes_fn: 覆盖层池字节数 -> 需要的总字节 (加稀疏表); 为 NULL 时就按池字节数 */
static inline int mp_try(const mp_ops_t *o, const mp_tier_t *t, uint32_t reserve, uint32_t extra_ov, mp_plan_t *p){
    void *b[4] = { 0, 0, 0, 0 };
    uint32_t sz[4] = { t->mem, MP_VGA, t->ov + extra_ov, t->rc };
    for (int i = 0; i < 4; i++){
        b[i] = o->alloc(o->ctx, sz[i]);
        if (!b[i]) goto fail;
    }
    {
        void *probe = o->alloc(o->ctx, reserve);
        if (!probe) goto fail;
        o->release(o->ctx, probe);
    }
    p->mem = b[0]; p->vga = b[1]; p->ov = b[2]; p->rc = b[3];
    p->mem_size = sz[0]; p->vga_size = sz[1]; p->ov_size = sz[2]; p->rc_size = sz[3];
    p->reserve = reserve;
    return 0;
fail:
    for (int i = 3; i >= 0; i--) if (b[i]) o->release(o->ctx, b[i]);
    return -1;
}

/* extra_ov: 覆盖层稀疏表的额外字节 (和盘大小有关, 调用方算)。返回 0 成功, -1 = 内存不够, 不启动客户机 */
static inline int mp_alloc(const mp_ops_t *o, uint32_t extra_ov, mp_plan_t *p){
    static const uint32_t res[2] = { MP_RESERVE, MP_FLOOR };
    for (int r = 0; r < 2; r++)
        for (uint32_t i = 0; i < MP_NTIERS; i++)
            if (mp_try(o, &mp_tiers[i], res[r], extra_ov, p) == 0){ p->tier = i; return 0; }
    return -1;
}

static inline void mp_free(const mp_ops_t *o, mp_plan_t *p){
    void *b[4] = { p->rc, p->ov, p->vga, p->mem };
    for (int i = 0; i < 4; i++) if (b[i]) o->release(o->ctx, b[i]);
    p->mem = p->vga = p->ov = p->rc = 0;
}

#endif
