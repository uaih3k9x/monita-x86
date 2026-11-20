/* x86 链路帧编解码 + 输入环 (core/x86proto.h), 外加和 Monita link 帧 (fw/link/proto.h) 拼起来的整帧往返。 */
#include "tcheck.h"
#include "../core/x86proto.h"
#include "../fw/link/proto.h"
#include <string.h>

static void t_kbd(void){
    in_ring_t r; memset(&r, 0, sizeof r);
    uint8_t f[3] = { X86_KF_DOWN, 0, X86_KF_DOWN | X86_KF_E0 }, c[3] = { 0x1E, 0x9E, 0x48 }, b[16];
    uint32_t n = x86_kbd_encode(b, f, c, 3);
    CHECK(n == 7 && b[0] == 3);
    CHECK(x86_kbd_decode(b, n, &r) == 3);
    in_ev_t e;
    CHECK(in_pop(&r, &e) && e.kind == IN_KEY && e.flags == X86_KF_DOWN && e.code == 0x1E);
    CHECK(in_pop(&r, &e) && e.flags == 0 && e.code == 0x1E);            /* 断码位被去掉 */
    CHECK(in_pop(&r, &e) && e.flags == (X86_KF_DOWN | X86_KF_E0) && e.code == 0x48);
    CHECK(!in_pop(&r, &e));
    CHECK(x86_kbd_decode(b, n - 1, &r) == -1);                          /* 长度和 n 对不上 */
    CHECK(x86_kbd_decode(b, 0, &r) == -1);
    CHECK(r.head == r.tail);
}

static void t_mouse(void){
    in_ring_t r; memset(&r, 0, sizeof r);
    uint8_t b[8];
    CHECK(x86_mouse_encode(b, -300, 70000, -200, 0xFF) == 6);
    CHECK(x86_mouse_decode(b, 6, &r) == 1);
    in_ev_t e;
    CHECK(in_pop(&r, &e) && e.kind == IN_MOUSE && e.dx == -300 && e.dy == 32767 && e.dz == -128 && e.flags == 7);
    CHECK(x86_mouse_decode(b, 5, &r) == -1);
}

static void t_ring_full(void){
    in_ring_t r; memset(&r, 0, sizeof r);
    in_ev_t e = { IN_KEY, 1, 2, 0, 0, 0 }, o;
    for (uint32_t i = 0; i < IN_RING + 5; i++){ e.code = (uint8_t)i; in_push(&r, &e); }
    CHECK(r.dropped == 5);
    for (uint32_t i = 0; i < IN_RING; i++) CHECK(in_pop(&r, &o) && o.code == (uint8_t)i);
    CHECK(!in_pop(&r, &o));
    /* 回绕之后照常 */
    for (int k = 0; k < 1000; k++){ e.code = (uint8_t)k; CHECK(in_push(&r, &e)); CHECK(in_pop(&r, &o) && o.code == (uint8_t)k); }
}

static void t_stat(void){
    x86_stat_t s, d; memset(&s, 0, sizeof s);
    s.ver = 1; s.flags = 0x11; s.kips = 4321; s.ov_used = 7; s.ov_cap = 768; s.heap_free_kib = 300;
    s.mem_kib = 3072; s.in_dropped = 9; strcpy(s.log, "ide: abrt");
    uint8_t b[X86_STAT_LEN];
    CHECK(x86_stat_encode(b, &s) == X86_STAT_LEN);
    CHECK(b[0] == 1 && b[8] == (4321 & 0xff) && b[9] == (4321 >> 8));
    CHECK(x86_stat_decode(b, X86_STAT_LEN, &d) == 0 && memcmp(&s, &d, sizeof s) == 0);
    CHECK(x86_stat_decode(b, X86_STAT_LEN - 1, &d) == -1);
}

/* 主机编一条 PC_KBD 整帧, 设备侧用 Monita 的流式拆帧解出来 */
static void t_frame(void){
    uint8_t f[1] = { X86_KF_DOWN }, c[1] = { 0x1C }, pl[8], out[64], buf[64];
    uint32_t n = x86_kbd_encode(pl, f, c, 1);
    uint32_t len = lk_encode(out, X86_PC_KBD, 5, pl, n);
    lk_parser_t p; lk_init(&p, buf, sizeof buf);
    int ev; uint32_t k = lk_feed(&p, out, len, &ev);
    CHECK(k == len && ev == LK_EV_FRAME && p.type == X86_PC_KBD && p.seq == 5 && p.len == n);
    in_ring_t r; memset(&r, 0, sizeof r);
    CHECK(x86_kbd_decode(buf, p.len, &r) == 1);
}

int main(void){
    t_kbd(); t_mouse(); t_ring_full(); t_stat(); t_frame();
    return tc_done("x86proto");
}
