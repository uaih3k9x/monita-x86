/*
 * x86 固件的链路帧分派 (纯逻辑, 主机单测 tests/test_xdisp.c)。帧格式见 fw/link/proto.h, x86 载荷见 core/x86proto.h。
 *   HELLO -> HELLO_ACK (caps = X86_CAP, 图像类 caps 全 0: 主机据此认出 x86 固件; 带时间的话交给 time 回调)
 *   PC_KBD / PC_MOUSE -> 进输入环, 不应答 (坏载荷计数)
 *   PC_CTRL -> ctrl 回调, ACK (0 = OK, 否则 EPARAM);  PC_STAT -> PC_STAT_R;  PING -> ACK
 *   推像素类 (RECT / COPY / IMM_RECT / RECT_LZ4 / AUDIO_DATA) 按协议不应答, 直接丢; 其它 -> ACK EUNSUP
 * 没有任何写 flash / 写文件的分支。
 */
#ifndef MX_XDISP_H
#define MX_XDISP_H
#include <stdint.h>
#include <string.h>
#include "x86proto.h"
#include "proto.h"

#define XD_FW_VER      0x0001u
#define XD_MAX_PAYLOAD 256u          /* x86 固件只收小帧 */

typedef struct {
    in_ring_t *in;
    void (*reply)(void *ctx, uint8_t type, const uint8_t *pl, uint32_t n);
    int  (*ctrl)(void *ctx, int op, int arg);
    void (*stat)(void *ctx, x86_stat_t *s);
    void (*time)(void *ctx, uint32_t unix_s, int tz_min);
    void *ctx;
    uint32_t bad;                    /* 坏载荷 */
    int hello;                       /* 收到过 HELLO */
} xd_t;

static inline void xd_ack(xd_t *d, uint8_t type, uint8_t status, uint32_t extra){
    uint8_t b[40];
    memset(b, 0, sizeof b);
    b[0] = type; b[1] = status;
    x86_wr32(b + 4 + 6 * 4, d->bad);       /* 同 Monita ACK 布局: 第 7 个字 = bad_param */
    x86_wr32(b + 4 + 8 * 4, extra);
    d->reply(d->ctx, LK_ACK, b, 40);
}

static inline void xd_frame(xd_t *d, uint8_t type, const uint8_t *pl, uint32_t n){
    switch (type){
    case LK_HELLO: {
        d->hello = 1;
        if (n >= 6 && d->time) d->time(d->ctx, x86_rd32(pl), (int)(int16_t)x86_rd16(pl + 4));
        uint8_t b[16];
        memset(b, 0, sizeof b);
        x86_wr16(b + 0, LK_PROTO_VER); x86_wr16(b + 2, XD_FW_VER);
        x86_wr16(b + 4, 800); x86_wr16(b + 6, 1280);
        b[8] = LK_PIXFMT_RGB565;
        x86_wr16(b + 10, X86_CAP); x86_wr16(b + 12, XD_MAX_PAYLOAD);
        d->reply(d->ctx, LK_HELLO_ACK, b, 16);
        return;
    }
    case X86_PC_KBD:   if (x86_kbd_decode(pl, n, d->in) < 0) d->bad++; return;
    case X86_PC_MOUSE: if (x86_mouse_decode(pl, n, d->in) < 0) d->bad++; return;
    case X86_PC_CTRL:
        if (n < 1 || n > 2){ d->bad++; xd_ack(d, type, LK_EPARAM, 0); return; }
        xd_ack(d, type, d->ctrl(d->ctx, pl[0], n == 2 ? pl[1] : 0) == 0 ? LK_OK : LK_EPARAM, 0);
        return;
    case X86_PC_STAT: {
        x86_stat_t s;
        memset(&s, 0, sizeof s);
        d->stat(d->ctx, &s);
        uint8_t b[X86_STAT_LEN];
        d->reply(d->ctx, X86_PC_STAT_R, b, x86_stat_encode(b, &s));
        return;
    }
    case LK_PING: xd_ack(d, type, LK_OK, 0); return;
    case LK_RECT: case LK_COPY: case LK_IMM_RECT: case LK_RECT_LZ4: case LK_AUDIO_DATA: return;
    default: xd_ack(d, type, LK_EUNSUP, 0); return;
    }
}
#endif
