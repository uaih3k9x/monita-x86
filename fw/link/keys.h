/*
 * CS80 前面板 4 键: 去抖 + 长按状态机 + PWRKEY 计数转电平 + KEY 消息载荷 (纯逻辑, 不碰硬件, 主机单测 test_keys.c)。
 * 硬件层在 ../io/keys_hw.h; 协议见 docs/protocol.md 的 KEY (0x90)。
 *
 * 键号: 1 查询 (左上, PWRKEY)  2 ≡ (左下, GPIO21)  3 + (右上, GPIO8)  4 − (右下, GPIO23)
 * raw: bit(k-1)=1 表示键 k 此刻是按下 (调用方负责把"低电平有效"翻成 1)。
 *
 * 规则: 新电平连续保持 >= KEY_DEBOUNCE_MS 才算数 (主循环 ≈ 10 ms 采一次);
 *       按下满 KEY_LONG_MS 报一次 LONG, 之后松开照样报 UP。
 *       事件缓冲满时不提交状态, 下次再报 (不丢事件)。时间是 u32 (固件里 1.024 ms 一个单位), 允许回绕。
 */
#ifndef MONITA_KEYS_H
#define MONITA_KEYS_H
#include <stdint.h>

#define KEY_N            4
#define KEY_ALL          0x0Fu
#define KEY_BIT(k)       ((uint8_t)(1u << ((k) - 1)))
#define KEY_DEBOUNCE_MS  16u      /* 实机: 30 ms 会把很快的轻点滤掉; GPIO 键另有硬件去抖 */
#define KEY_LONG_MS      1000u

#define KEY_DOWN 1u
#define KEY_UP   2u
#define KEY_LONG 3u
#define KEY_EV(k, type)  ((uint8_t)(((k) << 4) | (type)))
#define KEY_EV_KEY(e)    ((e) >> 4)
#define KEY_EV_TYPE(e)   ((e) & 0x0Fu)

typedef struct {
    uint8_t  stable;               /* 已确认的按下位图 */
    uint8_t  cand;                 /* 最近一次采样 */
    uint8_t  long_sent;
    uint32_t since[KEY_N];         /* cand 该位最近一次变化的时刻 */
    uint32_t down_at[KEY_N];
} keys_t;

static inline void keys_init(keys_t *k){
    uint8_t *p = (uint8_t *)k;
    for (unsigned i = 0; i < sizeof *k; i++) p[i] = 0;   /* 开机按全松开处理 */
}

/* 喂一次采样, 返回写进 ev 的事件数 (<= max) */
static inline int keys_feed(keys_t *k, uint32_t now, uint8_t raw, uint8_t *ev, int max){
    int n = 0;
    for (int i = 0; i < KEY_N; i++){
        uint8_t b = (uint8_t)(1u << i);
        if ((raw ^ k->cand) & b){ k->cand ^= b; k->since[i] = now; continue; }
        if ((k->cand ^ k->stable) & b){
            if (now - k->since[i] < KEY_DEBOUNCE_MS || n >= max) continue;
            k->stable ^= b;
            if (k->stable & b){ k->down_at[i] = now; k->long_sent &= (uint8_t)~b; ev[n++] = KEY_EV(i + 1, KEY_DOWN); }
            else ev[n++] = KEY_EV(i + 1, KEY_UP);
        } else if ((k->stable & b) && !(k->long_sent & b) && now - k->down_at[i] >= KEY_LONG_MS && n < max){
            k->long_sent |= b; ev[n++] = KEY_EV(i + 1, KEY_LONG);
        }
    }
    return n;
}

/* ---- PWRKEY (键 1) ----
 * 内核按下 / 抬起回调只给计数器 +1 (volatile, 回调里不做别的), 主循环用 pwr_sample 翻成电平喂给 keys_feed。
 *  - 两拍之间按下又抬起 (两个计数一起变): 电平保持到"某一拍时已按下满 KEY_DEBOUNCE_MS"之后的下一拍,
 *    保证 keys_feed 的去抖一定看得见 (只按时间算不够: 主循环被 PRESENT / 整屏同步拖住时两拍可差 ≈ 60 ms)。
 *  - 实机: 长按后内核不再调抬起回调。按下超过 KEY_PWR_STUCK_MS 还没抬起就当已经松开 (报一次 UP);
 *    之后迟到的抬起回调被吞掉, 下一次按下回调照常。 */
#define KEY_PWR_STUCK_MS 4000u    /* > 内核关机长按 (3000 ms, 已被我们的回调截住) */

typedef struct { uint32_t press, release, at; uint8_t down, seen; } pwr_t;   /* seen: 已有一拍按下满去抖时间 */

static inline void pwr_init(pwr_t *p){ p->press = p->release = p->at = 0; p->down = p->seen = 0; }

/* press / release: 回调计数的当前值; 返回 1 = 按下。调用方每拍先 pwr_sample 再用同一个 now 喂 keys_feed */
static inline uint8_t pwr_sample(pwr_t *p, uint32_t press, uint32_t release, uint32_t now){
    if (press != p->press){ p->press = press; p->down = 1; p->at = now; p->seen = 0; }
    if (p->down){
        uint32_t held = now - p->at;
        if ((release != p->release && p->seen) || held >= KEY_PWR_STUCK_MS){ p->down = 0; p->release = release; }
        else if (held >= KEY_DEBOUNCE_MS) p->seen = 1;       /* 这一拍 keys_feed 会确认按下; 之后才许抬起 */
    } else p->release = release;
    return p->down;
}

/* ---- KEY 消息 (0x90, 设备 -> 主机) 载荷: u8 key, u8 ev, u16 n, u32 uptime ---- */
#define KEY_PL_LEN 8u
static inline void keys_payload(uint8_t *b, uint8_t ev, uint32_t n, uint32_t uptime){
    b[0] = (uint8_t)KEY_EV_KEY(ev); b[1] = (uint8_t)KEY_EV_TYPE(ev);
    b[2] = (uint8_t)n; b[3] = (uint8_t)(n >> 8);
    for (int i = 0; i < 4; i++) b[4 + i] = (uint8_t)(uptime >> (8 * i));
}

#endif
