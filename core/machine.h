/*
 * 一台 PC: tiny386 的 pc.c 加我们的平台层胶水 (HAL: get_uticks / bigmalloc / pcmalloc / load_rom)。
 * host 版 (host/main.c)、固件 (fw/app_x86.c)、QEMU 估速 (qemu/qplat.c) 共用这一份。
 *
 * 设备参数 (和固件一致): 386 (gen 3) 无 FPU, BPP=16 + VGA_STORE_CHANGED, 帧缓冲 800x1280 RGB565 行跨 1600,
 *   640x480 落在 (80, 400), 文本 720x400 落在 (40, 440)。上下空出来的地方画机箱面板 / 状态栏 (decor.c)。
 * 输入: 生产者 (USB 拆帧 / host 脚本) 往 mx_input() 的环里放事件, mx_run 里取; 按键之间至少隔 MX_KEY_GAP_US,
 *   让 i8042 的 E0 前缀延迟 (i8042.c ps2_put_keycode) 来得及走完。
 */
#ifndef MX_MACHINE_H
#define MX_MACHINE_H
#include <stdint.h>
#include <stddef.h>
#include "x86proto.h"
#include "fbdirty.h"

#define MX_FB_W 800
#define MX_FB_H 1280
#define MX_KEY_GAP_US 12000u
#define MX_REFRESH_US 33000u

typedef void (*mx_clean_fn)(void *ctx, uintptr_t addr, uint32_t len);

typedef struct {
    const char *bios, *vgabios, *hda;   /* 路径 (固件: EFS:/monita/x86/...) */
    int cpu_gen, fpu;                   /* 3, 0 */
    uint32_t mem_size, vga_size;
    void *mem, *vga;                    /* 事先拿好的大块 (固件 memplan.h); NULL = plat_alloc */
    void *ov_mem; uint32_t ov_len;      /* 写覆盖层内存 (NULL = 只读盘) */
    void *rc_mem; uint32_t rc_blocks;   /* 读缓存 (4 KiB 块) */
    uint16_t *fb;                       /* MX_FB_W x MX_FB_H RGB565 */
    mx_clean_fn clean; void *clean_ctx; /* 每轮刷屏后对脏区调 (固件: D-cache clean) */
    uint32_t refresh_us;                /* 0 = MX_REFRESH_US */
    int decor;                          /* 画机箱面板 / 状态栏 */
} mx_cfg_t;

typedef struct {
    uint32_t insns_lo;                  /* 客户机累计指令 (低 32 位) */
    uint32_t kips;                      /* 最近 1 s 窗口: 千条 / 秒 */
    uint32_t fps_x10;
    uint32_t refreshes, cleaned_lines;
    uint32_t keys, mouse;
    uint32_t resets;
    int paused, booted;
} mx_stats_t;

int  mx_init(const mx_cfg_t *c);         /* 0 成功 */
/* 跑到 budget_us 用完 (按 plat_now_us), 期间按节拍刷屏、取输入。返回这次跑的客户机指令数 */
uint32_t mx_run(uint32_t budget_us);
in_ring_t *mx_input(void);
void mx_ctrl(int op, int arg);           /* X86_OP_* */
const mx_stats_t *mx_stats(void);
void mx_force_refresh(void);             /* 立刻刷一次 (host 截图前) */
/* 文本模式下把屏上字符按行导出 (每行末尾 '\n'), 返回字节数; 图形模式返回 -1 */
int  mx_text(char *out, int cap);
int  mx_mode(void);                      /* 0 空白 1 文本 2 图形 */
void mx_status_line(char *out, int cap); /* 状态栏那行字 (decor 和 STAT 共用) */

#endif
