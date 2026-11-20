/* QEMU virt (cortex-a7) 裸机入口: 栈、平铺页表 (1 MB 段; 0..1 GB 设备, 其余普通内存可缓存), 开 MMU / cache, 进 qmain。
 * 开 MMU 是为了和设备一样允许非对齐访问 (MMU 关时内存算强序, 非对齐会 fault)。semi() 是 ARM 半主机调用。 */
    .syntax unified
    .arm
    .section .text.start, "ax"
    .globl _start
_start:
    ldr   sp, =__stack_top
    /* 清 bss (页表也在 bss 里, 先清) */
    ldr   r0, =__bss_start
    ldr   r1, =__bss_end
    mov   r2, #0
2:  cmp   r0, r1
    strlo r2, [r0], #4
    blo   2b
    /* 页表 */
    ldr   r0, =ttb
    mov   r1, #0
1:  mov   r2, r1, lsl #20
    cmp   r1, #0x400
    orrlo r2, r2, #0x0c00
    orrlo r2, r2, #0x0006          /* 设备: AP=11, B=1 */
    orrhs r2, r2, #0x1c00
    orrhs r2, r2, #0x000e          /* 普通内存 WBWA: TEX=001 C=1 B=1 */
    str   r2, [r0, r1, lsl #2]
    add   r1, r1, #1
    cmp   r1, #0x1000
    bne   1b
    mcr   p15, 0, r0, c2, c0, 0    /* TTBR0 */
    mov   r1, #0
    mcr   p15, 0, r1, c2, c0, 2    /* TTBCR = 0 */
    mvn   r1, #0
    mcr   p15, 0, r1, c3, c0, 0    /* DACR: 全部 manager */
    mcr   p15, 0, r1, c8, c7, 0    /* TLBIALL (r1 忽略) */
    mrc   p15, 0, r1, c1, c0, 0
    orr   r1, r1, #0x1             /* M */
    orr   r1, r1, #0x4             /* C */
    orr   r1, r1, #0x1000          /* I */
    orr   r1, r1, #0x800           /* Z */
    bic   r1, r1, #0x2             /* A = 0: 允许非对齐 */
    mcr   p15, 0, r1, c1, c0, 0
    isb
    blx   qmain
3:  b     3b

    .globl semi
semi:                               /* int semi(int op, void *arg) */
    svc   0x123456
    bx    lr

    .bss
    .balign 16384
ttb: .space 16384
