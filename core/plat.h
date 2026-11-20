/*
 * 平台抽象层: core/ 和 vendor/tiny386 只通过这几个函数碰平台。
 *   host/host_plat.c  Mac host 版 (POSIX)
 *   fw/x86_plat.c     固件 (R05 ql_* 导入; 文件只读, 只在 EFS:/monita/ 下)
 *   qemu/qplat.c      QEMU 粗估用的裸机版 (文件预先放进内存)
 */
#ifndef MX_PLAT_H
#define MX_PLAT_H
#include <stdint.h>
#include <stddef.h>

uint32_t plat_now_us(void);                 /* 单调微秒, 32 位回绕 (约 71 分钟一次, 只做差用) */
void    *plat_alloc(size_t n);              /* 小块 (外设状态等) */
void     plat_free(void *p);
void     plat_log(const char *msg);         /* 一行诊断 (固件: 内存日志环 + PC_STAT 最后一条) */

/* 只读文件。open 失败返回 NULL; read_at 返回读到的字节数 (<0 出错) */
typedef struct plat_file plat_file;
plat_file *plat_open_ro(const char *path);
int        plat_read_at(plat_file *f, uint32_t off, void *buf, uint32_t n);
int32_t    plat_size(plat_file *f);
void       plat_close(plat_file *f);

/* 模拟循环每个时间片调一次: 固件在这里让出 CPU / 喂狗 / 收链路帧, host 版什么都不做 */
void plat_slice(void);

#endif
