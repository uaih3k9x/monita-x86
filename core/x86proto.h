/*
 * x86 固件的链路帧 (在 Monita link 帧 A5 5A | type | seq | u16 len | payload | crc32 之上, 见 docs/protocol-x86.md)。
 * 只放 payload 的编解码和输入事件环, 纯逻辑, 主机 / 固件 / Mac 端对拍共用 (tests/test_x86proto.c)。
 *
 *   0xA0 PC_KBD   主机 -> 设备, 不应答: u8 n, n 组 {u8 flags (bit0 按下, bit1 E0 扩展), u8 set-1 扫描码 (不带 0x80 断码位)}
 *   0xA1 PC_MOUSE 主机 -> 设备, 不应答: s16 dx, s16 dy (向下为正), s8 dz, u8 buttons (bit0 左, bit1 右, bit2 中)
 *   0xA2 PC_CTRL  主机 -> 设备, 回 ACK: u8 op [, u8 arg]
 *                 1 硬复位 (arg bit0 = 同时清空写覆盖层), 2 Ctrl+Alt+Del, 3 暂停 / 继续, 4 切换状态栏, 5 静音 / 取消
 *   0xA3 PC_STAT  主机 -> 设备, 空 -> 设备回 0xE3 PC_STAT_R (见 x86_stat_t)
 * HELLO_ACK 的 caps 置 X86_CAP (bit11), 图像类 caps 全 0: 主机据此认出 x86 固件。
 */
#ifndef MX_X86PROTO_H
#define MX_X86PROTO_H
#include <stdint.h>

#define X86_PC_KBD    0xA0u
#define X86_PC_MOUSE  0xA1u
#define X86_PC_CTRL   0xA2u
#define X86_PC_STAT   0xA3u
#define X86_PC_STAT_R 0xE3u
#define X86_CAP       0x0800u

#define X86_KF_DOWN 0x01u
#define X86_KF_E0   0x02u

enum { X86_OP_RESET = 1, X86_OP_CAD = 2, X86_OP_PAUSE = 3, X86_OP_STATUSBAR = 4, X86_OP_MUTE = 5 };

/* 输入事件 (8 字节, 环里的单元) */
enum { IN_KEY = 1, IN_MOUSE = 2 };
typedef struct {
    uint8_t kind, flags, code;            /* KEY: flags/code; MOUSE: flags = buttons */
    int8_t  dz;
    int16_t dx, dy;
} in_ev_t;

/* 单生产者 (USB 拆帧) / 单消费者 (模拟器) 无锁环。容量 IN_RING (2 的幂), 满了丢新事件并计数 */
#define IN_RING 128u
typedef struct {
    in_ev_t ev[IN_RING];
    volatile uint32_t head, tail;     /* head 生产者写, tail 消费者写 */
    uint32_t dropped;
} in_ring_t;

#if defined(__arm__)
#define MX_DMB() __asm__ volatile("dmb sy" ::: "memory")
#else
#define MX_DMB() __sync_synchronize()
#endif

static inline int in_push(in_ring_t *r, const in_ev_t *e){
    uint32_t h = r->head;
    if (h - r->tail >= IN_RING){ r->dropped++; return 0; }
    r->ev[h & (IN_RING - 1)] = *e;
    MX_DMB();
    r->head = h + 1;
    return 1;
}
static inline int in_pop(in_ring_t *r, in_ev_t *e){
    uint32_t t = r->tail;
    if (t == r->head) return 0;
    MX_DMB();
    *e = r->ev[t & (IN_RING - 1)];
    MX_DMB();
    r->tail = t + 1;
    return 1;
}

static inline uint16_t x86_rd16(const uint8_t *p){ return (uint16_t)(p[0] | (p[1] << 8)); }
static inline void x86_wr16(uint8_t *p, uint32_t v){ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void x86_wr32(uint8_t *p, uint32_t v){ x86_wr16(p, v); x86_wr16(p + 2, v >> 16); }
static inline uint32_t x86_rd32(const uint8_t *p){ return x86_rd16(p) | ((uint32_t)x86_rd16(p + 2) << 16); }

/* PC_KBD payload -> 事件进环。返回事件数, -1 = 长度不对 (什么都不放) */
static inline int x86_kbd_decode(const uint8_t *pl, uint32_t n, in_ring_t *r){
    if (n < 1 || n != 1u + 2u * pl[0]) return -1;
    for (uint32_t i = 0; i < pl[0]; i++){
        in_ev_t e = { IN_KEY, pl[1 + 2 * i], (uint8_t)(pl[2 + 2 * i] & 0x7Fu), 0, 0, 0 };
        in_push(r, &e);
    }
    return pl[0];
}
static inline uint32_t x86_kbd_encode(uint8_t *out, const uint8_t *flags, const uint8_t *codes, uint32_t k){
    out[0] = (uint8_t)k;
    for (uint32_t i = 0; i < k; i++){ out[1 + 2 * i] = flags[i]; out[2 + 2 * i] = codes[i]; }
    return 1 + 2 * k;
}

static inline int x86_mouse_decode(const uint8_t *pl, uint32_t n, in_ring_t *r){
    if (n != 6) return -1;
    in_ev_t e = { IN_MOUSE, (uint8_t)(pl[5] & 7u), 0, (int8_t)pl[4],
                  (int16_t)x86_rd16(pl), (int16_t)x86_rd16(pl + 2) };
    in_push(r, &e);
    return 1;
}
static inline uint32_t x86_mouse_encode(uint8_t *out, int dx, int dy, int dz, uint8_t buttons){
    if (dx > 32767) dx = 32767; if (dx < -32768) dx = -32768;
    if (dy > 32767) dy = 32767; if (dy < -32768) dy = -32768;
    if (dz > 127) dz = 127; if (dz < -128) dz = -128;
    x86_wr16(out, (uint16_t)dx); x86_wr16(out + 2, (uint16_t)dy);
    out[4] = (uint8_t)(int8_t)dz; out[5] = buttons & 7u;
    return 6;
}

/* PC_STAT_R (设备 -> 主机), 全部小端 u32, 最后 32 字节是最近一条日志 (NUL 补齐) */
#define X86_STAT_WORDS 14u
#define X86_STAT_LOG   32u
#define X86_STAT_LEN   (X86_STAT_WORDS * 4u + X86_STAT_LOG)
typedef struct {
    uint32_t ver;          /* 固件版本 */
    uint32_t flags;        /* bit0 快路径核, bit1 暂停, bit2 静音, bit3 状态栏开, bit4 客户机已启动 */
    uint32_t kips;         /* 最近一个统计窗口的客户机指令 / ms (= 千条每秒) */
    uint32_t fps_x10;      /* 刷屏帧率 × 10 */
    uint32_t ov_used, ov_cap;      /* 写覆盖层扇区 */
    uint32_t ov_rejects;           /* 池满拒掉的写 */
    uint32_t heap_free_kib;        /* 分配完后堆的最大块 (KiB) */
    uint32_t mem_kib, vga_kib;     /* 客户机内存 / 显存 */
    uint32_t uptime_ms;
    uint32_t reset_reason;         /* 0 上电, 1 主机 PC_CTRL, 2 客户机 (8042 / 0x92) */
    uint32_t yields_per_s;         /* 模拟循环每秒让出 CPU 的次数 */
    uint32_t in_dropped;           /* 输入环满丢掉的事件 */
    char     log[X86_STAT_LOG];
} x86_stat_t;

static inline uint32_t x86_stat_encode(uint8_t *out, const x86_stat_t *s){
    const uint32_t *w = &s->ver;
    for (uint32_t i = 0; i < X86_STAT_WORDS; i++) x86_wr32(out + 4 * i, w[i]);
    for (uint32_t i = 0; i < X86_STAT_LOG; i++) out[X86_STAT_WORDS * 4 + i] = (uint8_t)s->log[i];
    return X86_STAT_LEN;
}
static inline int x86_stat_decode(const uint8_t *pl, uint32_t n, x86_stat_t *s){
    if (n != X86_STAT_LEN) return -1;
    uint32_t *w = &s->ver;
    for (uint32_t i = 0; i < X86_STAT_WORDS; i++) w[i] = x86_rd32(pl + 4 * i);
    for (uint32_t i = 0; i < X86_STAT_LOG; i++) s->log[i] = (char)pl[X86_STAT_WORDS * 4 + i];
    return 0;
}
_Static_assert(sizeof(x86_stat_t) == X86_STAT_LEN, "x86_stat_t 没有填充");
_Static_assert(sizeof(in_ev_t) == 8, "in_ev_t 8 字节");

#endif
