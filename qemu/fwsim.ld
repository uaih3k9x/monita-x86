/* fwsim: 固件的 .text / .data / .bss 放在设备上的地址 (同 fw/sdk/app.ld), 启动代码 / 页表 / 栈在 0x40000000 起 */
ENTRY(_start)
SECTIONS {
  .start 0x40000000 : { KEEP(*start.o(.text.start)) }
  .startbss 0x40010000 (NOLOAD) : { *start.o(.bss) }
  __stack_bottom = 0x40100000;
  __stack_top = 0x40E00000;
  .text 0x60271280 : { *(.text .text.* .glue_7 .glue_7t) *(.rodata .rodata.*) }
  .data 0x80fe0640 : { *(.data .data.*) }
  .bss 0x80fe1200 (NOLOAD) : { __bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); __bss_end = .; }
  /DISCARD/ : { *(.ARM.exidx*) *(.comment) *(.note*) *(.ARM.attributes) }
}
