/*
 * IDE 盘的块设备: 镜像只读打开 (plat_open_ro), 读走 rcache, 写只进 RAM 写覆盖层 (overlay.h)。
 * vendor/tiny386/ide.c 在 MONITA_X86 下用这里的 struct BlockDevice (和它原来的定义逐字段相同) 和 mx_blockdev_open。
 * 覆盖层 / 读缓存的内存由 machine.c 事先给 (mx_blockdev_setup), 不在这里分配大块。
 */
#ifndef MX_MXBLK_H
#define MX_MXBLK_H
#include <stdint.h>
#include "overlay.h"
#include "rcache.h"

typedef struct BlockDevice BlockDevice;
typedef void BlockDeviceCompletionFunc(void *opaque, int ret);
struct BlockDevice {
    int64_t (*get_sector_count)(BlockDevice *bs);
    int (*get_chs)(BlockDevice *bs, int *cylinders, int *heads, int *sectors);
    int (*read_async)(BlockDevice *bs,
                      uint64_t sector_num, uint8_t *buf, int n,
                      BlockDeviceCompletionFunc *cb, void *opaque);
    int (*write_async)(BlockDevice *bs,
                       uint64_t sector_num, const uint8_t *buf, int n,
                       BlockDeviceCompletionFunc *cb, void *opaque);
    void *opaque;
};

typedef struct {
    ov_t ov; rc_t rc;
    struct plat_file *f;
    uint32_t nsect;
    uint32_t reads, writes, rejects, read_errs;
    int open;
} mx_disk_t;

extern mx_disk_t mx_disk;     /* 只支持一块盘 (hda) */

/* 打开前先给内存: ov_mem/ov_len = 覆盖层 (稀疏表 + 池), rc_mem = rc_blocks * RC_BLK。
 * ov_len 按镜像大小在打开时切 (池扇区数 = 能放下的最大值)。 */
void mx_blockdev_setup(void *ov_mem, uint32_t ov_len, void *rc_mem, uint32_t rc_blocks);
BlockDevice *mx_blockdev_open(const char *path);
/* 池扇区数: 给定内存和盘扇区数, 稀疏表之外能放多少扇区 */
uint32_t mx_ov_cap_for(uint32_t ov_len, uint32_t nsect);

#endif
