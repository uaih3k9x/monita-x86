/*
 * core/plat.h 的固件版 (R05)。文件只读, 只开 EFS:/monita/x86/ 下面的固定几个名字, 模式写死 "rb"。
 * 不包含 <stdlib.h>: r05_api.h 里的 malloc 原型和 newlib 的不一样。
 */
#include "r05_api.h"
#include "r05_fs_ro.h"
#include "plat.h"
#include "x86fw.h"

#define DIR_PREFIX "EFS:/monita/x86/"

/* ---- 时间 ---- */
uint32_t plat_now_us(void){ return (uint32_t)osiUpTimeUS(); }

/* 墙钟: 主机 HELLO 带了时间就用它 (当地时间, CMOS 里放当地时间, DOS 习惯); 没有就用编译时刻 */
#ifndef MX_BUILD_EPOCH
#define MX_BUILD_EPOCH 1795132800u
#endif
static uint32_t g_t_base = MX_BUILD_EPOCH, g_t_at_s;
static uint32_t uptime_s(void){ return (uint32_t)((uint64_t)osiUpTimeUS() / 1000000u); }
void x86fw_set_time(uint32_t unix_s, int tz_min){ g_t_base = unix_s + (uint32_t)(tz_min * 60); g_t_at_s = uptime_s(); }
uint32_t plat_time_utc(void){ return g_t_base + (uptime_s() - g_t_at_s); }

/* ---- 内存 ---- */
void *plat_raw_alloc(size_t n){ return malloc((uint32_t)n); }
void plat_raw_free(void *p){ if (p) free(p); }

/* ---- 日志: 最近一条 (PC_STAT 带回主机) ---- */
char x86fw_last_log[32];
void plat_log(const char *m){
    uint32_t i = 0;
    for (; m[i] && i < sizeof x86fw_last_log - 1; i++) x86fw_last_log[i] = m[i];
    for (; i < sizeof x86fw_last_log; i++) x86fw_last_log[i] = 0;
}

/* ---- 文件 ---- */
struct plat_file { QFILE fd; int32_t size; uint32_t pos; };
static struct plat_file g_files[3];

plat_file *plat_open_ro(const char *name){
    static const char *const names[3] = { "bios.bin", "vgabios.bin", "hda.img" };
    int i = 0;
    for (; i < 3; i++){
        const char *a = names[i], *b = name;
        while (*a && *a == *b){ a++; b++; }
        if (!*a && !*b) break;
    }
    if (i == 3) return 0;                          /* 只认这三个名字, 拼不出别的路径 */
    char path[48];
    uint32_t n = 0;
    for (const char *s = DIR_PREFIX; *s; s++) path[n++] = *s;
    for (const char *s = names[i]; *s; s++) path[n++] = *s;
    path[n] = 0;
    QFILE fd = ql_fopen(path, "rb");
    if (fd <= 0){ plat_log("open failed"); return 0; }
    g_files[i].fd = fd; g_files[i].size = ql_fsize(fd); g_files[i].pos = 0;
    return &g_files[i];
}
int plat_read_at(plat_file *f, uint32_t off, void *buf, uint32_t n){
    if (f->pos != off){
        if (ql_fseek(f->fd, (int32_t)off, QL_SEEK_SET) != 0){ f->pos = 0xffffffffu; return -1; }
        f->pos = off;
    }
    int r = ql_fread(buf, 1, n, f->fd);
    f->pos = r > 0 ? off + (uint32_t)r : 0xffffffffu;
    return r;
}
int32_t plat_size(plat_file *f){ return f->size; }
void plat_close(plat_file *f){ ql_fclose(f->fd); f->fd = 0; }

/* ---- 让出 CPU ---- */
void plat_sleep_us(uint32_t us){ (void)us; ql_rtos_task_sleep_ms(1); }   /* 客户机 HLT: 让给基带 / 内核 */
void plat_abort(void){ plat_log("tiny386 abort"); x86fw_dead(); }
