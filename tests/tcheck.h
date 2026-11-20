/* 最小断言框架: CHECK 失败打印位置并计数, tc_done 汇总 (返回值给 main)。 */
#ifndef TCHECK_H
#define TCHECK_H
#include <stdio.h>
static int tc_fail, tc_n;
#define CHECK(c) do { tc_n++; if (!(c)) { tc_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static int tc_done(const char *name){
    printf("%-10s %s (%d checks, %d failed)\n", name, tc_fail ? "FAIL" : "ok", tc_n, tc_fail);
    return tc_fail != 0;
}
#endif
