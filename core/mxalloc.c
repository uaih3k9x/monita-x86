/* plat_alloc 记账: 每块前面 8 字节记大小, 统计当前量 / 峰值 (内存预算要用, 见 core/memplan.h) */
#include "plat.h"
#include <string.h>

static size_t g_cur, g_peak;

void *plat_alloc(size_t n){
    uint8_t *p = plat_raw_alloc(n + 8);
    if (!p) return NULL;
    memset(p, 0, n + 8);
    *(size_t *)p = n;
    g_cur += n;
    if (g_cur > g_peak) g_peak = g_cur;
    return p + 8;
}
void plat_free(void *q){
    if (!q) return;
    uint8_t *p = (uint8_t *)q - 8;
    g_cur -= *(size_t *)p;
    plat_raw_free(p);
}
char *plat_strdup(const char *s){
    size_t n = strlen(s) + 1;
    char *d = plat_alloc(n);
    if (d) memcpy(d, s, n);
    return d;
}
void plat_alloc_stats(size_t *cur, size_t *peak){ if (cur) *cur = g_cur; if (peak) *peak = g_peak; }
