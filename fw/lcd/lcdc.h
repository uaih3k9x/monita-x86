/*
 * LCD 控制器 (LCDC) 寄存器: 只有实机测过的几个 (2026-11-18, R05, 见 docs/r05-notes.md §4)。
 *   0x08807008  事件 (W1C)。0x04 / 0x08 / 0x20 每帧各置位一次, 周期 26.6 ms (≈37.5 Hz);
 *               0x40 一直置位。动 0x40/0x80 (或扫这段时 W1C 0x01/0x02/0x10) 后设备复位过, 只碰 0x2c 内的位。
 *   0x08807034  layer 扫描基址 (= ql_mipi_lcd_write_screen 写的值)。
 *   0x08807038  (行跨 << 16) | 使能, 0x06400001。
 * 读 0x08807040 起会让设备复位: 别扫这段以外的地址。
 */
#ifndef MONITA_LCDC_H
#define MONITA_LCDC_H
#include "lcd.h"

#define LCDC_BASE       0x08807000u
#define LCDC_EVT        0x08807008u
#define LCDC_LAYER      0x08807034u
#define LCDC_STRIDE     0x08807038u
#define LCDC_EVT_SAFE   0x2cu
#define LCDC_EVT_FRAME  0x04u

static inline uint32_t rd32(uint32_t a){ return *(volatile uint32_t *)a; }
static inline void wr32(uint32_t a, uint32_t v){ *(volatile uint32_t *)a = v; }

/* 清掉事件位, 忙等它下一次置位 (= 一帧里的固定相位)。返回等了多少 us, 超时返回 0。 */
static uint32_t lcd_wait_frame(uint32_t bit, uint32_t timeout_us){
    wr32(LCDC_EVT, bit);
    uint32_t t0 = (uint32_t)osiUpTimeUS();
    for (;;){
        uint32_t d = (uint32_t)osiUpTimeUS() - t0;
        if (rd32(LCDC_EVT) & bit) return d ? d : 1u;
        if (d > timeout_us) return 0;
    }
}

#endif
