/*
 * ARM EABI 整数除法运行时 (固件没有 libgcc; Cortex-A5 没有硬件除法 UDIV/SDIV)。
 * 386 的 DIV r/m32 是 64 / 32, 所以 64 位的 uldivmod 也要有。算法: 移位相减, 先用 clz 对齐 (每位一次比较)。
 * 主机单测 tests/test_aeabi.c 用 -DAEABI_TEST 把名字换成 mx_*, 随机向量和编译器的 '/' 对拍。
 * 除以 0: 返回 0 (客户机的 #DE 由 CPU 核在调用除法前自己判断)。
 */
#include <stdint.h>

#ifdef AEABI_TEST
#define F(n) mx_##n
#else
#define F(n) __aeabi_##n
#endif

static inline int clz32(uint32_t x){ return x ? __builtin_clz(x) : 32; }
static inline int clz64(uint64_t x){ return (x >> 32) ? clz32((uint32_t)(x >> 32)) : 32 + clz32((uint32_t)x); }

uint32_t mx_udiv32(uint32_t n, uint32_t d, uint32_t *rem){
    if (d == 0){ if (rem) *rem = 0; return 0; }
    if (n < d){ if (rem) *rem = n; return 0; }
    int sh = clz32(d) - clz32(n);
    uint32_t q = 0;
    d <<= sh;
    for (; sh >= 0; sh--, d >>= 1){
        q <<= 1;
        if (n >= d){ n -= d; q |= 1; }
    }
    if (rem) *rem = n;
    return q;
}

uint64_t mx_udiv64(uint64_t n, uint64_t d, uint64_t *rem){
    if (d == 0){ if (rem) *rem = 0; return 0; }
    if ((n >> 32) == 0 && (d >> 32) == 0){
        uint32_t r; uint32_t q = mx_udiv32((uint32_t)n, (uint32_t)d, &r);
        if (rem) *rem = r;
        return q;
    }
    if (n < d){ if (rem) *rem = n; return 0; }
    int sh = clz64(d) - clz64(n);
    uint64_t q = 0;
    d <<= sh;
    for (; sh >= 0; sh--, d >>= 1){
        q <<= 1;
        if (n >= d){ n -= d; q |= 1; }
    }
    if (rem) *rem = n;
    return q;
}

uint32_t F(uidiv)(uint32_t n, uint32_t d){ return mx_udiv32(n, d, 0); }
int32_t F(idiv)(int32_t n, int32_t d){
    uint32_t un = n < 0 ? 0u - (uint32_t)n : (uint32_t)n, ud = d < 0 ? 0u - (uint32_t)d : (uint32_t)d;
    uint32_t q = mx_udiv32(un, ud, 0);
    return (int32_t)(((n < 0) != (d < 0)) ? 0u - q : q);
}
/* uidivmod / idivmod: 商在 r0, 余数在 r1 = 按 AAPCS 返回 64 位值 (低 32 位 = 商) */
uint64_t F(uidivmod)(uint32_t n, uint32_t d){
    uint32_t r; uint32_t q = mx_udiv32(n, d, &r);
    return (uint64_t)q | ((uint64_t)r << 32);
}
uint64_t F(idivmod)(int32_t n, int32_t d){
    uint32_t un = n < 0 ? 0u - (uint32_t)n : (uint32_t)n, ud = d < 0 ? 0u - (uint32_t)d : (uint32_t)d, r;
    uint32_t q = mx_udiv32(un, ud, &r);
    if ((n < 0) != (d < 0)) q = 0u - q;
    if (n < 0) r = 0u - r;                 /* 余数和被除数同号 (C 语义) */
    return (uint64_t)q | ((uint64_t)r << 32);
}

int64_t mx_sdiv64(int64_t n, int64_t d, int64_t *rem){
    uint64_t un = n < 0 ? 0u - (uint64_t)n : (uint64_t)n, ud = d < 0 ? 0u - (uint64_t)d : (uint64_t)d, r;
    uint64_t q = mx_udiv64(un, ud, &r);
    if ((n < 0) != (d < 0)) q = 0u - q;
    if (n < 0) r = 0u - r;
    if (rem) *rem = (int64_t)r;
    return (int64_t)q;
}

#if !defined(AEABI_TEST) && defined(__arm__)
/* uldivmod / ldivmod: 商在 r0:r1, 余数在 r2:r3 —— C 返回不了, 用汇编包一层:
   在栈上留 8 字节给余数, 调 C, 再把余数装进 r2:r3。入口时 r0:r1 = n, r2:r3 = d (AAPCS 64 位参数) */
__asm__(
    ".syntax unified\n"
    ".thumb\n"
    ".section .text.__aeabi_uldivmod,\"ax\",%progbits\n"
    ".global __aeabi_uldivmod\n"
    ".thumb_func\n"
    "__aeabi_uldivmod:\n"
    "  push {r4, lr}\n"
    "  sub sp, sp, #16\n"
    "  add r4, sp, #8\n"
    "  str r4, [sp]\n"                     /* 第 3 个参数 (指针) 在栈上: n 占 r0:r1, d 占 r2:r3 */
    "  bl mx_udiv64\n"
    "  ldrd r2, r3, [sp, #8]\n"
    "  add sp, sp, #16\n"
    "  pop {r4, pc}\n"
    ".section .text.__aeabi_ldivmod,\"ax\",%progbits\n"
    ".global __aeabi_ldivmod\n"
    ".thumb_func\n"
    "__aeabi_ldivmod:\n"
    "  push {r4, lr}\n"
    "  sub sp, sp, #16\n"
    "  add r4, sp, #8\n"
    "  str r4, [sp]\n"
    "  bl mx_sdiv64\n"
    "  ldrd r2, r3, [sp, #8]\n"
    "  add sp, sp, #16\n"
    "  pop {r4, pc}\n"
);
#endif
