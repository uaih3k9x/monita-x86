/*
 * VGATEST.COM: 切 VGA 图形模式画样式, 给 host 版截图验证图形导出 (scripts/host_boot_test.sh)。
 *   12h (640x480x16, 平面模式, 写模式 2): 16 条竖色带; 按任意键
 *   13h (320x200x256, 线性): 每行颜色 = 行号; 按任意键
 *   回 03h 文本模式, 打印 "VGATEST DONE"
 */
    .intel_syntax noprefix
    .code16
    .text
    .globl _start
#define A(x) x
/* clang 在 .code16 里把 call 编成 32 位 (66 e8, 压 4 字节返回地址), 和 16 位 ret 对不上: 手写近调用 */
#define CALL(f) .byte 0xe8; .word (f) - . - 2
    .fill 0x100, 1, 0           /* PSP 的位置: .text 偏移 = COM 里的偏移, 抠的时候去掉 */
_start:
    mov ax, 0x0012
    int 0x10
    /* 写模式 2: 写进去的字节低 4 位 = 颜色, 位掩码选像素 */
    mov dx, 0x3ce
    mov ax, 0x0205              /* GR5 = 2 */
    out dx, ax
    mov ax, 0xff08              /* GR8 位掩码 = ff */
    out dx, ax
    mov ax, 0xa000
    mov es, ax
    xor di, di
    mov bx, 480
1:  mov cx, 80                  /* 一行 80 字节 = 640 像素 */
    xor si, si
2:  mov ax, si
    xor dx, dx
    mov bp, 5
    div bp                      /* 每 5 字节 (40 像素) 一种颜色: 80 / 5 = 16 条 */
    mov ah, es:[di]             /* 读一下装锁存器 */
    mov es:[di], al             /* 写模式 2: 低 4 位是颜色 */
    inc di
    inc si
    loop 2b
    dec bx
    jnz 1b
    mov dx, 0x3ce
    mov ax, 0x0005              /* 写模式回 0 */
    out dx, ax
    xor ah, ah
    int 0x16

    mov ax, 0x0013
    int 0x10
    xor di, di
    xor dx, dx
3:  mov al, dl
    mov cx, 320
    rep stosb
    inc dx
    cmp dx, 200
    jne 3b
    xor ah, ah
    int 0x16

    mov ax, 0x0003
    int 0x10
    mov dx, offset m_done
    mov ah, 9
    int 0x21
    mov ax, 0x4c00
    int 0x21
m_done: .ascii "VGATEST DONE\r\n$"
