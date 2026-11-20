/*
 * 固件 / QEMU 版的最小 libc (没有 newlib): vendor 和 core 用到的那几个。
 * 主机单测 tests/test_mxlibc.c 用 -DMXLIBC_TEST 把名字换成 mx_*, 和主机 libc 对拍。
 * snprintf 只支持 %d %u %x %X %s %c %% 和 l 修饰、宽度、0 / - 标志 (够 core/machine.c 用)。
 */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef MXLIBC_TEST
#define F(n) mx_##n
#else
#define F(n) n
#endif

void *F(memcpy)(void *d, const void *s, size_t n){
    uint8_t *o = d; const uint8_t *i = s;
    if ((((uintptr_t)o | (uintptr_t)i) & 3) == 0){
        for (; n >= 4; n -= 4, o += 4, i += 4) *(uint32_t *)o = *(const uint32_t *)i;
    }
    while (n--) *o++ = *i++;
    return d;
}
void *F(memmove)(void *d, const void *s, size_t n){
    uint8_t *o = d; const uint8_t *i = s;
    if (o <= i || o >= i + n) return F(memcpy)(d, s, n);
    while (n--) o[n] = i[n];
    return d;
}
void *F(memset)(void *d, int c, size_t n){
    uint8_t *o = d, b = (uint8_t)c;
    if (((uintptr_t)o & 3) == 0){
        uint32_t w = b * 0x01010101u;
        for (; n >= 4; n -= 4, o += 4) *(uint32_t *)o = w;
    }
    while (n--) *o++ = b;
    return d;
}
int F(memcmp)(const void *a, const void *b, size_t n){
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}
size_t F(strlen)(const char *s){ const char *p = s; while (*p) p++; return (size_t)(p - s); }
int F(strcmp)(const char *a, const char *b){
    while (*a && *a == *b){ a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
char *F(strcpy)(char *d, const char *s){ char *o = d; while ((*o++ = *s++) != 0) continue; return d; }
long F(atol)(const char *s){
    while (*s == ' ' || *s == '\t') s++;
    int neg = *s == '-'; if (*s == '-' || *s == '+') s++;
    long v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}
int F(atoi)(const char *s){ return (int)F(atol)(s); }

typedef struct { char *o; size_t cap, n; } sbuf;
static void put(sbuf *b, char c){ if (b->n + 1 < b->cap) b->o[b->n] = c; b->n++; }

int F(vsnprintf)(char *o, size_t cap, const char *f, va_list ap){
    sbuf b = { o, cap, 0 };
    for (; *f; f++){
        if (*f != '%'){ put(&b, *f); continue; }
        f++;
        int left = 0, zero = 0, w = 0;
        for (;; f++){ if (*f == '-') left = 1; else if (*f == '0') zero = 1; else break; }
        while (*f >= '0' && *f <= '9') w = w * 10 + (*f++ - '0');
        while (*f == 'l') f++;
        char tmp[12]; const char *s = tmp; int len = 0, neg = 0;
        switch (*f){
        case 'd': case 'u': case 'x': case 'X': {
            uint32_t v;
            if (*f == 'd'){ int32_t x = va_arg(ap, int32_t); neg = x < 0; v = neg ? 0u - (uint32_t)x : (uint32_t)x; }
            else v = va_arg(ap, uint32_t);
            uint32_t base = (*f == 'x' || *f == 'X') ? 16u : 10u;
            const char *dig = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char *p = tmp + sizeof tmp;
            do { *--p = dig[v % base]; v /= base; } while (v);
            if (neg) *--p = '-';
            s = p; len = (int)(tmp + sizeof tmp - p);
            break;
        }
        case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; while (s[len]) len++; break;
        case 'c': tmp[0] = (char)va_arg(ap, int); len = 1; break;
        case '%': tmp[0] = '%'; len = 1; break;
        default: tmp[0] = '%'; len = 1; f--; break;
        }
        int pad = w > len ? w - len : 0;
        if (!left && zero && *f != 's' && *f != 'c'){
            if (neg){ put(&b, '-'); s++; len--; }
            while (pad--) put(&b, '0');
        } else if (!left) while (pad-- > 0) put(&b, ' ');
        for (int i = 0; i < len; i++) put(&b, s[i]);
        if (left) while (pad-- > 0) put(&b, ' ');
    }
    if (cap) o[b.n < cap ? b.n : cap - 1] = 0;
    return (int)b.n;
}
int F(snprintf)(char *o, size_t cap, const char *f, ...){
    va_list ap; va_start(ap, f);
    int r = F(vsnprintf)(o, cap, f, ap);
    va_end(ap);
    return r;
}
