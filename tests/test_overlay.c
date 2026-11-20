/* 写覆盖层 (core/overlay.h) 单测: 主机上跑, 不依赖固件。 */
#include "tcheck.h"
#include "../core/overlay.h"
#include <stdlib.h>
#include <string.h>

static uint8_t sec(int v){ return (uint8_t)(v * 37 + 11); }
static void fill(uint8_t *b, int v){ memset(b, sec(v), 512); }

static void t_basic(void){
    ov_t ov; size_t need = ov_bytes(1000, 4);
    void *mem = malloc(need);
    CHECK(ov_init(&ov, mem, need, 1000, 4) == 0);
    CHECK(ov_lookup(&ov, 0) == NULL);
    CHECK(ov_lookup(&ov, 999) == NULL);
    uint8_t b[512]; fill(b, 1);
    CHECK(ov_write(&ov, 5, b) == 0);
    CHECK(ov.used == 1);
    const uint8_t *p = ov_lookup(&ov, 5);
    CHECK(p && p[0] == sec(1) && p[511] == sec(1));
    CHECK(ov_lookup(&ov, 4) == NULL && ov_lookup(&ov, 6) == NULL);
    /* 覆盖已有扇区不占新槽 */
    fill(b, 2); CHECK(ov_write(&ov, 5, b) == 0); CHECK(ov.used == 1);
    CHECK(ov_lookup(&ov, 5)[100] == sec(2));
    free(mem);
}

static void t_full(void){
    ov_t ov; size_t need = ov_bytes(1000, 3);
    void *mem = malloc(need);
    CHECK(ov_init(&ov, mem, need, 1000, 3) == 0);
    uint8_t b[512];
    for (int i = 0; i < 3; i++){ fill(b, i); CHECK(ov_write(&ov, (uint32_t)(i * 300), b) == 0); }
    fill(b, 9);
    CHECK(ov_write(&ov, 1, b) == -1);           /* 池满: 新扇区拒 */
    CHECK(ov.full_rejects == 1);
    CHECK(ov_lookup(&ov, 1) == NULL);
    CHECK(ov_write(&ov, 300, b) == 0);          /* 已有扇区照常覆盖 */
    CHECK(ov_lookup(&ov, 300)[0] == sec(9));
    CHECK(ov_need(&ov, 299, 3) == 2);           /* 299, 301 是新的, 300 已有 */
    CHECK(ov_write(&ov, 1000, b) == -1);        /* 越界 */
    ov_reset(&ov);
    CHECK(ov.used == 0 && ov_lookup(&ov, 300) == NULL);
    CHECK(ov_write(&ov, 1, b) == 0);
    free(mem);
}

static void t_small_mem(void){
    ov_t ov; size_t need = ov_bytes(64 * 10, 8);
    void *mem = malloc(need);
    CHECK(ov_init(&ov, mem, need - 1, 64 * 10, 8) == -1);   /* 给的内存不够 */
    CHECK(ov_init(&ov, mem, need, 64 * 10, 0) == 0);        /* 池 0: 只读盘 */
    uint8_t b[512]; fill(b, 0);
    CHECK(ov_write(&ov, 0, b) == -1);
    free(mem);
}

/* 随机写和参照模型对拍 */
static void t_random(void){
    enum { N = 5000, CAP = 700 };
    ov_t ov; size_t need = ov_bytes(N, CAP);
    void *mem = malloc(need);
    CHECK(ov_init(&ov, mem, need, N, CAP) == 0);
    static int model[N];                   /* -1 = 没写过, 否则写进去的值 */
    for (int i = 0; i < N; i++) model[i] = -1;
    int used = 0; uint32_t r = 12345;
    uint8_t b[512];
    for (int it = 0; it < 20000; it++){
        r = r * 1103515245u + 12345u;
        uint32_t s = (r >> 8) % N; int v = (int)(r >> 3) & 0xff;
        fill(b, v);
        int rc = ov_write(&ov, s, b);
        if (model[s] >= 0 || used < CAP){
            CHECK(rc == 0);
            if (model[s] < 0) used++;
            model[s] = v;
        } else CHECK(rc == -1);
    }
    CHECK((int)ov.used == used);
    for (int i = 0; i < N; i++){
        const uint8_t *p = ov_lookup(&ov, (uint32_t)i);
        if (model[i] < 0) CHECK(p == NULL);
        else CHECK(p && p[0] == sec(model[i]) && p[511] == sec(model[i]));
    }
    free(mem);
}

int main(void){
    t_basic(); t_full(); t_small_mem(); t_random();
    return tc_done("overlay");
}
