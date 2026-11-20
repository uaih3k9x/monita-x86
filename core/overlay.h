/*
 * 盘镜像的 RAM 写覆盖层: 客户机写盘只进这里, 不落 flash; 断电即丢, 每次开机都是干净镜像。
 *
 * 二级稀疏表: 扇区按 64 个一组, grp[g] = 0 (这组没写过) 或 1 + 组在 arena 里的序号;
 * 每组 64 个 u16 槽, 0 = 这个扇区没写过, 否则 1 + 扇区在池里的序号。池容量有上限 (cap 个扇区):
 *   已有扇区照常覆盖; 池满时新扇区返回 -1 (ide 回 ABRT), full_rejects 计数。
 * 内存由调用方一次给齐 (ov_bytes 算), 运行中不再分配。纯逻辑, 主机单测 tests/test_overlay.c。
 */
#ifndef MX_OVERLAY_H
#define MX_OVERLAY_H
#include <stddef.h>
#include <stdint.h>

#define OV_SECT 512u
#define OV_GRP  64u

typedef struct {
    uint32_t nsect, ngroups;
    uint16_t *grp;          /* [ngroups] */
    uint16_t *arena;        /* [garena][OV_GRP] */
    uint8_t  *pool;         /* [cap][OV_SECT] */
    uint32_t cap, used, garena, gused;
    uint32_t full_rejects, writes;
} ov_t;

size_t ov_bytes(uint32_t nsect, uint32_t cap);
/* mem 至少 ov_bytes(nsect, cap) 字节 (4 字节对齐)。返回 0 成功, -1 内存不够 / 参数不对 */
int ov_init(ov_t *ov, void *mem, size_t memlen, uint32_t nsect, uint32_t cap);
void ov_reset(ov_t *ov);
/* 覆盖层里的这个扇区 (512 B), 没写过返回 NULL */
const uint8_t *ov_lookup(const ov_t *ov, uint32_t s);
/* [s, s+n) 里有几个扇区还没进覆盖层 (写之前先算, 放不下就整条拒, 不留半截) */
uint32_t ov_need(const ov_t *ov, uint32_t s, uint32_t n);
/* 写一个扇区。0 成功, -1 池满 (新扇区) 或越界 */
int ov_write(ov_t *ov, uint32_t s, const uint8_t *buf);

#endif
