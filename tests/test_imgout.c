/* RGB565 -> PPM / PNG (host/imgout.h): 像素换算、PNG 结构 (CRC / 存储块 / adler32) 自己解一遍对拍。 */
#include "tcheck.h"
#include "../host/imgout.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rd32be(const uint8_t *p){ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void t_pixel(void){
    uint8_t c[3];
    img_565_to_888(0xF800, c); CHECK(c[0] == 255 && c[1] == 0 && c[2] == 0);
    img_565_to_888(0x07E0, c); CHECK(c[0] == 0 && c[1] == 255 && c[2] == 0);
    img_565_to_888(0x001F, c); CHECK(c[0] == 0 && c[1] == 0 && c[2] == 255);
    img_565_to_888(0xFFFF, c); CHECK(c[0] == 255 && c[1] == 255 && c[2] == 255);
    img_565_to_888(0x8410, c); CHECK(c[0] == 132 && c[1] == 130 && c[2] == 132);
    CHECK(img_crc32(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u);
}

/* 解析 PNG: 校验各块 CRC, 把 IDAT 的存储块拼起来, 查 adler32, 和直接换算的扫描行比 */
static void t_png(int w, int h, int stride){
    uint16_t *fb = malloc(sizeof(uint16_t) * (size_t)stride * h);
    for (int i = 0; i < stride * h; i++) fb[i] = (uint16_t)(i * 2654435761u >> 7);
    size_t cap = img_png_size(w, h);
    uint8_t *png = malloc(cap);
    size_t n = img_png_mem(png, cap, fb, w, h, stride);
    CHECK(n == cap);
    CHECK(img_png_mem(png, cap - 1, fb, w, h, stride) == 0);
    n = img_png_mem(png, cap, fb, w, h, stride);
    CHECK(memcmp(png, "\x89PNG\r\n\x1a\n", 8) == 0);
    size_t raw_n = (size_t)h * (1 + 3 * (size_t)w), got = 0;
    uint8_t *raw = malloc(raw_n + 1);
    int saw_end = 0;
    for (size_t o = 8; o + 12 <= n;){
        uint32_t len = rd32be(png + o);
        const uint8_t *type = png + o + 4, *data = png + o + 8;
        CHECK(rd32be(data + len) == img_crc32(0, type, len + 4));
        if (!memcmp(type, "IHDR", 4)) CHECK(rd32be(data) == (uint32_t)w && rd32be(data + 4) == (uint32_t)h && data[8] == 8 && data[9] == 2);
        if (!memcmp(type, "IDAT", 4)){
            CHECK(data[0] == 0x78 && ((data[0] << 8) | data[1]) % 31 == 0);
            const uint8_t *q = data + 2; int final = 0;
            while (!final){
                final = q[0] & 1; CHECK((q[0] >> 1) == 0);
                uint32_t bl = q[1] | q[2] << 8, nl = q[3] | q[4] << 8;
                CHECK((bl ^ nl) == 0xFFFF);
                if (got + bl > raw_n) { CHECK(0); break; }
                memcpy(raw + got, q + 5, bl); got += bl; q += 5 + bl;
            }
            uint32_t a1 = 1, a2 = 0;
            for (size_t i = 0; i < got; i++){ a1 = (a1 + raw[i]) % 65521; a2 = (a2 + a1) % 65521; }
            CHECK(rd32be(q) == ((a2 << 16) | a1));
        }
        if (!memcmp(type, "IEND", 4)) saw_end = 1;
        o += 12 + len;
    }
    CHECK(saw_end && got == raw_n);
    int bad = 0;
    for (int y = 0; y < h; y++){
        const uint8_t *row = raw + (size_t)y * (1 + 3 * (size_t)w);
        if (row[0] != 0) bad++;
        for (int x = 0; x < w; x++){
            uint8_t c[3]; img_565_to_888(fb[y * stride + x], c);
            if (memcmp(c, row + 1 + 3 * x, 3)) bad++;
        }
    }
    CHECK(bad == 0);
    free(fb); free(png); free(raw);
}

static void t_ppm(void){
    uint16_t fb[6] = { 0xF800, 0x07E0, 0x001F, 0, 0xFFFF, 0 };
    const char *path = "build/test_imgout.ppm";
    CHECK(img_write_ppm(path, fb, 2, 2, 3) == 0);
    FILE *f = fopen(path, "rb"); uint8_t b[64]; size_t n = f ? fread(b, 1, sizeof b, f) : 0; if (f) fclose(f);
    CHECK(n == 11 + 12 && memcmp(b, "P6\n2 2\n255\n", 11) == 0);
    CHECK(b[11] == 255 && b[12] == 0 && b[15] == 255 && b[17] == 0 && b[20] == 255 && b[22] == 255);
    remove(path);
}

int main(void){
    t_pixel(); t_ppm();
    t_png(5, 3, 7); t_png(800, 1280, 800); t_png(1, 1, 1);
    return tc_done("imgout");
}
