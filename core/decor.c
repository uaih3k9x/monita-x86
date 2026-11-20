/* 机箱面板 + 状态栏, 见 decor.h */
#include "decor.h"

extern const uint8_t vgafont16[];

#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define C_CASE   RGB(0xC8, 0xC4, 0xB8)    /* 米色机箱 */
#define C_SHADE  RGB(0x8A, 0x86, 0x7C)
#define C_DARK   RGB(0x30, 0x30, 0x30)
#define C_TEXT   RGB(0x20, 0x20, 0x20)
#define C_LEDG   RGB(0x20, 0xD0, 0x40)
#define C_BAR    RGB(0x10, 0x18, 0x20)
#define C_BARTX  RGB(0x9F, 0xE8, 0xA0)

static void rect(uint16_t *fb, int stride, int x, int y, int w, int h, uint16_t c){
    for (int r = 0; r < h; r++){
        uint16_t *p = fb + (y + r) * stride + x;
        for (int k = 0; k < w; k++) p[k] = c;
    }
}

void decor_text(uint16_t *fb, int stride, int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg){
    for (; *s; s++, x += 8 * scale){
        const uint8_t *g = vgafont16 + (uint8_t)*s * 16;
        for (int r = 0; r < 16 * scale; r++){
            uint8_t bits = g[r / scale];
            uint16_t *p = fb + (y + r) * stride + x;
            for (int c = 0; c < 8 * scale; c++) p[c] = (bits & (0x80 >> (c / scale))) ? fg : bg;
        }
    }
}

void decor_panel(uint16_t *fb, int w, int h){
    (void)h;
    int x0 = 40, y0 = 40, pw = w - 80, ph = DECOR_PANEL_H - 80;
    rect(fb, w, 0, 0, w, DECOR_PANEL_H, 0);
    rect(fb, w, x0, y0, pw, ph, C_CASE);
    rect(fb, w, x0, y0 + ph - 6, pw, 6, C_SHADE);
    /* 5.25" 软驱 + 3.5" 软驱槽 */
    rect(fb, w, x0 + 40, y0 + 40, 300, 60, C_SHADE); rect(fb, w, x0 + 60, y0 + 66, 260, 8, C_DARK);
    rect(fb, w, x0 + 40, y0 + 120, 300, 50, C_SHADE); rect(fb, w, x0 + 150, y0 + 140, 120, 6, C_DARK);
    decor_text(fb, w, x0 + 380, y0 + 40, "monita-x86", 3, C_TEXT, C_CASE);
    decor_text(fb, w, x0 + 380, y0 + 100, "i386  no FPU  tiny386", 1, C_TEXT, C_CASE);
    decor_text(fb, w, x0 + 380, y0 + 124, "FreeDOS / Win 3.1 std", 1, C_TEXT, C_CASE);
    /* 电源灯 + 硬盘灯位置 (硬盘灯的状态在状态栏里) */
    rect(fb, w, x0 + 380, y0 + 180, 24, 10, C_LEDG);
    decor_text(fb, w, x0 + 412, y0 + 177, "POWER", 1, C_TEXT, C_CASE);
    rect(fb, w, x0 + 40, y0 + 200, 120, 20, C_DARK);       /* 电源按钮 */
}

int decor_status(uint16_t *fb, int w, int h, const char *line, int on){
    (void)h;
    int y = DECOR_STATUS_Y;
    rect(fb, w, 0, y, w, DECOR_STATUS_H, on ? C_BAR : 0);
    if (on){
        char buf[100]; int n = 0;
        while (line[n] && n < 98 && (n + 1) * 8 <= w - 32){ buf[n] = line[n]; n++; }
        buf[n] = 0;
        decor_text(fb, w, 16, y + 16, buf, 1, C_BARTX, C_BAR);
    }
    return y;
}
