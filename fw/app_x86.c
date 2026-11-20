/*
 * monita-x86 固件: R05 app 槽里跑 tiny386 (386 无 FPU), 盘镜像和 BIOS 在外挂 flash EFS:/monita/x86/ 下只读打开。
 *
 * 线程模型 (同 Monita app): USB 回调只 release 信号量; 一个 "x86" 任务 (prio 12) 干全部的活:
 *   收链路帧 (fw/xdisp.h: 键盘 / 鼠标进输入环, CTRL / STAT 应答) -> 跑模拟器一个时间片 -> 睡 YIELD_MS 让给内核和基带。
 *   客户机 HLT 时 tiny386 调 usleep -> plat_sleep_us 也睡 1 ms。看门狗每秒喂一次。
 * 显示: 单缓冲, 直接画在原厂 fb (0x80de0000, 不在堆里), 每轮刷屏后对脏区做 D-cache clean。
 * 内存: core/memplan.h 逐级降配 (2 MB -> 1.5 MB 客户机内存), 拿完之后堆里至少还剩 MP_SMALL + 256 KiB, 留不出来就不启动客户机。
 * 写盘: 客户机写只进内存覆盖层 (core/overlay.c), 本固件没有任何写 flash / 写文件的导入 (validate.py --target x86 查)。
 *
 * 逃生口 (同 monita-doom): 上电时按住 "−" (GPIO23, 低有效) = 安全模式, 不挂外挂 flash、不启动客户机, 只应答链路、照常喂狗;
 *   正常开机也先等 BOOT_WAIT_MS 再去碰外挂 flash 和模拟器, 万一后面崩了反复重启, 刷机工具还有窗口。
 *
 * 入口约定 (照原厂): enter 里只配看门狗 + 建任务就返回, 不得阻塞; 任务 prio 12, 7 参数, event_count = 1。
 */
#include "r05_api.h"
#include "r05_fs_ro.h"
#include "lcd.h"
#include "proto.h"
#include "xdisp.h"
#include "x86fw.h"
#include "machine.h"
#include "memplan.h"
#include "mxblk.h"
#include "decor.h"
#include "plat.h"
#include <stdio.h>

#define PORT       R05_PORT_USB_MODEM
#define RX_CHUNK   2048u
#define SLICE_US   15000u        /* 一次跑这么久的客户机 */
#define YIELD_MS   2u            /* 然后睡这么久 (占空比约 88%; 上机按基带表现调) */
#define TASK_STACK 16384u        /* QEMU 实测栈高水位 < 2 KiB */
#define BOOT_WAIT_MS 3000u
#define KEY_GPIO_MINUS 23u       /* 前面板 "−": 脚 13 func4 -> GPIO23, 低有效 (Monita io/keys_hw.h, 实机证实) */

static uint8_t     g_pl[XD_MAX_PAYLOAD] __attribute__((aligned(4)));
static uint8_t     g_rx[RX_CHUNK] __attribute__((aligned(4)));
static void       *g_sem, *g_task;
static lk_parser_t g_p;
static xd_t        g_xd;
static mp_plan_t   g_plan;
static uint32_t    g_yields, g_yields_win, g_win_t, g_ypers, g_booted, g_reset_reason;
extern uint32_t    mx_heap_free_kib;

/* ---- 屏上的字 (开机 / 出错时; 客户机起来以后归 machine.c) ---- */
#define C_FG 0xFFFFu
#define C_BG 0x0000u
#define C_ERR 0xF800u
static int g_line;
static void say(uint16_t fg, const char *s){
    decor_text((uint16_t *)FB_ADDR, LCD_W, 40, 400 + g_line * 20, s, 1, fg, C_BG);
    dcache_clean(FB_ADDR + (400u + (uint32_t)g_line * 20u) * LCD_W * 2u, 16u * LCD_W * 2u);
    g_line++;
}

/* ---- 链路 ---- */
static volatile uint32_t g_rx_ind;                      /* 回调每来一次加一 (主循环不等信号量: 超时 0 的语义没核实) */
static uint32_t g_rx_seen;
static void uart_cb(uint32_t ind, uint32_t port, uint32_t size){
    (void)size;
    if (port == PORT && (ind == R05_UART_IND_RX || ind == R05_UART_IND_RX2)){ g_rx_ind++; ql_rtos_semaphore_release(g_sem); }
}
static void reply(void *ctx, uint8_t type, const uint8_t *pl, uint32_t n){
    (void)ctx;
    uint8_t out[LK_HDR + 96u + LK_TRAIL];
    if (n > 96u) n = 96u;
    ql_uart_write(PORT, out, lk_encode(out, type, g_p.seq, pl, n));
}
static int ctrl(void *ctx, int op, int arg){
    (void)ctx;
    if (!g_booted) return op == X86_OP_STATUSBAR || op == X86_OP_MUTE ? 0 : -1;
    if (op < X86_OP_RESET || op > X86_OP_MUTE) return -1;
    if (op == X86_OP_RESET) g_reset_reason = 1;
    if (op != X86_OP_MUTE) mx_ctrl(op, arg);          /* 没有声卡: 静音是空操作 */
    return 0;
}
static void stat(void *ctx, x86_stat_t *s){
    (void)ctx;
    const mx_stats_t *m = mx_stats();
    s->ver = XD_FW_VER;
    s->flags = (m->paused ? 2u : 0u) | 8u | (g_booted ? 16u : 0u);
    s->kips = m->kips; s->fps_x10 = m->fps_x10;
    s->ov_used = mx_disk.ov.used; s->ov_cap = mx_disk.ov.cap; s->ov_rejects = mx_disk.rejects;
    s->heap_free_kib = mx_heap_free_kib;
    s->mem_kib = g_plan.mem_size >> 10; s->vga_kib = g_plan.vga_size >> 10;
    s->uptime_ms = (uint32_t)(osiUpTimeUS() >> 10);     /* 1.024 ms 单位, 同 Monita ACK */
    s->reset_reason = g_reset_reason;
    s->yields_per_s = g_ypers;
    s->in_dropped = mx_input()->dropped;
    for (int i = 0; i < 32; i++) s->log[i] = x86fw_last_log[i];
}
static void htime(void *ctx, uint32_t unix_s, int tz_min){ (void)ctx; x86fw_set_time(unix_s, tz_min); }

static void link_drain(void){
    for (;;){
        int r = ql_uart_read(PORT, g_rx, RX_CHUNK);
        if (r <= 0) break;
        const uint8_t *d = g_rx; uint32_t n = (uint32_t)r;
        while (n){
            int ev = 0;
            uint32_t k = lk_feed(&g_p, d, n, &ev);
            d += k; n -= k;
            if (ev == LK_EV_FRAME) xd_frame(&g_xd, g_p.type, g_pl, g_p.len);
        }
    }
}
/* 主循环: 不阻塞 */
static void link_poll(void){
    uint32_t n = g_rx_ind;
    if (n == g_rx_seen) return;
    g_rx_seen = n;
    link_drain();
}

static uint32_t g_last_wdt;
static void housekeeping(void){
    uint32_t t = plat_now_us();
    if (t - g_last_wdt > 1000000u){ ql_dev_feed_wdt(); g_last_wdt = t; }
    if (t - g_win_t >= 1000000u){ g_ypers = g_yields - g_yields_win; g_yields_win = g_yields; g_win_t = t; }
}

/* 客户机没法继续 (起不来 / tiny386 abort): 停在这里, 只应答链路 (STAT 能看到原因), 照常喂狗 */
void x86fw_dead(void){
    say(C_ERR, x86fw_last_log);
    for (;;){ ql_rtos_semaphore_wait(g_sem, 20); link_drain(); housekeeping(); }
}

/* ---- 内存: 内核堆 ---- */
static void *k_alloc(void *c, uint32_t n){ (void)c; return malloc(n); }
static void  k_free(void *c, void *p){ (void)c; free(p); }

static void clean_cb(void *ctx, uintptr_t a, uint32_t n){ (void)ctx; dcache_clean((uint32_t)a, n); }

static void task_entry(void *arg){
    (void)arg;
    char line[80];
    int lcd_err = lcd_power_on();                       /* 清屏 + 扫描基址 = 原厂 fb */
    ql_rtos_task_sleep_ms(200);
    if (lcd_err == 0) lcd_backlight(1);
    say(C_FG, "monita-x86: tiny386 on CS80");
    ql_dev_feed_wdt();                                  /* 开机这一串 (清屏 / 挂载 / 分内存 / 读 BIOS) 中间都喂一下 */

    lk_init(&g_p, g_pl, XD_MAX_PAYLOAD);
    g_xd.in = mx_input(); g_xd.reply = reply; g_xd.ctrl = ctrl; g_xd.stat = stat; g_xd.time = htime;
    ql_rtos_semaphore_create(&g_sem, 0);
    ql_uart_open(PORT);
    ql_uart_register_cb(PORT, uart_cb);

    /* 同 extdump (实机两个都返回 0): init 结果不管都去 mount, 同次上电只挂一次。返回值只显示, 能不能用看下面三个文件开不开得了 */
    /* 逃生口: 只配 "−" 这一个键 (同 Monita keys_hw_init 的这一项: 另一个可选脚 121 先设 func1) */
    ql_pin_set_func(121, 1); ql_pin_set_func(13, 4);
    ql_gpio_deinit(KEY_GPIO_MINUS);
    ql_gpio_init(KEY_GPIO_MINUS, GPIO_INPUT, PULL_UP, 1);
    ql_rtos_task_sleep_ms(10);                          /* 等上拉稳定 */
    uint8_t lv = 1;
    if (ql_gpio_get_level(KEY_GPIO_MINUS, &lv) == 0 && lv == 0){
        plat_log("safe mode (- held at power-on)");
        x86fw_dead();
    }
    say(C_FG, "hold - at power-on for safe mode; waiting 3 s");
    for (uint32_t t0 = plat_now_us(); plat_now_us() - t0 < BOOT_WAIT_MS * 1000u;){
        ql_rtos_semaphore_wait(g_sem, 20); link_drain(); housekeeping();
    }

    int r1 = ql_spi6_ext_nor_flash_init();
    int r2 = ql_spi6_ext_nor_flash_sffs_mount();
    snprintf(line, sizeof line, "EFS: init %d mount %d", r1, r2);
    say(r1 == 0 && r2 == 0 ? C_FG : C_ERR, line);
    static const char *const need[3] = { "bios.bin", "vgabios.bin", "hda.img" };
    for (int i = 0; i < 3; i++){
        plat_file *f = plat_open_ro(need[i]);
        int32_t sz = f ? plat_size(f) : -1;
        if (f) plat_close(f);
        snprintf(line, sizeof line, "EFS:/monita/x86/%s: %d B", need[i], (int)sz);
        int ok = i == 0 ? sz == 131072 : i == 1 ? sz > 0 && sz <= 65536 : sz >= 512 * 1024 && (sz & 511) == 0;
        say(ok ? C_FG : C_ERR, line);
        if (!ok){ snprintf(x86fw_last_log, sizeof x86fw_last_log, "bad %s (%d B)", need[i], (int)sz); x86fw_dead(); }
    }

    ql_dev_feed_wdt();
    mp_ops_t ops = { k_alloc, k_free, 0 };
    uint32_t ov_extra = 4096u;                          /* 覆盖层稀疏表 (同 host 版) */
    if (mp_alloc(&ops, ov_extra, &g_plan) != 0){ plat_log("not enough heap"); x86fw_dead(); }
    uint32_t q[8] = { 0 };
    if (ql_dev_memory_size_query(q) == 0) mx_heap_free_kib = q[2] >> 10;   /* {total, avail, max_block} */
    snprintf(line, sizeof line, "mem %uK vga %uK ov %uK rc %uK, heap max block %uK (tier %u)",
             (unsigned)(g_plan.mem_size >> 10), (unsigned)(g_plan.vga_size >> 10), (unsigned)(g_plan.ov_size >> 10),
             (unsigned)(g_plan.rc_size >> 10), (unsigned)mx_heap_free_kib, (unsigned)g_plan.tier);
    say(C_FG, line);

    mx_cfg_t c;
    for (uint32_t i = 0; i < sizeof c; i++) ((uint8_t *)&c)[i] = 0;
    c.bios = "bios.bin"; c.vgabios = "vgabios.bin"; c.hda = "hda.img";
    c.cpu_gen = 3; c.fpu = 0;
    c.mem_size = g_plan.mem_size; c.vga_size = g_plan.vga_size;
    c.mem = g_plan.mem; c.vga = g_plan.vga;
    c.ov_mem = g_plan.ov; c.ov_len = g_plan.ov_size;
    c.rc_mem = g_plan.rc; c.rc_blocks = g_plan.rc_size / 4096u;
    c.fb = (uint16_t *)FB_ADDR; c.clean = clean_cb; c.decor = 1;
    ql_dev_feed_wdt();
    if (mx_init(&c) != 0){ plat_log("mx_init failed (bios / hda?)"); x86fw_dead(); }
    g_booted = 1;

    g_last_wdt = g_win_t = plat_now_us();
    for (;;){
        link_poll();
        mx_run(SLICE_US);
        ql_rtos_task_sleep_ms(YIELD_MS);
        g_yields++;
        housekeeping();
    }
}

int appimg_enter(void *arg){
    (void)arg;
    ql_dev_cfg_wdt(1);                                  /* 同原厂 enter; 任务里每秒喂一次 */
    ql_rtos_task_create(&g_task, TASK_STACK, APP_PRIORITY_NORMAL, "x86", task_entry, 0, 1);
    return 0;
}

void appimg_exit(void){ }
