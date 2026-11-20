/* QEMU virt (cortex-a7) 裸机入口: 栈、平铺页表 (1 MB 段; 0..1 GB 设备, 其余普通内存可缓存), 开 MMU / cache, 进 qmain。
 * 对齐检查打开 (SCTLR.A = 1): 按"严格对齐"跑, 任何非对齐访问都数据中止 -> qabort() 打出地址, 半主机退出码 3。
 * 设备上非对齐访问会不会 fault 没核实, 所以代码一律 -mno-unaligned-access 编, 这里用最严的设定把漏网的抓出来。
 * semi() 是 ARM 半主机调用。 */
    .syntax unified
    .arm
    .section .text.start, "ax"
    .globl _start
_start:
    /* 异常向量 (VBAR) + 中止 / 未定义模式的栈 */
    ldr   r0, =vectors
    mcr   p15, 0, r0, c12, c0, 0
    cps   #0x17                    /* ABT */
    ldr   sp, =__stack_top
    cps   #0x1b                    /* UND */
    ldr   sp, =__stack_top
    cps   #0x13                    /* SVC */
    ldr   sp, =__stack_top - 0x1000
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
    orr   r1, r1, #0x2             /* A = 1: 严格对齐 */
    mcr   p15, 0, r1, c1, c0, 0
    isb
    blx   qmain
3:  b     3b

    .balign 32
vectors:                            /* 0 复位 1 未定义 2 SVC 3 预取中止 4 数据中止 5 - 6 IRQ 7 FIQ */
    b     .
    b     und_h
    b     .
    b     pabt_h
    b     dabt_h
    b     .
    b     .
    b     .
und_h:  mov r0, #1
        mrc p15, 0, r2, c6, c0, 2   /* IFAR (未定义时无意义) */
        b   1f
pabt_h: mov r0, #3
        mrc p15, 0, r2, c6, c0, 2   /* IFAR */
        mrc p15, 0, r3, c5, c0, 1   /* IFSR */
        b   1f
dabt_h: mov r0, #4
        mrc p15, 0, r2, c6, c0, 0   /* DFAR */
        mrc p15, 0, r3, c5, c0, 0   /* DFSR */
1:      mov r1, lr
        blx qabort                  /* void qabort(kind, lr, far, fsr): 打出来, 半主机退出, 不返回 */
        b   .

    .globl semi
semi:                               /* int semi(int op, void *arg) */
    svc   0x123456
    bx    lr

    .bss
    .balign 16384
ttb: .space 16384
