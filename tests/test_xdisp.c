/* fw/xdisp.h: x86 固件的链路帧分派 (纯逻辑)。回包、输入环、控制回调、HELLO 带的时间。 */
#include "tcheck.h"
#include "../fw/link/proto.h"
#include "../fw/xdisp.h"
#include <string.h>

static uint8_t  r_type[8], r_pl[8][128];
static uint32_t r_n[8], nr;
static void reply(void *ctx, uint8_t type, const uint8_t *pl, uint32_t n){
    (void)ctx; r_type[nr] = type; memcpy(r_pl[nr], pl, n); r_n[nr] = n; nr++;
}
static int c_op, c_arg, c_calls;
static int ctrl(void *ctx, int op, int arg){ (void)ctx; c_op = op; c_arg = arg; c_calls++; return op >= 1 && op <= 5 ? 0 : -1; }
static void stat(void *ctx, x86_stat_t *s){ (void)ctx; s->ver = 7; s->kips = 1234; strcpy(s->log, "hi"); }
static uint32_t t_unix; static int t_tz, t_calls;
static void htime(void *ctx, uint32_t unix_s, int tz_min){ (void)ctx; t_unix = unix_s; t_tz = tz_min; t_calls++; }

int main(void){
    in_ring_t ring; memset(&ring, 0, sizeof ring);
    xd_t d = { &ring, reply, ctrl, stat, htime, 0, 0, 0 };
    in_ev_t e;

    /* HELLO 空载荷: HELLO_ACK 16 字节, caps 只有 X86_CAP, 不碰时间 */
    nr = 0; xd_frame(&d, LK_HELLO, NULL, 0);
    CHECK(nr == 1 && r_type[0] == LK_HELLO_ACK && r_n[0] == 16);
    CHECK(lk_rd16(r_pl[0] + 2) == XD_FW_VER && lk_rd16(r_pl[0] + 4) == 800 && lk_rd16(r_pl[0] + 6) == 1280);
    CHECK(lk_rd16(r_pl[0] + 10) == X86_CAP && lk_rd16(r_pl[0] + 12) == XD_MAX_PAYLOAD);
    CHECK(t_calls == 0 && d.hello);
    /* HELLO 带时间: u32 unix, s16 tz */
    uint8_t h[6]; lk_wr32(h, 1795132800u); lk_wr16(h + 4, (uint16_t)(int16_t)-300);
    nr = 0; xd_frame(&d, LK_HELLO, h, 6);
    CHECK(nr == 1 && t_calls == 1 && t_unix == 1795132800u && t_tz == -300);

    /* 键盘: 不应答, 进环 */
    uint8_t k[5] = { 2, X86_KF_DOWN, 0x1E, 0, 0x1E };
    nr = 0; xd_frame(&d, X86_PC_KBD, k, 5);
    CHECK(nr == 0 && in_pop(&ring, &e) && e.code == 0x1E && e.flags == X86_KF_DOWN && in_pop(&ring, &e) && !in_pop(&ring, &e));
    xd_frame(&d, X86_PC_KBD, k, 4);                       /* 长度不对: 不应答, 计数 */
    CHECK(nr == 0 && d.bad == 1 && ring.head == ring.tail);

    /* 鼠标 */
    uint8_t m[6]; x86_mouse_encode(m, 5, -3, 0, 1);
    xd_frame(&d, X86_PC_MOUSE, m, 6);
    CHECK(nr == 0 && in_pop(&ring, &e) && e.kind == IN_MOUSE && e.dx == 5 && e.dy == -3 && e.flags == 1);

    /* 控制: 回 ACK, status 0; 带 arg; 不认识的 op -> EPARAM; 空载荷 -> EPARAM, 不调回调 */
    uint8_t c2[2] = { X86_OP_RESET, 1 };
    nr = 0; xd_frame(&d, X86_PC_CTRL, c2, 2);
    CHECK(nr == 1 && r_type[0] == LK_ACK && r_pl[0][0] == X86_PC_CTRL && r_pl[0][1] == LK_OK && c_op == 1 && c_arg == 1);
    uint8_t c1[1] = { X86_OP_PAUSE };
    nr = 0; xd_frame(&d, X86_PC_CTRL, c1, 1);
    CHECK(nr == 1 && r_pl[0][1] == LK_OK && c_op == 3 && c_arg == 0);
    uint8_t cx[1] = { 99 };
    nr = 0; xd_frame(&d, X86_PC_CTRL, cx, 1);
    CHECK(nr == 1 && r_pl[0][1] == LK_EPARAM);
    int before = c_calls;
    nr = 0; xd_frame(&d, X86_PC_CTRL, NULL, 0);
    CHECK(nr == 1 && r_pl[0][1] == LK_EPARAM && c_calls == before);

    /* 状态 */
    nr = 0; xd_frame(&d, X86_PC_STAT, NULL, 0);
    x86_stat_t s;
    CHECK(nr == 1 && r_type[0] == X86_PC_STAT_R && x86_stat_decode(r_pl[0], r_n[0], &s) == 0 && s.ver == 7 && s.kips == 1234 && !strcmp(s.log, "hi"));

    /* PING -> ACK; 推像素类帧 (RECT / COPY / IMM_RECT / RECT_LZ4 / AUDIO_DATA) 按协议本来就不应答: 丢掉;
       其它不认识的 -> ACK EUNSUP */
    nr = 0; xd_frame(&d, LK_PING, NULL, 0);
    CHECK(nr == 1 && r_type[0] == LK_ACK && r_pl[0][0] == LK_PING && r_pl[0][1] == LK_OK && r_n[0] == 40);
    nr = 0; xd_frame(&d, LK_RECT, k, 5); xd_frame(&d, LK_COPY, k, 5); xd_frame(&d, LK_RECT_LZ4, k, 5);
    xd_frame(&d, LK_IMM_RECT, k, 5); xd_frame(&d, LK_AUDIO_DATA, k, 4);
    CHECK(nr == 0);
    nr = 0; xd_frame(&d, LK_SEG, k, 5);
    CHECK(nr == 1 && r_type[0] == LK_ACK && r_pl[0][0] == LK_SEG && r_pl[0][1] == LK_EUNSUP);
    return tc_done("xdisp");
}
