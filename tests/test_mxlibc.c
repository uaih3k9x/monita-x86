/* port/mxlibc.c (固件 / QEMU 版没有 libc): -DMXLIBC_TEST 下名字是 mx_*, 和主机 libc 对拍 */
#include "tcheck.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void *mx_memcpy(void *d, const void *s, size_t n);
void *mx_memmove(void *d, const void *s, size_t n);
void *mx_memset(void *d, int c, size_t n);
int   mx_memcmp(const void *a, const void *b, size_t n);
size_t mx_strlen(const char *s);
int   mx_strcmp(const char *a, const char *b);
char *mx_strcpy(char *d, const char *s);
int   mx_atoi(const char *s);
int   mx_snprintf(char *o, size_t n, const char *f, ...);

int main(void){
    char buf[64], ref[64];
    unsigned char x[40], y[40];
    for (int i = 0; i < 40; i++) x[i] = (unsigned char)(i * 7 + 1);
    memset(y, 0, sizeof y); mx_memcpy(y + 3, x, 17); CHECK(!memcmp(y + 3, x, 17) && y[2] == 0 && y[20] == 0);
    mx_memset(y, 0xab, 40); CHECK(y[0] == 0xab && y[39] == 0xab);
    memcpy(y, x, 40); mx_memmove(y + 5, y, 30); CHECK(!memcmp(y + 5, x, 30));
    memcpy(y, x, 40); mx_memmove(y, y + 5, 30); CHECK(!memcmp(y, x + 5, 30));
    CHECK(mx_memcmp(x, x, 40) == 0);
    memcpy(y, x, 40); y[9]++; CHECK(mx_memcmp(x, y, 40) < 0 && mx_memcmp(y, x, 40) > 0);
    y[9] = 0xff; x[9] = 0x01; CHECK(mx_memcmp(x, y, 40) < 0);          /* 按无符号比 */
    CHECK(mx_strlen("") == 0 && mx_strlen("abc") == 3);
    CHECK(mx_strcmp("abc", "abc") == 0 && mx_strcmp("abc", "abd") < 0 && mx_strcmp("b", "abc") > 0 && mx_strcmp("ab", "abc") < 0);
    CHECK(!strcmp(mx_strcpy(buf, "hello"), "hello"));
    CHECK(mx_atoi("42") == 42 && mx_atoi("-17") == -17 && mx_atoi("  7z") == 7);
#define SN(...) do { int r1 = mx_snprintf(buf, sizeof buf, __VA_ARGS__), r2 = snprintf(ref, sizeof ref, __VA_ARGS__); \
                     CHECK(r1 == r2 && !strcmp(buf, ref)); if (strcmp(buf, ref)) printf("  [%s] vs [%s]\n", buf, ref); } while (0)
    SN("plain");
    SN("%u.%02u MIPS  %u.%u fps", 12u, 5u, 29u, 9u);
    SN("HDD %c  OV %u/%u%s", 'R', 3u, 422u, " FULL");
    SN("%d %d %d", 0, -5, 2147483647);
    SN("%x %08x %X", 0xbeefu, 0x1234u, 0xabcu);
    SN("%s|%5u|%-4d|", "x", 42u, 7);
    SN("100%%");
    SN("%lu %ld", 4000000000ul, -3l);
    /* 截断: 返回完整长度, 输出以 0 结尾 */
    char small[6]; int r = mx_snprintf(small, sizeof small, "%s", "abcdefgh");
    CHECK(r == 8 && !strcmp(small, "abcde"));
    return tc_done("mxlibc");
}
