/* 平台层 core/plat.h 的 Mac host 版 (POSIX) */
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

struct plat_file { FILE *f; };

uint32_t plat_now_us(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000000u + (uint32_t)(ts.tv_nsec / 1000);
}
size_t host_small_bytes;           /* pc_new 里小块分配的总量 (估固件堆用量) */
void *plat_alloc(size_t n){ host_small_bytes += n; return calloc(1, n); }
void plat_free(void *p){ free(p); }
void plat_log(const char *msg){ fprintf(stderr, "[x86] %s\n", msg); }

plat_file *plat_open_ro(const char *path){
    FILE *f = fopen(path, "rb");            /* 只读: host 版也不写盘镜像 */
    if (!f) return NULL;
    plat_file *p = malloc(sizeof *p);
    p->f = f;
    return p;
}
int plat_read_at(plat_file *f, uint32_t off, void *buf, uint32_t n){
    if (fseeko(f->f, (off_t)off, SEEK_SET)) return -1;
    return (int)fread(buf, 1, n, f->f);
}
int32_t plat_size(plat_file *f){
    if (fseeko(f->f, 0, SEEK_END)) return -1;
    return (int32_t)ftello(f->f);
}
void plat_close(plat_file *f){ fclose(f->f); free(f); }
void plat_slice(void){ }
