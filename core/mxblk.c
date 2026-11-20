/* IDE 块设备 = 只读镜像 + 读缓存 + RAM 写覆盖层, 见 mxblk.h */
#include "mxblk.h"
#include "plat.h"
#include <string.h>

mx_disk_t mx_disk;
static BlockDevice g_bs;
static void *g_ov_mem, *g_rc_mem;
static uint32_t g_ov_len, g_rc_blocks;

void mx_blockdev_setup(void *ov_mem, uint32_t ov_len, void *rc_mem, uint32_t rc_blocks){
    g_ov_mem = ov_mem; g_ov_len = ov_len; g_rc_mem = rc_mem; g_rc_blocks = rc_blocks;
}

uint32_t mx_ov_cap_for(uint32_t ov_len, uint32_t nsect){
    uint32_t lo = 0, hi = ov_len / OV_SECT;
    if (hi > 0xFFFEu) hi = 0xFFFEu;
    while (lo < hi){                       /* 最大的 cap 使 ov_bytes(nsect, cap) <= ov_len */
        uint32_t mid = lo + (hi - lo + 1) / 2;
        if (ov_bytes(nsect, mid) <= ov_len) lo = mid; else hi = mid - 1;
    }
    return lo;
}

static int disk_read(void *ctx, uint32_t off, void *buf, uint32_t n){
    return plat_read_at((struct plat_file *)ctx, off, buf, n);
}

static int64_t mx_count(BlockDevice *bs){ (void)bs; return mx_disk.nsect; }

static int mx_read(BlockDevice *bs, uint64_t sector, uint8_t *buf, int n,
                   BlockDeviceCompletionFunc *cb, void *opaque){
    (void)bs; (void)cb; (void)opaque;
    mx_disk_t *d = &mx_disk;
    if (sector + (uint64_t)n > d->nsect) return -1;
    d->reads++;
    for (int i = 0; i < n; i++){
        uint32_t s = (uint32_t)sector + (uint32_t)i;
        const uint8_t *o = ov_lookup(&d->ov, s);
        if (o) { memcpy(buf, o, OV_SECT); buf += OV_SECT; continue; }
        /* 连续的、都不在覆盖层里的扇区一次读 */
        int k = 1;
        while (i + k < n && !ov_lookup(&d->ov, s + (uint32_t)k)) k++;
        uint32_t want = (uint32_t)k * OV_SECT;
        if (rc_read(&d->rc, s * OV_SECT, buf, want) != want){ d->read_errs++; return -1; }
        buf += want; i += k - 1;
    }
    return 0;            /* 同步完成 */
}

static int mx_write(BlockDevice *bs, uint64_t sector, const uint8_t *buf, int n,
                    BlockDeviceCompletionFunc *cb, void *opaque){
    (void)bs; (void)cb; (void)opaque;
    mx_disk_t *d = &mx_disk;
    if (sector + (uint64_t)n > d->nsect) return -1;
    /* 放不下就整条拒 (ide 回 ABRT), 不留半截 */
    if (ov_need(&d->ov, (uint32_t)sector, (uint32_t)n) > d->ov.cap - d->ov.used){
        d->rejects++; d->ov.full_rejects++;
        return -1;
    }
    for (int i = 0; i < n; i++) ov_write(&d->ov, (uint32_t)sector + (uint32_t)i, buf + (size_t)i * OV_SECT);
    d->writes++;
    return 0;
}

BlockDevice *mx_blockdev_open(const char *path){
    mx_disk_t *d = &mx_disk;
    if (d->open) return 0;
    d->f = plat_open_ro(path);
    if (!d->f){ plat_log("hda: open failed"); return 0; }
    int32_t sz = plat_size(d->f);
    if (sz < (int32_t)OV_SECT){ plat_close(d->f); d->f = 0; plat_log("hda: bad size"); return 0; }
    d->nsect = (uint32_t)sz / OV_SECT;
    uint32_t cap = g_ov_mem ? mx_ov_cap_for(g_ov_len, d->nsect) : 0;
    if (!g_ov_mem || ov_init(&d->ov, g_ov_mem, g_ov_len, d->nsect, cap) != 0){
        memset(&d->ov, 0, sizeof d->ov);               /* 没给内存: 只读盘, 写一律拒 (nsect 0 = 查不到任何扇区) */
    }
    rc_init(&d->rc, g_rc_mem, g_rc_mem ? g_rc_blocks : 0, disk_read, d->f, (uint32_t)sz);
    memset(&g_bs, 0, sizeof g_bs);
    g_bs.get_sector_count = mx_count;
    g_bs.read_async = mx_read;
    g_bs.write_async = mx_write;
    g_bs.opaque = d;
    d->open = 1;
    return &g_bs;
}
