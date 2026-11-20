/* 编 vendor/tiny386 时 -include 进来: 它直接调的 malloc / free / strdup 都改走 plat_alloc (记账, 固件里就是内核堆) */
#ifndef MX_VENDOR_H
#define MX_VENDOR_H
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#define malloc(n) plat_alloc(n)
#define free(p)   plat_free(p)
#define strdup(s) plat_strdup(s)
#endif
