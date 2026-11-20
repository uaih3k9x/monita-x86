/*
 * PMTEST.COM: 客户机里跑的 286 风格保护模式自测 (DOS 下, 实模式起步)。用 clang 的 i386 目标汇编, scripts/mk_guest_tools.py 抠出 .COM。
 *   1. SMSW 看 PE: 已经在保护模式 / V86 (比如装了 JEMM386) 就退出 (2)
 *   2. 建 GDT (16 位代码段 / 数据段 / 基址 2.5 MB 的数据段), LGDT, 用 286 的 LMSW 置 PE, 远跳刷新
 *   3. 保护模式里: LSL / LAR / VERR / VERW 查描述符, 往 2.5 MB 处写 256 字节样式再读回
 *   4. 清 PE 回实模式 (386 写法 MOV CR0), 远跳回实模式段
 *   5. 用 INT 15h AH=87h (BIOS 扩展内存块搬移, 它自己再进一次保护模式) 把 2.5 MB 处搬回来比对
 * 打印 "PMTEST OK" 退出码 0, 否则 "PMTEST FAIL n" 退出码 1。
 * .text 开头垫 0x100 字节, 段内偏移就是 COM 里的偏移; mk_guest_tools.py 自己按基址 0 做重定位, 不需要链接器。
 */
    .intel_syntax noprefix
    .code16
    .text
    .globl _start
#define A(x) x
/* clang 在 .code16 里把 call 编成 32 位 (66 e8, 压 4 字节返回地址), 和 16 位 ret 对不上: 手写近调用 */
#define CALL(f) .byte 0xe8; .word (f) - . - 2
/* ret 同理会被编成 66 c3 (弹 4 字节), 手写 16 位近返回 */
#define RET .byte 0xc3
    .fill 0x100, 1, 0           /* PSP 的位置: .text 偏移 = COM 里的偏移, 抠的时候去掉 */
_start:
    mov dx, offset m_start
    CALL(puts)
    smsw ax
    test al, 1
    jz 1f
    mov dx, offset m_v86
    CALL(puts)
    mov ax, 0x4c02
    int 0x21
1:
    /* GDT 里填实模式段基址 (CS = DS = SS, COM) */
    mov ax, cs
    movzx eax, ax
    shl eax, 4
    mov bx, offset gdt_code
    CALL(setbase)
    mov bx, offset gdt_data
    CALL(setbase)
    add eax, offset gdt
    mov dword ptr [A(gdtr) + 2], eax
    mov word ptr [A(rm_cs)], cs

    cli
    in al, 0x92                 /* 快速 A20 */
    or al, 2
    and al, 0xfe
    out 0x92, al
    lgdt [A(gdtr)]
    smsw ax
    or ax, 1
    lmsw ax                     /* 286 的置 PE 方式 */
    .byte 0xea                  /* jmp far 0x08:pm16 */
    .word A(pm16)
    .word 0x08
pm16:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    /* 描述符检查指令 */
    mov ax, 0x18
    lsl cx, ax
    jnz fail_pm1
    cmp cx, 0xffff
    jne fail_pm1
    lar cx, ax
    jnz fail_pm1
    cmp ch, 0x92                /* P=1 DPL=0 S=1 数据可写 */
    jne fail_pm1
    verr ax
    jnz fail_pm1
    verw ax
    jnz fail_pm1
    mov ax, 0x08
    verw ax                     /* 代码段不可写: ZF = 0 */
    jz fail_pm1
    /* 往 2.5 MB 写样式, 读回 */
    mov ax, 0x18
    mov es, ax
    xor di, di
    mov cx, 256
2:  mov al, cl
    xor al, 0x5a
    mov es:[di], al
    inc di
    loop 2b
    xor di, di
    mov cx, 256
3:  mov al, cl
    xor al, 0x5a
    cmp es:[di], al
    jne fail_pm2
    inc di
    loop 3b
    mov byte ptr [A(stage)], 0
    jmp to_real
fail_pm1:
    mov byte ptr [A(stage)], 1
    jmp to_real
fail_pm2:
    mov byte ptr [A(stage)], 2
to_real:
    mov ax, 0x10
    mov es, ax
    mov eax, cr0
    and al, 0xfe
    mov cr0, eax
    .byte 0xea                  /* jmp far rm_cs:rm16 (段在运行时填) */
    .word A(rm16)
rm_cs:
    .word 0
rm16:
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    sti
    cmp byte ptr [A(stage)], 0
    jne fail
    /* INT 15h AH=87h: 从 2.5 MB 搬 256 字节到 buf */
    mov ax, cs
    movzx eax, ax
    shl eax, 4
    add eax, offset buf
    mov word ptr [A(mv_dst) + 2], ax
    shr eax, 16
    mov byte ptr [A(mv_dst) + 4], al
    mov si, offset mvgdt
    mov cx, 128                 /* 字数 */
    mov ah, 0x87
    int 0x15
    mov byte ptr [A(stage)], 3
    jc fail
    mov si, offset buf
    mov cx, 256
4:  mov al, cl
    xor al, 0x5a
    cmp [si], al
    jne fail4
    inc si
    loop 4b
    mov dx, offset m_ok
    CALL(puts)
    mov ax, 0x4c00
    int 0x21
fail4:
    mov byte ptr [A(stage)], 4
fail:
    mov dx, offset m_fail
    CALL(puts)
    mov dl, [A(stage)]
    add dl, '0'
    mov ah, 2
    int 0x21
    mov dx, offset m_crlf
    CALL(puts)
    mov ax, 0x4c01
    int 0x21

/* eax = 基址, bx = 描述符地址 */
setbase:
    push eax
    mov [bx + 2], ax
    shr eax, 16
    mov [bx + 4], al
    pop eax
    RET
puts:
    mov ah, 9
    int 0x21
    RET

m_start: .ascii "PMTEST: 286 protected mode self-test\r\n$"
m_v86:   .ascii "PMTEST: already in protected/V86 mode, skipped\r\n$"
m_ok:    .ascii "PMTEST OK\r\n$"
m_fail:  .ascii "PMTEST FAIL $"
m_crlf:  .ascii "\r\n$"
stage:   .byte 9
    .balign 8
gdt:
    .quad 0
gdt_code: .word 0xffff, 0
    .byte 0, 0x9a, 0, 0         /* 16 位代码, 基址运行时填 */
gdt_data: .word 0xffff, 0
    .byte 0, 0x92, 0, 0         /* 16 位数据 */
    .word 0xffff, 0x0000
    .byte 0x28, 0x92, 0, 0      /* 基址 0x280000 (2.5 MB), 限长 64 KiB */
gdt_end:
gdtr: .word gdt_end - gdt - 1
    .long 0
/* INT 15h AH=87h 的 GDT: 0 空, 8 GDT 自己, 0x10 源, 0x18 目的, 0x20 / 0x28 BIOS 用 */
    .balign 8
mvgdt:
    .quad 0, 0
    .word 0xffff, 0x0000
    .byte 0x28, 0x93, 0, 0      /* 源 = 0x280000 */
mv_dst: .word 0xffff, 0x0000
    .byte 0x00, 0x93, 0, 0      /* 目的 = buf (运行时填) */
    .quad 0, 0
buf: .fill 256, 1, 0
