/*
 * ASCII / 键名 -> PC set-1 扫描码 (美式键盘)。主机脚本注入用 (host/main.c 的 type / key 命令);
 * Mac 端转发工具 mac/x86kbd.py 用同一套扫描码 (已对拍过全部可打印字符)。纯逻辑, 单测 tests/test_kbdmap.c。
 * 返回值: 低 8 位扫描码, KM_SHIFT = 需要按着 Shift, KM_E0 = E0 扩展键; 0 = 不认识。
 */
#ifndef MX_KBDMAP_H
#define MX_KBDMAP_H
#include <stdint.h>
#include <string.h>

#define KM_SHIFT 0x100u
#define KM_E0    0x200u
#define SC_LSHIFT 0x2Au
#define SC_LCTRL  0x1Du
#define SC_LALT   0x38u

static inline uint32_t km_ascii(char ch){
    static const char row1[] = "1234567890-=";        /* 0x02.. */
    static const char row1s[] = "!@#$%^&*()_+";
    static const char rowq[] = "qwertyuiop[]";        /* 0x10.. */
    static const char rowqs[] = "QWERTYUIOP{}";
    static const char rowa[] = "asdfghjkl;'`";        /* 0x1E.. */
    static const char rowas[] = "ASDFGHJKL:\"~";
    static const char rowz[] = "\\zxcvbnm,./";        /* 0x2B, 0x2C.. */
    static const char rowzs[] = "|ZXCVBNM<>?";
    const char *p;
    if (!ch) return 0;
    if ((p = strchr(row1, ch)))  return 0x02u + (uint32_t)(p - row1);
    if ((p = strchr(row1s, ch))) return KM_SHIFT | (0x02u + (uint32_t)(p - row1s));
    if ((p = strchr(rowq, ch)))  return 0x10u + (uint32_t)(p - rowq);
    if ((p = strchr(rowqs, ch))) return KM_SHIFT | (0x10u + (uint32_t)(p - rowqs));
    if ((p = strchr(rowa, ch)))  return 0x1Eu + (uint32_t)(p - rowa);
    if ((p = strchr(rowas, ch))) return KM_SHIFT | (0x1Eu + (uint32_t)(p - rowas));
    if ((p = strchr(rowz, ch)))  return 0x2Bu + (uint32_t)(p - rowz);
    if ((p = strchr(rowzs, ch))) return KM_SHIFT | (0x2Bu + (uint32_t)(p - rowzs));
    switch (ch){
    case ' ': return 0x39; case '\n': case '\r': return 0x1C; case '\t': return 0x0F;
    case '\b': return 0x0E; case 27: return 0x01;
    }
    return 0;
}

static inline uint32_t km_name(const char *s){
    static const struct { const char *n; uint32_t c; } t[] = {
        { "esc", 0x01 }, { "enter", 0x1C }, { "tab", 0x0F }, { "bksp", 0x0E }, { "space", 0x39 },
        { "ctrl", SC_LCTRL }, { "shift", SC_LSHIFT }, { "alt", SC_LALT },
        { "f1", 0x3B }, { "f2", 0x3C }, { "f3", 0x3D }, { "f4", 0x3E }, { "f5", 0x3F }, { "f6", 0x40 },
        { "f7", 0x41 }, { "f8", 0x42 }, { "f9", 0x43 }, { "f10", 0x44 }, { "f11", 0x57 }, { "f12", 0x58 },
        { "up", KM_E0 | 0x48 }, { "down", KM_E0 | 0x50 }, { "left", KM_E0 | 0x4B }, { "right", KM_E0 | 0x4D },
        { "home", KM_E0 | 0x47 }, { "end", KM_E0 | 0x4F }, { "pgup", KM_E0 | 0x49 }, { "pgdn", KM_E0 | 0x51 },
        { "ins", KM_E0 | 0x52 }, { "del", KM_E0 | 0x53 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp(s, t[i].n)) return t[i].c;
    if (s[0] && !s[1]) return km_ascii(s[0]);
    return 0;
}

#endif
