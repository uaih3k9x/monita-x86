/* host 版 plat_alloc 记账: 当前量 / 峰值 (估固件堆里小块用量) */
#include "tcheck.h"
#include "../core/plat.h"
#include <stdint.h>

int main(void){
    size_t cur, peak;
    plat_alloc_stats(&cur, &peak);
    CHECK(cur == 0 && peak == 0);
    void *a = plat_alloc(1000), *b = plat_alloc(24);
    CHECK(a && b);
    CHECK(((uint8_t *)a)[999] == 0);            /* 清零 */
    plat_alloc_stats(&cur, &peak);
    CHECK(cur == 1024 && peak == 1024);
    plat_free(a);
    plat_alloc_stats(&cur, &peak);
    CHECK(cur == 24 && peak == 1024);
    plat_free(0);                               /* 同 free(NULL) */
    char *s = plat_strdup("abc");
    CHECK(s && s[0] == 'a' && s[3] == 0);
    plat_alloc_stats(&cur, &peak);
    CHECK(cur == 28);
    plat_free(s); plat_free(b);
    plat_alloc_stats(&cur, &peak);
    CHECK(cur == 0 && peak == 1024);
    return tc_done("hostalloc");
}
