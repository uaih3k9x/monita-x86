/* QEMU 外壳共用: CPU 异常 (对齐检查打开后的非对齐访问等) 打到串口, 半主机退出码 3 */
#include <stdint.h>
int semi(int op, void *arg);
static void put(const char *s){ for (; *s; s++) *(volatile uint32_t *)0x09000000 = (uint8_t)*s; }
static void hex(uint32_t v){ char b[11] = "0x"; for (int i = 0; i < 8; i++) b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15]; b[10] = 0; put(b); }
static volatile int busy;
void qabort(uint32_t kind, uint32_t lr, uint32_t far, uint32_t fsr){
    if (busy){ struct { uint32_t r, c; } a = { 0x20026, 4 }; semi(0x20, &a); for (;;); }   /* 处理函数自己又出错 */
    busy = 1;
    put("\r\nCPU EXCEPTION kind "); hex(kind); put(" (1 未定义 3 预取中止 4 数据中止) lr "); hex(lr);
    put(" far "); hex(far); put(" fsr "); hex(fsr); put("\r\n");
    struct { uint32_t r, c; } a = { 0x20026, 3 };
    semi(0x20, &a);
    for (;;);
}
