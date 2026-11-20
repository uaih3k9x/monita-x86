/*
 * 竖屏 800x1280 上客户机画面之外的装饰 (拍视频用): 上方一块"机箱面板", 下方一条状态栏。
 * 客户机画面由 vga.c 居中: 640x480 占 y 400..879, 文本 720x400 占 y 440..839。面板画在 y < DECOR_PANEL_H,
 * 状态栏在 DECOR_STATUS_Y 起 DECOR_STATUS_H 行, 都不碰客户机区域。字模用 vgafont.c 的 8x16。
 */
#ifndef MX_DECOR_H
#define MX_DECOR_H
#include <stdint.h>

#define DECOR_PANEL_H   360
#define DECOR_STATUS_Y  1180
#define DECOR_STATUS_H  48

void decor_text(uint16_t *fb, int stride, int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg);
void decor_panel(uint16_t *fb, int w, int h);
/* 画状态栏 (on = 0 时清成背景), 返回起始行 */
int decor_status(uint16_t *fb, int w, int h, const char *line, int on);

#endif
