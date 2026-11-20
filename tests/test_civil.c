/* core/civil.h: Unix 秒 -> 年月日时分秒星期 (CMOS 时钟用), 和 libc gmtime_r 对比 */
#include "tcheck.h"
#include "../core/civil.h"
#include <time.h>

int main(void){
    int64_t samples[] = { 0, 951782400 /* 2000-02-29 */, 4102444799LL /* 2099-12-31 23:59:59 */, 1795132800LL, 86399, 978307200 };
    for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; i++){
        time_t t = (time_t)samples[i]; struct tm tm; gmtime_r(&t, &tm);
        civil_t c; civil_from_unix((uint32_t)samples[i], &c);
        CHECK(c.year == tm.tm_year + 1900 && c.mon == tm.tm_mon + 1 && c.mday == tm.tm_mday);
        CHECK(c.hour == tm.tm_hour && c.min == tm.tm_min && c.sec == tm.tm_sec && c.wday == tm.tm_wday);
    }
    for (uint32_t t = 0; t < 4102444800u - 86400u * 37; t += 86400u * 37 + 3601u){   /* 1970..2099 扫一遍 */
        time_t tt = t; struct tm tm; gmtime_r(&tt, &tm);
        civil_t c; civil_from_unix(t, &c);
        CHECK(c.year == tm.tm_year + 1900 && c.mon == tm.tm_mon + 1 && c.mday == tm.tm_mday && c.wday == tm.tm_wday && c.hour == tm.tm_hour);
    }
    return tc_done("civil");
}
