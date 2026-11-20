/* IDE 块设备 (core/mxblk.c): 只读镜像 + 读缓存 + 写覆盖层, 平台层用内存里的假镜像。
 * 查: 读到的是镜像; 写进覆盖层后读回写的内容, 镜像本身一个字节都没变; 池满整条拒且不留半截。 */
#include "tcheck.h"
#include "../core/mxblk.h"
#include "../core/plat.h"
#include <stdlib.h>
#include <string.h>

#define NSECT 2000
static uint8_t img[NSECT * 512], orig[NSECT * 512];
struct plat_file { int dummy; };
static struct plat_file the_file;
plat_file *plat_open_ro(const char *p){ return strcmp(p, "hda.img") ? NULL : &the_file; }
int plat_read_at(plat_file *f, uint32_t off, void *buf, uint32_t n){
    (void)f; if (off >= sizeof img) return 0;
    if (n > sizeof img - off) n = sizeof img - off;
    memcpy(buf, img + off, n); return (int)n;
}
int32_t plat_size(plat_file *f){ (void)f; return (int32_t)sizeof img; }
void plat_close(plat_file *f){ (void)f; }
void plat_log(const char *m){ (void)m; }

int main(void){
    for (size_t i = 0; i < sizeof img; i++) img[i] = (uint8_t)(i * 13 + (i >> 9));
    memcpy(orig, img, sizeof img);
    uint32_t ov_len = 64 * 1024;                 /* 稀疏表 + 约 120 个扇区 */
    void *ovm = malloc(ov_len); static uint8_t rcm[RC_BLK * 4];
    CHECK(mx_blockdev_open("nope.img") == NULL);
    mx_blockdev_setup(ovm, ov_len, rcm, 4);
    BlockDevice *bs = mx_blockdev_open("hda.img");
    CHECK(bs != NULL);
    CHECK(bs->get_sector_count(bs) == NSECT);
    uint32_t cap = mx_disk.ov.cap;
    CHECK(cap == mx_ov_cap_for(ov_len, NSECT) && cap > 100 && ov_bytes(NSECT, cap) <= ov_len && ov_bytes(NSECT, cap + 1) > ov_len);

    uint8_t buf[512 * 8], w[512 * 8];
    CHECK(bs->read_async(bs, 10, buf, 8, 0, 0) == 0 && memcmp(buf, img + 10 * 512, sizeof buf) == 0);
    CHECK(bs->read_async(bs, NSECT - 1, buf, 2, 0, 0) == -1);          /* 越界 */
    memset(w, 0xAB, sizeof w);
    CHECK(bs->write_async(bs, 12, w, 2, 0, 0) == 0);
    CHECK(bs->read_async(bs, 10, buf, 8, 0, 0) == 0);
    CHECK(memcmp(buf, img + 10 * 512, 1024) == 0);                     /* 10, 11 原样 */
    CHECK(memcmp(buf + 1024, w, 1024) == 0);                           /* 12, 13 是写的 */
    CHECK(memcmp(buf + 2048, img + 14 * 512, 2048) == 0);              /* 14..17 原样 */
    CHECK(memcmp(img, orig, sizeof img) == 0);                         /* 镜像没被写 */

    /* 填满池, 然后一条既有旧扇区又有新扇区的写: 整条拒, 旧扇区也不改 */
    uint32_t s = 100;
    while (mx_disk.ov.used < cap){ CHECK(bs->write_async(bs, s, w, 1, 0, 0) == 0); s += 3; }
    memset(w, 0xCD, 1024);
    CHECK(bs->write_async(bs, 12, w, 2, 0, 0) == 0);                   /* 全是已有扇区: 照常 */
    CHECK(bs->write_async(bs, 13, w, 2, 0, 0) == -1);                  /* 14 是新的 */
    CHECK(mx_disk.rejects == 1);
    CHECK(bs->read_async(bs, 12, buf, 3, 0, 0) == 0 && buf[0] == 0xCD && buf[512] == 0xCD);
    CHECK(memcmp(buf + 1024, img + 14 * 512, 512) == 0);
    CHECK(memcmp(img, orig, sizeof img) == 0);
    CHECK(mx_blockdev_open("hda.img") == NULL);                        /* 只支持一块 */
    free(ovm);
    return tc_done("mxblk");
}
