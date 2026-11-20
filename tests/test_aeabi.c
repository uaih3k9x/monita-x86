/* 整数除法运行时 (port/aeabi_div.c, -DAEABI_TEST 下名字是 mx_*): 边界值 + 随机向量, 和编译器的 '/' '%' 对拍。
 * 386 DIV r/m32 = 64 位被除数 / 32 位除数, 商要 < 2^32 (否则 #DE): 专门造这种向量。 */
#include "tcheck.h"
#include <stdint.h>

uint32_t mx_udiv32(uint32_t n, uint32_t d, uint32_t *rem);
uint64_t mx_udiv64(uint64_t n, uint64_t d, uint64_t *rem);
int64_t  mx_sdiv64(int64_t n, int64_t d, int64_t *rem);
uint32_t mx_uidiv(uint32_t n, uint32_t d);
int32_t  mx_idiv(int32_t n, int32_t d);
uint64_t mx_uidivmod(uint32_t n, uint32_t d);
uint64_t mx_idivmod(int32_t n, int32_t d);

static uint64_t s = 88172645463325252ull;
static uint64_t rnd(void){ s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
/* 随机位宽, 让小数 / 大数都常见 */
static uint64_t rbits(void){ int b = (int)(rnd() % 65); return b == 64 ? rnd() : rnd() & ((1ull << b) - 1); }

static void one32(uint32_t n, uint32_t d){
    if (!d) return;
    CHECK(mx_uidiv(n, d) == n / d);
    uint64_t qr = mx_uidivmod(n, d);
    CHECK((uint32_t)qr == n / d && (uint32_t)(qr >> 32) == n % d);
    int32_t sn = (int32_t)n, sd = (int32_t)d;
    if (!(sn == INT32_MIN && sd == -1)){
        CHECK(mx_idiv(sn, sd) == sn / sd);
        uint64_t q2 = mx_idivmod(sn, sd);
        CHECK((int32_t)(uint32_t)q2 == sn / sd && (int32_t)(uint32_t)(q2 >> 32) == sn % sd);
    }
}
static void one64(uint64_t n, uint64_t d){
    if (!d) return;
    uint64_t r; CHECK(mx_udiv64(n, d, &r) == n / d && r == n % d);
    int64_t sn = (int64_t)n, sd = (int64_t)d, sr;
    if (!(sn == INT64_MIN && sd == -1)) CHECK(mx_sdiv64(sn, sd, &sr) == sn / sd && sr == sn % sd);
}

int main(void){
    static const uint32_t e32[] = { 0, 1, 2, 3, 7, 10, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu, 0xFFFFFFFFu, 65535, 65536, 1000000 };
    for (unsigned i = 0; i < sizeof e32 / 4; i++) for (unsigned j = 0; j < sizeof e32 / 4; j++) one32(e32[i], e32[j]);
    static const uint64_t e64[] = { 0, 1, 2, 0xFFFFFFFFull, 0x100000000ull, 0x100000001ull, 0x7FFFFFFFFFFFFFFFull,
                                    0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull, 1000000007ull, 0xFFFFFFFF00000000ull };
    for (unsigned i = 0; i < sizeof e64 / 8; i++) for (unsigned j = 0; j < sizeof e64 / 8; j++) one64(e64[i], e64[j]);
    for (int i = 0; i < 300000; i++){ one32((uint32_t)rbits(), (uint32_t)rbits()); one64(rbits(), rbits()); }
    /* DIV r/m32 形状: 被除数 EDX:EAX, EDX < 除数 (这样商放得下 32 位) */
    for (int i = 0; i < 200000; i++){
        uint32_t d = (uint32_t)rbits(); if (!d) continue;
        uint64_t n = ((uint64_t)((uint32_t)rnd() % d) << 32) | (uint32_t)rnd();
        uint64_t r, q = mx_udiv64(n, d, &r);
        CHECK(q == n / d && q <= 0xFFFFFFFFu && r == n % d);
    }
    uint32_t r0; CHECK(mx_udiv32(5, 0, &r0) == 0 && r0 == 0);      /* 除 0 不崩 */
    return tc_done("aeabi");
}
