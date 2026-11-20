/*
 * monita-x86 的 Mac host 版: 和固件同一份 core/ + vendor/tiny386, 设备同款参数 (386 无 FPU, 800x1280 RGB565,
 * 客户机内存 / 覆盖层池按固件档位), 不开窗口: 帧导出 PNG / PPM, 文本模式导出 TXT, 键盘鼠标由脚本注入。
 *
 *   x86host [-bios bios.bin] [-vgabios vgabios.bin] -hda hda.img [-mem 2M] [-ov 256] [-gen 3]
 *           [-script run.txt] [-snap out.png] [-secs N] [-no-decor] [-q]
 *
 * 脚本 (一行一条, # 注释; 字符串用双引号, 支持 \n \" \\):
 *   run <ms>                   跑 ms 毫秒 (墙钟)
 *   until-text "<串>" <ms>      跑到文本屏出现这个串, 超时就失败 (退出码 1)
 *   expect-text "<串>"          现在的文本屏里必须有
 *   until-mode <0|1|2> <ms>     跑到显示模式变成这个 (0 空白 1 文本 2 图形), 超时就失败
 *   expect-clean                再跑一个刷屏周期, 然后影子缓冲 (只收 clean 回调报过的字节, 同固件的 D-cache clean)
 *                               必须和帧缓冲逐字节相同; 不同 = 有像素改了没报脏区, 设备上会留残影
 *   type "<串>"                 按 ASCII 打字 (美式键盘, 大写自动按 Shift)
 *   key <名> [<名> ...]         组合键: 依次按下, 倒序松开 (名见 core/kbdmap.h: enter esc f1 up del ctrl alt ...)
 *   mouse <dx> <dy> [<按键>]    相对移动 (按键 bit0 左 bit1 右 bit2 中)
 *   ctrl <op> [<arg>]           同 PC_CTRL (1 复位 2 Ctrl+Alt+Del 3 暂停 4 状态栏)
 *   snap <文件.png|.ppm>        存一帧
 *   text <文件.txt>             存文本屏
 *   log "<串>"                  打印一行
 *   stats                      打印 MIPS / 帧率 / 覆盖层
 */
#include "machine.h"
#include "plat.h"
#include "mxblk.h"
#include "kbdmap.h"
#include "imgout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t g_fb[MX_FB_W * MX_FB_H];
static uint16_t g_shadow[MX_FB_W * MX_FB_H];     /* 设备上 LCD 真正看到的: 只有 clean 过的字节 */
static void shadow_clean(void *ctx, uintptr_t a, uint32_t n){
    (void)ctx;
    uintptr_t off = a - (uintptr_t)g_fb;
    memcpy((uint8_t *)g_shadow + off, (const uint8_t *)g_fb + off, n);
}
static int g_quiet;

static uint32_t parse_size(const char *s){
    char *e; double v = strtod(s, &e);
    if (*e == 'M' || *e == 'm') v *= 1024 * 1024; else if (*e == 'K' || *e == 'k') v *= 1024;
    return (uint32_t)v;
}

/* 往输入环里放事件; 环满就先跑一会儿让模拟器取走 */
static void push(const in_ev_t *e){
    in_ring_t *r = mx_input();
    while (r->head - r->tail >= IN_RING) mx_run(5000);
    in_push(r, e);
}
static void push_key(uint32_t sc, int down){
    in_ev_t e = { IN_KEY, (uint8_t)((down ? X86_KF_DOWN : 0) | ((sc & KM_E0) ? X86_KF_E0 : 0)), (uint8_t)sc, 0, 0, 0 };
    push(&e);
}
static void type_str(const char *s){
    for (; *s; s++){
        uint32_t sc = km_ascii(*s);
        if (!sc){ fprintf(stderr, "type: 不认识的字符 0x%02x\n", (unsigned char)*s); continue; }
        if (sc & KM_SHIFT) push_key(SC_LSHIFT, 1);
        push_key(sc, 1); push_key(sc, 0);
        if (sc & KM_SHIFT) push_key(SC_LSHIFT, 0);
    }
}
/* 等输入环排空 (按键有节拍) */
static void drain(void){ in_ring_t *r = mx_input(); while (r->head != r->tail) mx_run(5000); mx_run(30000); }

static int text_has(const char *needle){
    static char buf[16384];
    if (mx_text(buf, sizeof buf) < 0) return 0;
    return strstr(buf, needle) != NULL;
}

static int snap(const char *path){
    mx_force_refresh();
    size_t n = strlen(path);
    int r = (n > 4 && !strcmp(path + n - 4, ".ppm")) ? img_write_ppm(path, g_fb, MX_FB_W, MX_FB_H, MX_FB_W)
                                                     : img_write_png(path, g_fb, MX_FB_W, MX_FB_H, MX_FB_W);
    if (!g_quiet) printf("snap %s (fb crc32 %08x)\n", path, img_crc32(0, (const uint8_t *)g_fb, sizeof g_fb));
    return r;
}

static void stats(void){
    const mx_stats_t *s = mx_stats();
    char line[128]; mx_status_line(line, sizeof line);
    size_t cur, peak; plat_alloc_stats(&cur, &peak);
    printf("stats: insns %u  %s  keys %u mouse %u  ov %u/%u rejects %u  reads %u writes %u  rc hit %u miss %u  small %zu peak %zu\n",
           s->insns_lo, line, s->keys, s->mouse, mx_disk.ov.used, mx_disk.ov.cap, mx_disk.rejects,
           mx_disk.reads, mx_disk.writes, mx_disk.rc.hits, mx_disk.rc.misses, cur, peak);
}

/* 取一个参数: 双引号串 (带转义) 或到空白为止 */
static char *arg(char **pp){
    char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return NULL;
    char *out = p, *w = p;
    if (*p == '"'){
        p++;
        while (*p && *p != '"'){
            if (*p == '\\' && p[1]){ p++; *w++ = *p == 'n' ? '\n' : *p; p++; }
            else *w++ = *p++;
        }
        if (*p) p++;
    } else {
        while (*p && *p != ' ' && *p != '\t') *w++ = *p++;
        if (*p) p++;
    }
    *w = 0; *pp = p;
    return out;
}

static int run_script(const char *path){
    FILE *f = fopen(path, "r");
    if (!f){ perror(path); return 1; }
    char line[1024]; int ln = 0, rc = 0;
    while (!rc && fgets(line, sizeof line, f)){
        ln++;
        line[strcspn(line, "\r\n")] = 0;
        char *p = line, *cmd = arg(&p);
        if (!cmd || cmd[0] == '#') continue;
        if (!strcmp(cmd, "run")){
            uint32_t ms = (uint32_t)atoi(arg(&p));
            uint32_t t0 = plat_now_us();
            while (plat_now_us() - t0 < ms * 1000u) mx_run(10000);
        } else if (!strcmp(cmd, "until-text")){
            char *s = arg(&p); uint32_t ms = (uint32_t)atoi(arg(&p)), t0 = plat_now_us();
            int ok = 0;
            while (plat_now_us() - t0 < ms * 1000u){ mx_run(50000); if (text_has(s)){ ok = 1; break; } }
            printf("%s until-text \"%s\" (%u ms)\n", ok ? "ok  " : "FAIL", s, (plat_now_us() - t0) / 1000u);
            if (!ok) rc = 1;
        } else if (!strcmp(cmd, "until-mode")){
            int m = atoi(arg(&p)); uint32_t ms = (uint32_t)atoi(arg(&p)), t0 = plat_now_us();
            int ok = 0;
            while (plat_now_us() - t0 < ms * 1000u){ mx_run(50000); if (mx_mode() == m){ ok = 1; break; } }
            printf("%s until-mode %d (%u ms)\n", ok ? "ok  " : "FAIL", m, (plat_now_us() - t0) / 1000u);
            if (!ok) rc = 1;
        } else if (!strcmp(cmd, "expect-clean")){
            mx_run(80000);                                       /* 至少一个刷屏周期 (33 ms), 不强制刷新 */
            uint32_t bad = 0, first = 0;
            for (uint32_t i = 0; i < MX_FB_W * MX_FB_H; i++) if (g_fb[i] != g_shadow[i]){ if (!bad) first = i; bad++; }
            printf("%s expect-clean (%u 个像素没 clean%s", bad ? "FAIL" : "ok  ", bad, bad ? "" : ")\n");
            if (bad){ printf(", 第一个在 x %u y %u)\n", first % MX_FB_W, first / MX_FB_W); rc = 1; }
        } else if (!strcmp(cmd, "expect-text")){
            char *s = arg(&p); int ok = text_has(s);
            printf("%s expect-text \"%s\"\n", ok ? "ok  " : "FAIL", s);
            if (!ok) rc = 1;
        } else if (!strcmp(cmd, "type")){
            type_str(arg(&p)); drain();
        } else if (!strcmp(cmd, "key")){
            uint32_t sc[8]; int n = 0; char *a;
            while (n < 8 && (a = arg(&p))){ sc[n] = km_name(a); if (!sc[n]) fprintf(stderr, "line %d: 不认识的键 %s\n", ln, a); else n++; }
            for (int i = 0; i < n; i++) push_key(sc[i], 1);
            for (int i = n - 1; i >= 0; i--) push_key(sc[i], 0);
            drain();
        } else if (!strcmp(cmd, "mouse")){
            int dx = atoi(arg(&p)), dy = atoi(arg(&p)); char *b = arg(&p);
            in_ev_t e = { IN_MOUSE, (uint8_t)(b ? atoi(b) : 0), 0, 0, (int16_t)dx, (int16_t)dy };
            push(&e); mx_run(20000);
        } else if (!strcmp(cmd, "ctrl")){
            int op = atoi(arg(&p)); char *a = arg(&p);
            mx_ctrl(op, a ? atoi(a) : 0);
        } else if (!strcmp(cmd, "snap")){
            if (snap(arg(&p))) rc = 1;
        } else if (!strcmp(cmd, "text")){
            static char buf[16384]; char *fn = arg(&p);
            int n = mx_text(buf, sizeof buf);
            FILE *o = fopen(fn, "w");
            if (o){ if (n > 0) fwrite(buf, 1, (size_t)n, o); fclose(o); }
            if (!g_quiet) printf("text %s (%d bytes, mode %d)\n", fn, n, mx_mode());
        } else if (!strcmp(cmd, "log")){
            printf("log: %s\n", arg(&p));
        } else if (!strcmp(cmd, "stats")){
            stats();
        } else { fprintf(stderr, "line %d: 不认识的命令 %s\n", ln, cmd); rc = 2; }
    }
    fclose(f);
    return rc;
}

int main(int argc, char **argv){
    mx_cfg_t c; memset(&c, 0, sizeof c);
    c.bios = "bios.bin"; c.vgabios = "vgabios.bin";
    c.cpu_gen = 3; c.fpu = 0; c.mem_size = 2u << 20; c.vga_size = 256u << 10; c.decor = 1;
    uint32_t ov_kib = 256, secs = 0;
    const char *script = NULL, *snap_path = NULL;
    for (int i = 1; i < argc; i++){
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "-bios") && v){ c.bios = v; i++; }
        else if (!strcmp(a, "-vgabios") && v){ c.vgabios = v; i++; }
        else if (!strcmp(a, "-hda") && v){ c.hda = v; i++; }
        else if (!strcmp(a, "-mem") && v){ c.mem_size = parse_size(v); i++; }
        else if (!strcmp(a, "-ov") && v){ ov_kib = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "-gen") && v){ c.cpu_gen = atoi(v); i++; }
        else if (!strcmp(a, "-script") && v){ script = v; i++; }
        else if (!strcmp(a, "-snap") && v){ snap_path = v; i++; }
        else if (!strcmp(a, "-secs") && v){ secs = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "-no-decor")) c.decor = 0;
        else if (!strcmp(a, "-q")) g_quiet = 1;
        else { fprintf(stderr, "用法见 host/main.c 文件头\n"); return 2; }
    }
    if (!c.hda){ fprintf(stderr, "要 -hda\n"); return 2; }
    c.fb = g_fb; c.clean = shadow_clean;
    c.mem = calloc(1, c.mem_size); c.vga = calloc(1, c.vga_size);   /* 同固件: 大块事先拿好, plat_alloc 只记小块 */
    c.ov_len = ov_kib * 1024u + 4096u;            /* 池 + 稀疏表 (同固件: memplan 的 extra_ov) */
    c.ov_mem = ov_kib ? malloc(c.ov_len) : NULL;
    if (!c.ov_mem) c.ov_len = 0;
    static uint8_t rc_mem[16 * 4096];
    c.rc_mem = rc_mem; c.rc_blocks = 16;
    if (mx_init(&c)){ fprintf(stderr, "mx_init 失败\n"); return 1; }
    size_t cur; plat_alloc_stats(&cur, NULL);
    if (!g_quiet) printf("x86host: mem %uK vga %uK ov %u sectors, small allocs %zu B\n",
                         c.mem_size >> 10, c.vga_size >> 10, mx_disk.ov.cap, cur);
    int rc = 0;
    if (script) rc = run_script(script);
    if (secs){ uint32_t t0 = plat_now_us(); while (plat_now_us() - t0 < secs * 1000000u) mx_run(10000); }
    if (snap_path) snap(snap_path);
    if (!g_quiet) stats();
    return rc;
}
