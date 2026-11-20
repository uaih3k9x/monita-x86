/* 编 vendor/tiny386 时 -include 进来: 它直接调的 malloc / free / strdup 都改走 plat_alloc (记账, 固件里就是内核堆) */
#ifndef MX_VENDOR_H
#define MX_VENDOR_H
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#define malloc(n) plat_alloc(n)
#define free(p)   plat_free(p)
#define strdup(s) plat_strdup(s)

#ifdef MONITA_FW
/* 固件 / QEMU 版没有 stdio: 调试打印全部丢掉, HLT 时的 usleep 改成让出 CPU, abort 停客户机 (不重启设备) */
#include <stdio.h>
#include <unistd.h>          /* 先让它声明完, 下面再改名 */
#define fprintf(...) ((void)0)
#define printf(...)  ((void)0)
#define putchar(c)   ((void)(c))
#define fflush(f)    ((void)0)
#define usleep(n)    plat_sleep_us(n)
#define abort()      plat_abort()
#endif
#endif
