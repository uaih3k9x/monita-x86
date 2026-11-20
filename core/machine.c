/* 一台 PC: pc.c + 平台层胶水, 见 machine.h */
#include "machine.h"
#include "plat.h"
#include "mxblk.h"
#include "decor.h"
#include "pc.h"
#include <stdio.h>
#include <string.h>

static mx_cfg_t   C;
static PC        *g_pc;
static in_ring_t  g_in;
static mx_stats_t S;
static fbd_t      g_dirty;
static int        g_big_n;               /* bigmalloc 第几次 (1 = 客户机内存, 2 = 显存) */
static uint32_t   g_last_refresh, g_last_key, g_win_t, g_win_insn, g_win_frames, g_last_status;
static int        g_decor_dirty, g_statusbar = 1, g_cad_step = -1;
static uint32_t   g_cad_at;
uint32_t mx_heap_free_kib;               /* 固件开机分配完后填 (状态栏 / STAT 用) */

/* ---- tiny386 的平台 HAL (pc.h) ---- */
uint32_t get_uticks(void){ return plat_now_us(); }
void *pcmalloc(long size){ return plat_alloc((size_t)size); }
void *bigmalloc(size_t size){
    g_big_n++;
    if (g_big_n == 1 && C.mem && size <= C.mem_size) return C.mem;
    if (g_big_n == 2 && C.vga && size <= C.vga_size) return C.vga;
    return plat_alloc(size);
}
int load_rom(void *phys_mem, const char *file, uword addr, int backward){
    plat_file *f = plat_open_ro(file);
    if (!f){ plat_log("load_rom: open failed"); return 0; }
    int32_t len = plat_size(f);
    if (len <= 0 || (uint32_t)len > addr + (backward ? 0u : 0x100000u)){ plat_close(f); plat_log("load_rom: bad size"); return 0; }
    uint8_t *dst = (uint8_t *)phys_mem + (backward ? addr - (uword)len : addr);
    int r = plat_read_at(f, 0, dst, (uint32_t)len);
    plat_close(f);
    if (r != len) plat_log("load_rom: short read");
    return len;
}

/* ---- 刷屏 ---- */
static void redraw(void *opaque, int x, int y, int w, int h){
    (void)opaque;
    if (w >= MX_FB_W && h >= MX_FB_H) g_decor_dirty = 1;     /* 换模式时 vga.c 整屏清过 */
    fbd_add(&g_dirty, x, y, w, h, MX_FB_W, MX_FB_H);
}

static void clean_cb(void *ctx, uintptr_t a, uint32_t n){ (void)ctx; if (C.clean) C.clean(C.clean_ctx, a, n); }

static void draw_status(void){
    if (!C.decor) return;
    char line[96];
    mx_status_line(line, sizeof line);
    int y = decor_status(C.fb, MX_FB_W, MX_FB_H, line, g_statusbar);
    fbd_add(&g_dirty, 0, y, MX_FB_W, DECOR_STATUS_H, MX_FB_W, MX_FB_H);
}

static void refresh(int full){
    vga_refresh(g_pc->vga, redraw, NULL, full);
    if (C.decor && g_decor_dirty){
        g_decor_dirty = 0;
        decor_panel(C.fb, MX_FB_W, MX_FB_H);
        fbd_add(&g_dirty, 0, 0, MX_FB_W, MX_FB_H, MX_FB_W, MX_FB_H);
        g_last_status = 0;
    }
    uint32_t t = plat_now_us();
    if (!g_last_status || t - g_last_status >= 500000u){ g_last_status = t | 1u; draw_status(); }
    if (!fbd_empty(&g_dirty)){
        S.cleaned_lines += (uint32_t)(g_dirty.y1 - g_dirty.y0);
        fbd_flush(&g_dirty, (uintptr_t)C.fb, MX_FB_W * 2u, 2u, MX_FB_W, clean_cb, 0);
    }
    S.refreshes++; g_win_frames++;
}

int mx_init(const mx_cfg_t *c){
    C = *c;
    if (!C.refresh_us) C.refresh_us = MX_REFRESH_US;
    memset(&S, 0, sizeof S); memset(&g_in, 0, sizeof g_in);
    fbd_clear(&g_dirty);
    mx_blockdev_setup(C.ov_mem, C.ov_len, C.rc_mem, C.rc_blocks);
    PCConfig conf;
    memset(&conf, 0, sizeof conf);
    conf.bios = C.bios; conf.vga_bios = C.vgabios;
    conf.mem_size = (long)C.mem_size; conf.vga_mem_size = (long)C.vga_size;
    conf.disks[0] = C.hda;
    conf.width = MX_FB_W; conf.height = MX_FB_H;
    conf.cpu_gen = C.cpu_gen ? C.cpu_gen : 3; conf.fpu = C.fpu;
    if (C.decor){ memset(C.fb, 0, (size_t)MX_FB_W * MX_FB_H * 2u); g_decor_dirty = 1; }
    g_pc = pc_new(redraw, NULL, (u8 *)C.fb, &conf);
    if (!g_pc) return -1;
    load_bios_and_reset(g_pc);
    g_pc->boot_start_time = get_uticks();
    g_last_refresh = g_win_t = plat_now_us();
    S.booted = 1;
    return 0;
}

in_ring_t *mx_input(void){ return &g_in; }
const mx_stats_t *mx_stats(void){ return &S; }
void mx_force_refresh(void){ refresh(1); }
int mx_mode(void){ return g_pc ? vga_monita_info(g_pc->vga, 0, 0, 0) : 0; }

static void key(uint8_t code, int down, int e0){
    ps2_put_keycode(g_pc->kbd, down, e0 ? (0xE000 | code) : code);
    S.keys++;
}

/* Ctrl+Alt+Del: 6 个键, 同样按节拍发 */
static const struct { uint8_t code, down, e0; } cad_seq[6] = {
    { 0x1D, 1, 0 }, { 0x38, 1, 0 }, { 0x53, 1, 1 }, { 0x53, 0, 1 }, { 0x38, 0, 0 }, { 0x1D, 0, 0 },
};

static void input_poll(uint32_t now){
    if (g_cad_step >= 0){
        if (now - g_cad_at < MX_KEY_GAP_US) return;
        key(cad_seq[g_cad_step].code, cad_seq[g_cad_step].down, cad_seq[g_cad_step].e0);
        g_cad_at = now;
        if (++g_cad_step == 6) g_cad_step = -1;
        return;
    }
    in_ev_t e;
    for (;;){
        uint32_t t = g_in.tail;
        if (t == g_in.head) return;
        e = g_in.ev[t & (IN_RING - 1)];
        if (e.kind == IN_KEY){
            if (now - g_last_key < MX_KEY_GAP_US) return;     /* 留在环里, 下一轮再取 */
            in_pop(&g_in, &e);
            key(e.code, e.flags & X86_KF_DOWN, (e.flags & X86_KF_E0) != 0);
            g_last_key = now;
            return;
        }
        in_pop(&g_in, &e);
        if (e.kind == IN_MOUSE){ ps2_mouse_event(g_pc->mouse, e.dx, e.dy, e.dz, e.flags); S.mouse++; }
    }
}

void mx_ctrl(int op, int arg){
    switch (op){
    case X86_OP_RESET:
        if (arg & 1) ov_reset(&mx_disk.ov);
        g_pc->reset_request = 1; S.resets++;
        break;
    case X86_OP_CAD: g_cad_step = 0; g_cad_at = plat_now_us() - MX_KEY_GAP_US; break;
    case X86_OP_PAUSE: S.paused = !S.paused; break;
    case X86_OP_STATUSBAR: g_statusbar = !g_statusbar; g_last_status = 0; break;
    }
}

uint32_t mx_run(uint32_t budget_us){
    uint32_t t0 = plat_now_us(), i0 = (uint32_t)cpui386_get_cycle(g_pc->cpu), now = t0;
    do {
        input_poll(now);
        if (!S.paused){
            pc_step(g_pc);
            if (vga_step(g_pc->vga)) { /* 回扫相位由客户机轮询 3DA 看; 刷屏按我们自己的节拍 */ }
        }
        now = plat_now_us();
        if (now - g_last_refresh >= C.refresh_us){
            g_last_refresh = now;
            refresh(0);
        }
    } while (now - t0 < budget_us);
    uint32_t i1 = (uint32_t)cpui386_get_cycle(g_pc->cpu);
    S.insns_lo = i1;
    if (now - g_win_t >= 1000000u){
        uint32_t dt = now - g_win_t;
        S.kips = (uint32_t)((uint64_t)(i1 - g_win_insn) * 1000u / dt);
        S.fps_x10 = (uint32_t)((uint64_t)g_win_frames * 10000000u / dt);
        g_win_t = now; g_win_insn = i1; g_win_frames = 0;
    }
    return i1 - i0;
}

void mx_status_line(char *out, int cap){
    static uint32_t last_r, last_w;
    char led = mx_disk.writes != last_w ? 'W' : mx_disk.reads != last_r ? 'R' : '-';
    last_r = mx_disk.reads; last_w = mx_disk.writes;
    snprintf(out, (size_t)cap, "HDD %c  OV %u/%u%s  %u.%02u MIPS  %u.%u fps  heap %uK%s",
             led, (unsigned)mx_disk.ov.used, (unsigned)mx_disk.ov.cap, mx_disk.rejects ? " FULL" : "",
             (unsigned)(S.kips / 1000u), (unsigned)(S.kips % 1000u / 10u),
             (unsigned)(S.fps_x10 / 10u), (unsigned)(S.fps_x10 % 10u),
             (unsigned)mx_heap_free_kib, S.paused ? "  PAUSED" : "");
}

int mx_text(char *out, int cap){
    int cols, rows; uint32_t start;
    if (vga_monita_info(g_pc->vga, &cols, &rows, &start) != 1) return -1;
    if (cols <= 0 || cols > 132 || rows <= 0 || rows > 60) return -1;
    int n = 0;
    for (int r = 0; r < rows; r++){
        int end = n;
        for (int c = 0; c < cols && n < cap - 2; c++){
            uint8_t ch = (uint8_t)vga_monita_cell(g_pc->vga, start + (uint32_t)(r * cols + c));
            out[n++] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : (ch ? '.' : ' ');
            if (out[n - 1] != ' ') end = n;
        }
        n = end;                                   /* 去掉行尾空格 */
        if (n < cap - 1) out[n++] = '\n';
    }
    out[n < cap ? n : cap - 1] = 0;
    return n;
}
