/*
 * fwsim: 在 QEMU virt 上跑真的 fw/app_x86.c + fw/x86_plat.c (同一批 .o, 链接到设备上的地址), 这 29 个 R05 导入在这里假装。
 * 目的: 上机前把固件胶水 (EFS 路径、内存分档、链路帧、主循环) 跑一遍。不是 R05 内核的模拟, 时序 / 外设都是假的。
 *   堆: 只有 SIM_HEAP 字节 (默认 = 实机 Monita 测到的最大块 4.35 MB), 后进先出才真的还 (memplan 的试探块正好是这样)
 *   EFS: run 脚本用 -device loader 预载的三个文件, 只认 "EFS:/monita/x86/<名字>" + "rb"; 别的路径 / 模式记一笔并失败
 *   USB: 按虚拟时间往接收缓冲里放脚本里的帧 (HELLO / 键盘 / STAT), 设备回的帧在串口上按十六进制打出来
 *   结束: 到 SIZES[6] 秒后导出帧缓冲 (0x80de0000), 半主机退出
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include "proto.h"
#include "x86proto.h"
#include "kbdmap.h"

#define UART   ((volatile uint32_t *)0x09000000)
#define SIZES  ((volatile uint32_t *)0x40F00000)   /* [0..2] 文件大小 [3] icount shift [4] 场景 0 FreeDOS 1 Win3.1 [6] 秒数 [8] 堆字节 (0 = 4.35 MB) */
#define FB     0x80de0000u
int semi(int op, void *arg);
int vsnprintf(char *o, size_t n, const char *f, va_list ap);
int appimg_enter(void *arg);

char *strchr(const char *s, int c){ for (;; s++){ if (*s == (char)c) return (char *)s; if (!*s) return 0; } }   /* kbdmap.h 用 */
static void puts_(const char *s){ for (; *s; s++){ if (*s == '\n') *UART = '\r'; *UART = (uint8_t)*s; } }
static void say(const char *fmt, ...){ char b[200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); puts_(b); }

/* ---- 时间 ---- */
static uint64_t cntvct(void){ uint32_t lo, hi; __asm__ volatile("isb; mrrc p15, 1, %0, %1, c14" : "=r"(lo), "=r"(hi)); return ((uint64_t)hi << 32) | lo; }
static uint64_t g_mul;
int64_t osiUpTimeUS(void){ return (int64_t)(cntvct() * g_mul >> 24); }
static uint32_t now_ms(void){ return (uint32_t)(osiUpTimeUS() / 1000); }

/* ---- 堆 ---- */
#define HEAP0 0x44000000u
static uint32_t g_brk = HEAP0, g_end, g_last, g_peak;
void *malloc(uint32_t n){
    uint32_t p = (g_brk + 7u) & ~7u;
    if (p + n > g_end || p + n < p) return 0;
    g_last = p; g_brk = p + n;
    if (g_brk - HEAP0 > g_peak) g_peak = g_brk - HEAP0;
    return (void *)p;
}
void free(void *p){ if ((uint32_t)p == g_last && p){ g_brk = g_last; g_last = 0; } }   /* 只有最后一块真的还 */
int ql_dev_memory_size_query(uint32_t *q){ q[0] = g_end - HEAP0; q[1] = g_end - g_brk; q[2] = g_end - ((g_brk + 7u) & ~7u); return 0; }

/* ---- 文件 (只读) ---- */
static const struct { const char *path; uint32_t at; int i; } files[3] = {
    { "EFS:/monita/x86/bios.bin", 0x41000000u, 0 }, { "EFS:/monita/x86/vgabios.bin", 0x41100000u, 1 },
    { "EFS:/monita/x86/hda.img", 0x42000000u, 2 } };
static uint32_t g_pos[4]; static int g_bad_fs;
static int mounted;
int ql_spi6_ext_nor_flash_init(void){ return 0; }
int ql_spi6_ext_nor_flash_sffs_mount(void){ if (mounted){ say("SIM: 第二次 mount (实机会返回错误)\n"); g_bad_fs++; } mounted = 1; return 0; }
int ql_fopen(const char *path, const char *mode){
    if (strcmp(mode, "rb")){ say("SIM: fopen 模式不是 rb: %s\n", mode); g_bad_fs++; return -1; }
    for (int i = 0; i < 3; i++) if (!strcmp(path, files[i].path)){ g_pos[i + 1] = 0; return i + 1; }
    say("SIM: fopen 不认识的路径 %s\n", path); g_bad_fs++; return -1;
}
int ql_fsize(int fd){ return (int)SIZES[fd - 1]; }
int ql_fseek(int fd, int32_t off, int32_t wh){ if (wh != 0 || (uint32_t)off > SIZES[fd - 1]) return -1; g_pos[fd] = (uint32_t)off; return 0; }
int ql_fread(void *buf, uint32_t size, uint32_t count, int fd){
    uint32_t n = size * count, sz = SIZES[fd - 1];
    if (g_pos[fd] >= sz) return 0;
    if (n > sz - g_pos[fd]) n = sz - g_pos[fd];
    memcpy(buf, (const uint8_t *)files[fd - 1].at + g_pos[fd], n);
    g_pos[fd] += n;
    return (int)n;
}
int ql_fclose(int fd){ (void)fd; return 0; }

/* ---- 杂项 ---- */
int ql_pin_set_func(uint32_t pin, uint32_t func){ (void)pin; (void)func; return 0; }
int ql_gpio_init(uint32_t g, uint32_t d, uint32_t p, uint32_t l){ (void)g; (void)d; (void)p; (void)l; return 0; }
int ql_gpio_deinit(uint32_t g){ (void)g; return 0; }
int ql_gpio_set_level(uint32_t g, uint32_t l){ if (g == 7) say("SIM: 背光 %u\n", l); return 0; }
int ql_mipi_lcd_init(const void *cfg){ (void)cfg; return 0; }
int ql_mipi_lcd_write_screen(void *fb){ say("SIM: 扫描基址 %08x\n", (uint32_t)fb); return 0; }
int r05_lcd_pm_lock(void){ return 0; }
static uint32_t g_wdt_cfg, g_wdt_feeds, g_wdt_last, g_wdt_gap_max;
int ql_dev_cfg_wdt(uint32_t o){ g_wdt_cfg = o; g_wdt_last = now_ms(); return 0; }
int ql_dev_feed_wdt(void){ uint32_t t = now_ms(); if (t - g_wdt_last > g_wdt_gap_max) g_wdt_gap_max = t - g_wdt_last; g_wdt_last = t; g_wdt_feeds++; return 0; }
int ql_rtos_semaphore_create(void **s, uint32_t init){ (void)init; *s = (void *)1; return 0; }
int ql_rtos_semaphore_release(void *s){ (void)s; return 0; }

/* ---- USB: 按时间放进来的帧 ---- */
typedef void (*uart_cb_t)(uint32_t ind, uint32_t port, uint32_t size);
static uart_cb_t g_ucb;
static uint8_t  g_rxq[4096]; static uint32_t g_rq_h, g_rq_t;
static uint8_t  g_seq;
static void inject(uint8_t type, const uint8_t *pl, uint32_t n){
    uint8_t b[300]; uint32_t k = lk_encode(b, type, ++g_seq, pl, n);
    for (uint32_t i = 0; i < k; i++) g_rxq[(g_rq_h++) & 4095u] = b[i];
    if (g_ucb) g_ucb(0x82060001u, 4, k);
}
static void type_text(const char *s){
    for (; *s; s++){
        uint32_t sc = km_ascii(*s);
        uint8_t f[4], c[4]; uint32_t k = 0;
        if (sc & KM_SHIFT){ f[k] = X86_KF_DOWN; c[k++] = SC_LSHIFT; }
        f[k] = X86_KF_DOWN; c[k++] = (uint8_t)sc; f[k] = 0; c[k++] = (uint8_t)sc;
        if (sc & KM_SHIFT){ f[k] = 0; c[k++] = SC_LSHIFT; }
        uint8_t pl[16]; inject(X86_PC_KBD, pl, x86_kbd_encode(pl, f, c, k));
    }
}
int ql_uart_open(uint32_t port){ (void)port; return 0; }
int ql_uart_register_cb(uint32_t port, uart_cb_t cb){ (void)port; g_ucb = cb; return 0; }
int ql_uart_read(uint32_t port, void *data, uint32_t len){
    (void)port; uint32_t n = 0;
    while (n < len && g_rq_t != g_rq_h) ((uint8_t *)data)[n++] = g_rxq[(g_rq_t++) & 4095u];
    return (int)n;
}
static uint32_t g_replies;
int ql_uart_write(uint32_t port, const void *data, uint32_t len){
    (void)port; const uint8_t *d = data;
    g_replies++;
    say("SIM: 设备回 type %02x len %u", d[2], (uint32_t)(d[4] | (d[5] << 8)));
    if (d[2] == X86_PC_STAT_R){
        x86_stat_t s; x86_stat_decode(d + 6, (uint32_t)(d[4] | (d[5] << 8)), &s);
        s.log[31] = 0;
        say(" STAT: ver %u flags %x kips %u fps %u.%u ov %u/%u heap %uK mem %uK yields/s %u log \"%s\"",
            s.ver, s.flags, s.kips, s.fps_x10 / 10, s.fps_x10 % 10, s.ov_used, s.ov_cap, s.heap_free_kib, s.mem_kib, s.yields_per_s, s.log);
    } else if (d[2] == LK_HELLO_ACK){
        say(" HELLO_ACK: fw %u caps %04x", (uint32_t)(d[8] | (d[9] << 8)), (uint32_t)(d[16] | (d[17] << 8)));
    } else if (d[2] == LK_ACK) say(" ACK: for %02x status %u", d[6], d[7]);
    say("\n");
    return (int)len;
}

/* ---- 脚本 + 结束 ---- */
static int g_step;
static uint32_t g_sleep_ms_total, g_sleeps;
static void dump_and_exit(void){
    say("SIM END: %u ms, 回包 %u, 睡 %u 次共 %u ms, 喂狗 %u 次 (最长间隔 %u ms, cfg %u), 堆峰值 %u B / %u B, 文件系统违规 %d\n",
        now_ms(), g_replies, g_sleeps, g_sleep_ms_total, g_wdt_feeds, g_wdt_gap_max, g_wdt_cfg, g_peak, g_end - HEAP0, g_bad_fs);
    struct { const char *p; uint32_t mode, len; } o = { "fwsim-fb.raw", 6, 12 };
    int fd = semi(0x01, &o);
    struct { int fd; const void *buf; uint32_t n; } w = { fd, (const void *)FB, 800u * 1280u * 2u };
    if (fd >= 0){ semi(0x05, &w); semi(0x02, &fd); }
    struct { uint32_t r, c; } a = { 0x20026, g_bad_fs ? 1u : 0u };
    semi(0x20, &a);
    for (;;);
}
static void script(void){
    uint32_t t = now_ms(), scen = SIZES[4];
    if (g_step == 0 && t > 1000){
        uint8_t h[6]; lk_wr32(h, 1795132800u); lk_wr16(h + 4, 480); inject(LK_HELLO, h, 6); g_step++;
    } else if (g_step == 1 && t > 1500){ inject(X86_PC_STAT, 0, 0); g_step++; }
    else if (g_step == 2 && t > (scen ? 25000u : 12000u)){
        if (scen == 0) type_text("ver\r"); else { uint8_t m[6]; inject(X86_PC_MOUSE, m, x86_mouse_encode(m, 60, 40, 0, 0)); }
        g_step++;
    } else if (g_step == 3 && t > (scen ? 27000u : 15000u)){ inject(X86_PC_STAT, 0, 0); g_step++; }
    if (t >= SIZES[6] * 1000u) dump_and_exit();
}
int ql_rtos_task_sleep_ms(uint32_t ms){
    uint32_t t0 = now_ms();
    g_sleeps++; g_sleep_ms_total += ms;
    while (now_ms() - t0 < ms) { }                     /* 虚拟时间照走 (实机这段时间归内核 / 基带) */
    script();
    return 0;
}
int ql_rtos_semaphore_wait(void *s, uint32_t ms){ (void)s; ql_rtos_task_sleep_ms(ms ? ms : 1); return 0; }
int ql_rtos_task_create(void *t, uint32_t stack, uint8_t prio, const char *name, void (*fn)(void *), void *arg, uint32_t ev){
    say("SIM: task_create stack %u prio %u name %s ev %u\n", stack, prio, name, ev);
    (void)t; fn(arg);                                  /* 单任务: 直接跑, 不返回 */
    return 0;
}

uint32_t cntfrq_(void){ uint32_t f; __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(f)); return f; }
void qmain(void){
    g_mul = (1000000ull << 24) / cntfrq_();
    g_end = HEAP0 + (SIZES[8] ? SIZES[8] : 4350u * 1024u);
    say("fwsim: 堆 %u KiB, 场景 %u\n", (g_end - HEAP0) >> 10, SIZES[4]);
    appimg_enter(0);
    say("SIM: appimg_enter 返回了 (任务没在跑?)\n");
    dump_and_exit();
}
