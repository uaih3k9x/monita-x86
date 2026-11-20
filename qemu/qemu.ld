/* QEMU virt: RAM 从 0x40000000 起; 代码 / 数据 / 栈在前 15 MB, 后面是预载的文件和堆 (见 qemu/qplat.c) */
ENTRY(_start)
SECTIONS {
  . = 0x40000000;
  .text : { KEEP(*(.text.start)) *(.text .text.*) *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); __bss_end = .; }
  . = ALIGN(16);
  __stack_bottom = .;
  . = 0x40E00000;
  __stack_top = .;
  /DISCARD/ : { *(.ARM.exidx*) *(.comment) *(.note*) *(.ARM.attributes) }
}
