/*
 * Monita link 协议 v0 —— 纯逻辑部分 (CRC-32、流式拆帧、回包编码)。
 * 不依赖 R05 SDK, 主机上可以直接编译测试 (fw/link/test.sh)。协议说明见 docs/protocol.md。
 *
 * 帧:  A5 5A | u8 type | u8 seq | u16 len | payload[len] | u32 crc    (全部小端)
 *      crc = CRC-32/IEEE (= zlib.crc32), 覆盖 type..payload (不含同步字和 crc 自身)。
 *
 * 用法: lk_parser_t p; lk_init(&p, buf, cap);
 *       每收到一段字节: while (n) { k = lk_feed(&p, data, n, &ev); data += k; n -= k; if (ev) 处理 p.type/p.seq/p.len/buf }
 * 解析器不做动态分配, payload 收进调用方给的缓冲 (cap 就是 max_payload)。
 */
#ifndef MONITA_LINK_PROTO_H
#define MONITA_LINK_PROTO_H
#include <stdint.h>

#define LK_SYNC0 0xA5u
#define LK_SYNC1 0x5Au
#define LK_HDR   6u          /* sync0 sync1 type seq len16 */
#define LK_TRAIL 4u          /* crc32 */
#define LK_PROTO_VER 0u

/* 主机 -> 设备 */
#define LK_HELLO     0x01u
#define LK_SEG       0x10u   /* 72 字节段码显存原样 */
#define LK_RECT      0x20u   /* u16 x,y,w,h + w*h 个 RGB565 */
#define LK_PRESENT   0x21u   /* u16 frame_id */
#define LK_BACKLIGHT 0x30u   /* u8 on */
#define LK_LED       0x31u   /* u8 mask, u8 mode, u16 period_ms, u16 on_ms, u16 duration_ms (fw v8, link/led.h) */
#define LK_SLEEP     0x32u   /* 空 -> ACK, 然后关背光 / 段码屏 / 灯; 之后任何一帧叫醒 (fw v10, link/linkwd.h) */
#define LK_COPY      0x22u   /* u16 sx,sy,w,h,dx,dy: 画布内搬矩形 (允许重叠), 不应答 */
#define LK_RECT_LZ4  0x23u   /* u16 x,y,w,h + u32 raw_len + LZ4 块 (fw v11, link/lz4.h); 同 RECT 不应答 */
#define LK_FBCRC     0x40u   /* u16 x,y,w,h -> ACK 带 crc32 */
#define LK_PING      0x7Fu
#define LK_DIAG      0x50u   /* u8 op + 参数, 诊断用 (fw/link/diag.h) -> DIAG_R */
/* 沉浸模式 (fw v9, link/imm.h): 整屏只放卷轴, 滚动 = 改扫描基址 */
#define LK_IMM_ENTER  0x60u  /* u32 y0 (可省 = 0) -> ACK (extra = 卷轴行数 R); 已在沉浸模式 = 重新开始 */
#define LK_IMM_EXIT   0x61u  /* 空 -> ACK (extra = 恢复后的 caps) */
#define LK_IMM_RECT   0x62u  /* u32 y, u16 x, w, h, u8 flags, u8 0 + w*h 个 RGB565; 不应答 (同 RECT) */
#define LK_IMM_SCROLL 0x63u  /* u32 y, u16 dur_ms -> ACK (extra = 正在扫描的窗口顶) */
/* 喇叭 (fw v13, link/audio.h, caps bit10, docs/audio.md) */
#define LK_AUDIO_START 0x70u /* u32 rate, u8 channels, u8 format, u8 volume, u8 0 -> ACK (extra = 环形缓冲字节数) */
#define LK_AUDIO_DATA  0x71u /* s16le PCM, 偶数字节; 不应答 (同 RECT), 放不下整条丢 */
#define LK_AUDIO_STOP  0x72u /* [u8 flags: bit0 放完缓冲再停] -> ACK */
#define LK_AUDIO_STAT  0x73u /* 空 -> AUDIO_STAT_R (0xF0, 40 B) */
/* 设备 -> 主机 */
#define LK_HELLO_ACK 0x81u
#define LK_KEY       0x90u   /* u8 key, u8 ev, u16 n, u32 uptime (fw v8, link/keys.h); 主动上报, seq = 0, 主机不应答 */
#define LK_AUDIO_STAT_R 0xF0u /* 40 B: 状态 / 水位 / 计数 (link/audio.h au_stat) */
#define LK_DIAG_R    0xD0u   /* u8 op, u8 status, u16 n, n × u32 (n ≤ 16) */
#define LK_ACK       0xFEu

/* ACK status */
#define LK_OK        0u
#define LK_EPARAM    1u
#define LK_EUNSUP    3u

/* HELLO_ACK caps */
#define LK_CAP_FBCRC     0x0001u
#define LK_CAP_DOUBLEBUF 0x0002u   /* PRESENT 是翻页 (RECT 写后台, PRESENT 前不可见) */
#define LK_CAP_COPY      0x0004u   /* 支持 COPY (0x22) */
#define LK_CAP_TRIBUF    0x0008u   /* 三缓冲 (fw v6): PRESENT 后下一批不用等帧头 */
#define LK_CAP_KEYS      0x0010u   /* 按键上报 KEY (0x90), fw v8 */
#define LK_CAP_LED       0x0020u   /* 状态灯 LED (0x31), fw v8 */
#define LK_CAP_IMMERSIVE 0x0040u   /* 沉浸模式 IMM_* (0x60..0x63), fw v9 */
#define LK_CAP_SLEEP     0x0080u   /* 休眠 SLEEP (0x32) + 主机走了回开机画面, fw v10 */
#define LK_CAP_LZ4       0x0100u   /* RECT_LZ4 (0x23), fw v11 */
#define LK_CAP_AUDIO     0x0400u   /* 喇叭 AUDIO_* (0x70..0x73), fw v13 */
#define LK_PIXFMT_RGB565 1u

/* ---- CRC-32/IEEE (反射, 多项式 0xEDB88320, 初值/终值异或 0xFFFFFFFF) ----
 * 256 项字节表放 RAM (1 KiB), 第一次用时生成; 每字节一次查表。 */
static uint32_t lk_crc_tab[256];
static void lk_crc_init(void){
    for (uint32_t i = 0; i < 256; i++){
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        lk_crc_tab[i] = c;
    }
}
/* 增量形式: c = lk_crc32_update(0xFFFFFFFF, ...); ...; 结果 = c ^ 0xFFFFFFFF */
static uint32_t lk_crc32_update(uint32_t c, const uint8_t *p, uint32_t n){
    if (lk_crc_tab[1] == 0) lk_crc_init();
    while (n >= 4){
        c = (c >> 8) ^ lk_crc_tab[(c ^ p[0]) & 0xFFu];
        c = (c >> 8) ^ lk_crc_tab[(c ^ p[1]) & 0xFFu];
        c = (c >> 8) ^ lk_crc_tab[(c ^ p[2]) & 0xFFu];
        c = (c >> 8) ^ lk_crc_tab[(c ^ p[3]) & 0xFFu];
        p += 4; n -= 4;
    }
    while (n--) c = (c >> 8) ^ lk_crc_tab[(c ^ *p++) & 0xFFu];
    return c;
}
__attribute__((unused)) static uint32_t lk_crc32(const uint8_t *p, uint32_t n){
    return lk_crc32_update(0xFFFFFFFFu, p, n) ^ 0xFFFFFFFFu;
}

static inline uint16_t lk_rd16(const uint8_t *p){ return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t lk_rd32(const uint8_t *p){ return (uint32_t)lk_rd16(p) | ((uint32_t)lk_rd16(p + 2) << 16); }
static inline void lk_wr16(uint8_t *p, uint32_t v){ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void lk_wr32(uint8_t *p, uint32_t v){
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* ---- 流式拆帧 ---- */
enum { LK_S_SYNC0, LK_S_SYNC1, LK_S_HDR, LK_S_BODY, LK_S_CRC };
#define LK_EV_NONE  0
#define LK_EV_FRAME 1    /* 一帧收齐且 CRC 对: p->type/seq/len, payload 在 buf */

typedef struct {
    uint8_t *buf; uint32_t cap;
    uint8_t  state, type, seq, hi;    /* hi: 头/CRC 已收字节数 */
    uint8_t  hdr[4];                  /* type seq len16 */
    uint8_t  crcb[4];
    uint32_t len, got;
    /* 统计 */
    uint32_t frames, crc_err, len_err;
} lk_parser_t;

__attribute__((unused)) static void lk_init(lk_parser_t *p, uint8_t *buf, uint32_t cap){
    uint8_t *z = (uint8_t *)p;
    for (uint32_t i = 0; i < sizeof *p; i++) z[i] = 0;
    p->buf = buf; p->cap = cap; p->state = LK_S_SYNC0;
}
/* 半截帧超时等情况: 丢掉当前帧, 重新找同步字 */
__attribute__((unused)) static void lk_reset(lk_parser_t *p){ p->state = LK_S_SYNC0; }
__attribute__((unused)) static int lk_idle(const lk_parser_t *p){ return p->state == LK_S_SYNC0; }

/* 吃进最多 n 字节, 返回实际消耗的字节数; 一旦收齐一帧就停下并 *ev = LK_EV_FRAME。
 * 坏帧计数后丢弃、不应答, 然后重新找 A5 5A: 长度超限的读完 4 字节头就找 (后面的字节照常扫);
 * CRC 错的是按 len 整帧读完才找 (流式、不留缓冲; len 坏了会吞掉后面最多 len+4 字节里的好帧)。 */
static uint32_t lk_feed(lk_parser_t *p, const uint8_t *d, uint32_t n, int *ev){
    uint32_t i = 0;
    *ev = LK_EV_NONE;
    while (i < n){
        switch (p->state){
        case LK_S_SYNC0:
            if (d[i++] == LK_SYNC0) p->state = LK_S_SYNC1;
            break;
        case LK_S_SYNC1: {
            uint8_t b = d[i++];
            if (b == LK_SYNC1){ p->state = LK_S_HDR; p->hi = 0; }
            else if (b != LK_SYNC0) p->state = LK_S_SYNC0;   /* A5 A5 5A 也能同步上 */
            break;
        }
        case LK_S_HDR:
            p->hdr[p->hi++] = d[i++];
            if (p->hi == 4){
                p->type = p->hdr[0]; p->seq = p->hdr[1]; p->len = lk_rd16(&p->hdr[2]);
                if (p->len > p->cap){ p->len_err++; p->state = LK_S_SYNC0; break; }
                p->got = 0; p->hi = 0;
                p->state = p->len ? LK_S_BODY : LK_S_CRC;
            }
            break;
        case LK_S_BODY: {
            uint32_t k = p->len - p->got;
            if (k > n - i) k = n - i;
            uint8_t *dst = p->buf + p->got; const uint8_t *src = d + i; uint32_t j = 0;
            if ((((uintptr_t)dst | (uintptr_t)src) & 3u) == 0)
                for (; j + 4 <= k; j += 4) *(uint32_t *)(dst + j) = *(const uint32_t *)(src + j);
            for (; j < k; j++) dst[j] = src[j];
            i += k; p->got += k;
            if (p->got == p->len){ p->state = LK_S_CRC; p->hi = 0; }
            break;
        }
        case LK_S_CRC:
            p->crcb[p->hi++] = d[i++];
            if (p->hi == 4){
                uint32_t want = (uint32_t)p->crcb[0] | ((uint32_t)p->crcb[1] << 8)
                              | ((uint32_t)p->crcb[2] << 16) | ((uint32_t)p->crcb[3] << 24);
                uint32_t c = lk_crc32_update(0xFFFFFFFFu, p->hdr, 4);
                c = lk_crc32_update(c, p->buf, p->len) ^ 0xFFFFFFFFu;
                p->state = LK_S_SYNC0;
                if (c != want){ p->crc_err++; break; }
                p->frames++;
                *ev = LK_EV_FRAME;
                return i;
            }
            break;
        }
    }
    return i;
}

/* 编一帧到 out (容量至少 LK_HDR+len+LK_TRAIL), 返回总长度 */
__attribute__((unused)) static uint32_t lk_encode(uint8_t *out, uint8_t type, uint8_t seq,
                                                  const uint8_t *payload, uint32_t len){
    out[0] = LK_SYNC0; out[1] = LK_SYNC1; out[2] = type; out[3] = seq; lk_wr16(out + 4, len);
    for (uint32_t i = 0; i < len; i++) out[LK_HDR + i] = payload[i];
    uint32_t c = lk_crc32(out + 2, 4 + len);
    lk_wr32(out + LK_HDR + len, c);
    return LK_HDR + len + LK_TRAIL;
}

/* DIAG_R 载荷 (b 至少 4 + 4n 字节), 返回长度 */
__attribute__((unused)) static uint32_t lk_diag_payload(uint8_t *b, uint8_t op, uint8_t status, const uint32_t *w, uint32_t n){
    b[0] = op; b[1] = status; lk_wr16(b + 2, n);
    for (uint32_t i = 0; i < n; i++) lk_wr32(b + 4 + 4 * i, w[i]);
    return 4 + 4 * n;
}

/* ---- RECT 落到帧缓冲 ----
 * fb: w_fb × h_fb 个 u16, 行跨 w_fb*2。payload = u16 x,y,w,h + w*h 个 RGB565 (行优先)。
 * 返回 LK_OK 或 LK_EPARAM (越界 / 长度不对, 此时不写任何像素)。 */
static uint32_t lk_rect_apply(uint16_t *fb, uint32_t w_fb, uint32_t h_fb,
                              const uint8_t *pl, uint32_t len, uint32_t *y0, uint32_t *y1){
    if (len < 8) return LK_EPARAM;
    uint32_t x = lk_rd16(pl), y = lk_rd16(pl + 2), w = lk_rd16(pl + 4), h = lk_rd16(pl + 6);
    if (w == 0 || h == 0 || x + w > w_fb || y + h > h_fb) return LK_EPARAM;
    if (len != 8u + w * h * 2u) return LK_EPARAM;
    const uint8_t *src = pl + 8;
    for (uint32_t r = 0; r < h; r++){
        uint16_t *dst = fb + (y + r) * w_fb + x;
        const uint8_t *s = src + r * w * 2u;
        if ((((uintptr_t)dst | (uintptr_t)s) & 3u) == 0){
            /* 两边都 4 字节对齐 (x 偶数时常见): 按字拷 */
            uint32_t pairs = w >> 1;
            uint32_t *d32 = (uint32_t *)dst; const uint32_t *s32 = (const uint32_t *)s;
            for (uint32_t k = 0; k < pairs; k++) d32[k] = s32[k];
            if (w & 1u) dst[w - 1] = lk_rd16(s + (w - 1) * 2u);
        } else {
            for (uint32_t k = 0; k < w; k++) dst[k] = lk_rd16(s + k * 2u);
        }
    }
    if (y < *y0) *y0 = y;
    if (y + h > *y1) *y1 = y + h;
    return LK_OK;
}

/* ---- COPY: fb 内把 (sx,sy,w,h) 搬到 (dx,dy), 等价于 memmove (先整块读出再写), 允许任意重叠 ----
 * 行顺序: 往下搬 (dy > sy) 从最后一行倒着拷; 同一行内往右搬 (dy == sy && dx > sx) 每行从右往左拷。
 * 返回 LK_OK / LK_EPARAM (越界、长度不是 12、w/h 为 0: 不动像素)。脏行区间并入目标行。 */
static void lk_row_move(uint16_t *d, const uint16_t *s, uint32_t w, int backward){
    if (!backward && ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0)){
        uint32_t *d32 = (uint32_t *)d; const uint32_t *s32 = (const uint32_t *)s; uint32_t n = w >> 1;
        for (uint32_t k = 0; k < n; k++) d32[k] = s32[k];   /* 向前拷: d < s 或不重叠, 按字也安全 */
        if (w & 1u) d[w - 1] = s[w - 1];
    } else if (!backward){
        for (uint32_t k = 0; k < w; k++) d[k] = s[k];
    } else {
        for (uint32_t k = w; k-- > 0;) d[k] = s[k];
    }
}

__attribute__((unused)) static uint32_t lk_copy_apply(uint16_t *fb, uint32_t w_fb, uint32_t h_fb,
                                                      const uint8_t *pl, uint32_t len, uint32_t *y0, uint32_t *y1){
    if (len != 12) return LK_EPARAM;
    uint32_t sx = lk_rd16(pl), sy = lk_rd16(pl + 2), w = lk_rd16(pl + 4), h = lk_rd16(pl + 6);
    uint32_t dx = lk_rd16(pl + 8), dy = lk_rd16(pl + 10);
    if (w == 0 || h == 0 || sx + w > w_fb || sy + h > h_fb || dx + w > w_fb || dy + h > h_fb) return LK_EPARAM;
    int back = dy == sy && dx > sx;
    if (dy > sy){
        for (uint32_t r = h; r-- > 0;) lk_row_move(fb + (dy + r) * w_fb + dx, fb + (sy + r) * w_fb + sx, w, 0);
    } else if (dy < sy || dx != sx){
        for (uint32_t r = 0; r < h; r++) lk_row_move(fb + (dy + r) * w_fb + dx, fb + (sy + r) * w_fb + sx, w, back);
    }
    if (dy < *y0) *y0 = dy;
    if (dy + h > *y1) *y1 = dy + h;
    return LK_OK;
}

/* FBCRC: 对 fb 里矩形区域按行优先、小端字节做 CRC-32 (与主机对同一块 RGB565 字节算 zlib.crc32 一致) */
__attribute__((unused)) static int lk_fb_crc(const uint16_t *fb, uint32_t w_fb, uint32_t h_fb,
                                             const uint8_t *pl, uint32_t len, uint32_t *out){
    if (len != 8) return LK_EPARAM;
    uint32_t x = lk_rd16(pl), y = lk_rd16(pl + 2), w = lk_rd16(pl + 4), h = lk_rd16(pl + 6);
    if (w == 0 || h == 0 || x + w > w_fb || y + h > h_fb) return LK_EPARAM;
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t r = 0; r < h; r++)
        c = lk_crc32_update(c, (const uint8_t *)(fb + (y + r) * w_fb + x), w * 2u);  /* 小端机器 */
    *out = c ^ 0xFFFFFFFFu;
    return LK_OK;
}

#endif
