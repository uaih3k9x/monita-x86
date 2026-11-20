/*
 * QEMU 估速外壳: 和固件同一批 .o (core/ + vendor/tiny386), 只换平台层。不连设备、不开窗口。
 * 时间 = 通用定时器 CNTVCT。配 -icount shift=N 时, 每条 ARM 指令推进 2^N ns 虚拟时间, 所以:
 *   虚拟时间 = 设备在 "有效 1000/2^N MIPS" 下会花的时间 (客户机看到的定时器也按这个走);
 *   ARM 指令数 = 虚拟 ns / 2^N (确定的, 和 Mac 快慢无关)。
 * 文件 (bios / vgabios / 盘镜像) 由 run.sh 用 -device loader 预载到固定地址, 大小写在 SIZES。
 * 场景 (SIZES[4]): 0 = FreeDOS 到 "monita-x86 FreeDOS ready"; 1 = Win3.1 进图形模式后再跑 TAIL 秒。
 * 输出: PL011 串口一行一条; 结束时把帧缓冲 (RGB565 800x1280) 用半主机写到 qemu-fb.raw, 半主机退出。
 */
#include "machine.h"
#include "plat.h"
#include "mxblk.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define UART     ((volatile uint32_t *)0x09000000)
#define SIZES    ((volatile uint32_t *)0x40F00000)   /* [0] bios [1] vgabios [2] hda [3] icount shift [4] 场景 [5] 尾巴秒数 [6] 超时秒数 [7] 刷屏间隔 us (0 = 默认 33 ms) */
#define F_BIOS   0x41000000u
#define F_VGA    0x41100000u
#define F_HDA    0x42000000u
#define HEAP0    0x44000000u
#define HEAP1    0x48000000u
#define FB       ((uint16_t *)0x48000000u)

int semi(int op, void *arg);
extern uint32_t __stack_bottom[], __stack_top[];

static void puts_(const char *s){ for (; *s; s++){ if (*s == '\n') *UART = '\r'; *UART = (uint8_t)*s; } }
static void say(const char *fmt, ...);

/* ---- 时间 ---- */
static uint64_t cntvct(void){ uint32_t lo, hi; __asm__ volatile("isb; mrrc p15, 1, %0, %1, c14" : "=r"(lo), "=r"(hi)); return ((uint64_t)hi << 32) | lo; }
static uint32_t cntfrq(void){ uint32_t f; __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(f)); return f; }
static uint64_t g_mul;                               /* 微秒 = 计数 * g_mul >> 24 (A5 没有除法指令, 别在热路径上做 64 位除法) */
static uint64_t now_ns(void){ return (cntvct() * g_mul >> 24) * 1000u; }
uint32_t plat_now_us(void){ return (uint32_t)(cntvct() * g_mul >> 24); }
uint32_t plat_time_utc(void){ return 1795132800u; }  /* 固定 2026-11-20 */
void plat_sleep_us(uint32_t us){ (void)us; }         /* HLT: 空转 (虚拟时间照走) */
void plat_log(const char *m){ puts_("[x86] "); puts_(m); puts_("\n"); }
void plat_abort(void){ puts_("ABORT\n"); struct { uint32_t r, c; } a = { 0x20026, 1 }; semi(0x20, &a); for (;;); }

/* ---- 堆: 只增不减 (vga 换模式时 seen 缓存会还了再要, 漏一点没关系, 16 MB 够) ---- */
static uint32_t g_brk = HEAP0;
void *plat_raw_alloc(size_t n){
    uint32_t p = (g_brk + 7u) & ~7u;
    if (p + n > HEAP1) return NULL;
    g_brk = p + (uint32_t)n;
    return (void *)p;
}
void plat_raw_free(void *p){ (void)p; }

/* ---- 文件: 预载在内存里 ---- */
struct plat_file { const uint8_t *base; uint32_t size; };
static struct plat_file g_files[3];
plat_file *plat_open_ro(const char *path){
    int i = !strcmp(path, "bios.bin") ? 0 : !strcmp(path, "vgabios.bin") ? 1 : !strcmp(path, "hda.img") ? 2 : -1;
    if (i < 0 || !SIZES[i]) return NULL;
    static const uint32_t at[3] = { F_BIOS, F_VGA, F_HDA };
    g_files[i].base = (const uint8_t *)at[i]; g_files[i].size = SIZES[i];
    return &g_files[i];
}
int plat_read_at(plat_file *f, uint32_t off, void *buf, uint32_t n){
    if (off >= f->size) return 0;
    if (n > f->size - off) n = f->size - off;
    memcpy(buf, f->base + off, n);
    return (int)n;
}
int32_t plat_size(plat_file *f){ return (int32_t)f->size; }
void plat_close(plat_file *f){ (void)f; }

/* ---- 输出 ---- */
#include <stdarg.h>
int vsnprintf(char *o, size_t n, const char *f, va_list ap);
static void say(const char *fmt, ...){
    char b[200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); puts_(b);
}
static void dump_fb(const char *name){
    struct { const char *p; uint32_t mode, len; } o = { name, 6 /* "wb" */, (uint32_t)strlen(name) };
    int fd = semi(0x01, &o);
    if (fd < 0){ say("fb dump: open failed\n"); return; }
    struct { int fd; const void *buf; uint32_t n; } w = { fd, FB, MX_FB_W * MX_FB_H * 2u };
    semi(0x05, &w);
    semi(0x02, &fd);
}

static uint8_t ov_mem[256u * 1024u + 4096u] __attribute__((aligned(8)));
static uint8_t rc_mem[16 * 4096] __attribute__((aligned(8)));
static char    txt[8192];

static int has(const char *h, const char *n){
    size_t m = strlen(n);
    for (; *h; h++) if (!memcmp(h, n, m)) return 1;
    return 0;
}

/* ARM 指令数 (百万) = 虚拟 ns / 2^shift / 1e6 */
static uint32_t marm(uint64_t ns, uint32_t sh){ return (uint32_t)((ns >> sh) / 1000000u); }

void qmain(void){
    uint32_t *sp; __asm__ volatile("mov %0, sp" : "=r"(sp));
    for (uint32_t *p = __stack_bottom; p < sp - 16; p++) *p = 0x5a5a5a5au;          /* 栈高水位用 (只涂当前栈顶以下) */
    g_mul = (1000000ull << 24) / cntfrq();
    uint32_t sh = SIZES[3], scen = SIZES[4], tail = SIZES[5], limit = SIZES[6];
    say("qemu-x86: CNTFRQ %u Hz, icount shift %u (= %u 有效 MIPS), scenario %u, sizes %u/%u/%u\n",
        cntfrq(), sh, 1000u >> sh, scen, SIZES[0], SIZES[1], SIZES[2]);

    mx_cfg_t c; memset(&c, 0, sizeof c);
    c.bios = "bios.bin"; c.vgabios = "vgabios.bin"; c.hda = "hda.img";
    c.cpu_gen = 3; c.mem_size = 2u << 20; c.vga_size = 256u << 10;
    c.mem = plat_raw_alloc(c.mem_size); memset(c.mem, 0, c.mem_size);
    c.vga = plat_raw_alloc(c.vga_size); memset(c.vga, 0, c.vga_size);
    c.ov_mem = ov_mem; c.ov_len = sizeof ov_mem;
    c.rc_mem = rc_mem; c.rc_blocks = 16;
    c.fb = FB; c.decor = 1; c.refresh_us = SIZES[7];
    uint64_t t0 = now_ns();
    if (mx_init(&c)){ say("mx_init 失败\n"); plat_abort(); }

    uint64_t hit = 0, last_print = t0;
    for (;;){
        mx_run(100000);
        uint64_t t = now_ns();
        const mx_stats_t *s = mx_stats();
        if (t - last_print >= 1000000000ull){
            last_print = t;
            char line[96]; mx_status_line(line, sizeof line);
            say("t=%u.%us  guest %u M insns  ARM %u M insns  mode %d  | %s\n",
                (uint32_t)((t - t0) / 1000000000ull), (uint32_t)((t - t0) / 100000000ull % 10u),
                s->insns_lo / 1000000u, marm(t - t0, sh), mx_mode(), line);
        }
        if (!hit){
            int ok = scen == 0 ? (mx_text(txt, sizeof txt) > 0 && has(txt, "monita-x86 FreeDOS ready"))
                               : mx_mode() == 2;
            if (ok){
                hit = t;
                say("MILESTONE %s: 虚拟 %u ms, 客户机 %u 条, ARM %u M 条 (%u 条 ARM / 条 x86)\n",
                    scen == 0 ? "FreeDOS ready" : "Win3.1 进图形模式",
                    (uint32_t)((t - t0) / 1000000u), s->insns_lo, marm(t - t0, sh),
                    s->insns_lo ? (uint32_t)(((t - t0) >> sh) / s->insns_lo) : 0u);
            }
        }
        if ((hit && t - hit >= (uint64_t)tail * 1000000000ull) || t - t0 >= (uint64_t)limit * 1000000000ull) break;
    }
    mx_force_refresh();
    uint64_t t = now_ns();
    const mx_stats_t *s = mx_stats();
    uint32_t *p = __stack_bottom; while (p < __stack_top && *p == 0x5a5a5a5au) p++;
    size_t cur, peak; plat_alloc_stats(&cur, &peak);
    say("END %s: 虚拟 %u ms, 客户机 %u 条, ARM %u M 条; 栈高水位 %u B, 小块 %u B (峰值 %u)\n",
        hit ? "ok" : "TIMEOUT", (uint32_t)((t - t0) / 1000000u), s->insns_lo, marm(t - t0, sh),
        (uint32_t)((uintptr_t)__stack_top - 0x1000u - (uintptr_t)p), (uint32_t)cur, (uint32_t)peak);
    dump_fb("qemu-fb.raw");
    struct { uint32_t r, c; } a = { 0x20026, hit ? 0u : 1u };   /* ADP_Stopped_ApplicationExit */
    semi(0x20, &a);
    for (;;);
}
