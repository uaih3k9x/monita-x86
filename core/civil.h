/* Unix 秒 (UTC) -> 公历日期, 不用 libc (固件没有 gmtime_r)。日期算法: H. Hinnant civil_from_days。 */
#ifndef MX_CIVIL_H
#define MX_CIVIL_H
#include <stdint.h>

typedef struct { int year, mon, mday, hour, min, sec, wday; } civil_t;   /* mon 1..12, wday 0 = 周日 */

static inline void civil_from_unix(uint32_t t, civil_t *c){
    uint32_t days = t / 86400u, s = t % 86400u;
    c->hour = (int)(s / 3600u); c->min = (int)(s / 60u % 60u); c->sec = (int)(s % 60u);
    c->wday = (int)((days + 4u) % 7u);                       /* 1970-01-01 是周四 */
    uint32_t z = days + 719468u, era = z / 146097u, doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u), mp = (5u * doy + 2u) / 153u;
    c->mday = (int)(doy - (153u * mp + 2u) / 5u + 1u);
    c->mon = (int)(mp < 10u ? mp + 3u : mp - 9u);
    c->year = (int)(yoe + era * 400u) + (c->mon <= 2);
}
#endif
