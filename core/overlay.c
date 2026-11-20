/* 写覆盖层, 见 overlay.h */
#include "overlay.h"
#include <string.h>

static uint32_t garena_for(uint32_t ngroups, uint32_t cap){ return cap < ngroups ? cap : ngroups; }

size_t ov_bytes(uint32_t nsect, uint32_t cap){
    uint32_t ng = (nsect + OV_GRP - 1) / OV_GRP;
    size_t grp = ((size_t)ng * 2u + 3u) & ~(size_t)3u;
    return grp + (size_t)garena_for(ng, cap) * OV_GRP * 2u + (size_t)cap * OV_SECT;
}

int ov_init(ov_t *ov, void *mem, size_t memlen, uint32_t nsect, uint32_t cap){
    if (!mem || cap > 0xFFFEu || memlen < ov_bytes(nsect, cap)) return -1;
    uint8_t *m = mem;
    memset(ov, 0, sizeof *ov);
    ov->nsect = nsect;
    ov->ngroups = (nsect + OV_GRP - 1) / OV_GRP;
    ov->cap = cap;
    ov->garena = garena_for(ov->ngroups, cap);
    ov->grp = (uint16_t *)m;
    m += ((size_t)ov->ngroups * 2u + 3u) & ~(size_t)3u;
    ov->arena = (uint16_t *)m;
    m += (size_t)ov->garena * OV_GRP * 2u;
    ov->pool = m;
    ov_reset(ov);
    return 0;
}

void ov_reset(ov_t *ov){
    memset(ov->grp, 0, (size_t)ov->ngroups * 2u);
    ov->used = ov->gused = 0;
}

static uint16_t *slot_of(const ov_t *ov, uint32_t s){
    uint16_t g = ov->grp[s / OV_GRP];
    return g ? &ov->arena[(uint32_t)(g - 1) * OV_GRP + s % OV_GRP] : NULL;
}

const uint8_t *ov_lookup(const ov_t *ov, uint32_t s){
    if (s >= ov->nsect) return NULL;
    uint16_t *sl = slot_of(ov, s);
    return sl && *sl ? ov->pool + (size_t)(*sl - 1) * OV_SECT : NULL;
}

uint32_t ov_need(const ov_t *ov, uint32_t s, uint32_t n){
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) if (!ov_lookup(ov, s + i)) k++;
    return k;
}

int ov_write(ov_t *ov, uint32_t s, const uint8_t *buf){
    if (s >= ov->nsect) return -1;
    uint16_t *sl = slot_of(ov, s);
    if (!sl || !*sl){
        if (ov->used >= ov->cap){ ov->full_rejects++; return -1; }
        if (!sl){
            /* garena = min(ngroups, cap): 槽没满时组也一定没满 */
            uint32_t g = ov->gused++;
            memset(&ov->arena[g * OV_GRP], 0, OV_GRP * 2u);
            ov->grp[s / OV_GRP] = (uint16_t)(g + 1);
            sl = slot_of(ov, s);
        }
        *sl = (uint16_t)(++ov->used);
    }
    memcpy(ov->pool + (size_t)(*sl - 1) * OV_SECT, buf, OV_SECT);
    ov->writes++;
    return 0;
}
