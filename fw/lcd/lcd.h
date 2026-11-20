/*
 * 800x1280 MIPI 大屏: 帧缓冲 + 背光 + 上电顺序 (常量和顺序来自原厂 app 的调用点, 见 docs/r05-notes.md)。
 *
 *   pin118 func0 -> GPIO7 输出低(背光灭) -> ql_mipi_lcd_init(&{800,1280}) -> 无参 PM 锁
 *   -> 清 fb -> ql_mipi_lcd_write_screen(fb) -> 画首帧 -> sleep 200 ms -> GPIO7=1 背光亮
 *
 * write_screen 只把 LCDC 扫描基址指到 fb (video mode 持续扫描, 不拷贝), 之后直接改 fb 即可。
 * 原厂没做 cache clean; 这里写完后做一次 DCCMVAC (若该区不可缓存则等于空操作)。
 * 只供单个任务调用。
 */
#ifndef MONITA_LCD_H
#define MONITA_LCD_H
#include "r05_api.h"

#define LCD_W   800u
#define LCD_H   1280u
#define FB_ADDR 0x80de0000u                 /* 内核堆止于此; 0x80de0000..0x80fe0000 2 MiB 不在堆内 */
#define FB_SIZE (LCD_W * LCD_H * 2u)         /* RGB565, 行跨 0x640, 共 0x1f4000 B */
#define BL_PIN  118u
#define BL_GPIO 7u

static const r05_mipi_lcd_cfg_t lcd_cfg = { LCD_W, LCD_H };

static void dcache_clean(uint32_t a, uint32_t n){
    uint32_t e = a + n;
    __asm__ volatile("dmb sy" ::: "memory");
    for (a &= ~31u; a < e; a += 32u)
        __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(a) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

static void fb_fill(uint16_t c){
    volatile uint32_t *p = (volatile uint32_t *)FB_ADDR;
    uint32_t v = ((uint32_t)c << 16) | c;
    for (uint32_t i = 0; i < FB_SIZE / 4u; i++) p[i] = v;
    dcache_clean(FB_ADDR, FB_SIZE);
}

static void lcd_backlight(uint32_t on){ ql_gpio_set_level(BL_GPIO, on ? 1u : 0u); }

/* 背光灭 -> init -> PM 锁 -> 清屏 -> 指定扫描基址。返回 0 成功;
 * 失败返回 (阶段<<16)|(返回码低 16 位): 阶段 1 = lcd_init, 2 = write_screen。背光保持灭, 由调用方画好首帧后再开。 */
static int lcd_power_on(void){
    ql_pin_set_func(BL_PIN, 0);
    ql_gpio_deinit(BL_GPIO);
    ql_gpio_init(BL_GPIO, GPIO_OUTPUT, PULL_NONE, 0);
    int r = ql_mipi_lcd_init(&lcd_cfg);
    if (r < 0) return (1 << 16) | (r & 0xffff);
    r05_lcd_pm_lock();
    fb_fill(0x0000);
    r = ql_mipi_lcd_write_screen((void *)FB_ADDR);
    if (r != 0) return (2 << 16) | (r & 0xffff);
    return 0;
}

#endif
