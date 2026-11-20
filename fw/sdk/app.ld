/* R05 2PPA app link map. VMAs mirror the 原厂 container:
     .corestub -> RAM 0x80fe0000 (loader patches hashes->addrs here)
     .text/.rodata -> XIP flash 0x60271280
     .data -> RAM 0x80fe0640   .bss -> RAM 0x80fe1200
   File layout + load descriptors are produced by pack.py; this script only
   fixes the runtime VMAs. */
ENTRY(appimg_enter)
EXTERN(appimg_exit)

MEMORY {
  STUB  (rwx) : ORIGIN = 0x80fe0000, LENGTH = 0x00000640
  RAM   (rw)  : ORIGIN = 0x80fe0640, LENGTH = 0x00040000
  FLASH (rx)  : ORIGIN = 0x60271280, LENGTH = 0x00090000
}

SECTIONS {
  .corestub 0x80fe0000 : { KEEP(*(.corestub.*)) } > STUB

  .text 0x60271280 : {
    KEEP(*(.text.appimg_enter))
    KEEP(*(.text.appimg_exit))
    *(.text .text.* .glue_7 .glue_7t)
    *(.rodata .rodata.*)
    *(.ARM.extab*)
  } > FLASH

  .data 0x80fe0640 : { *(.data .data.*) } > RAM
  .bss  0x80fe1200 (NOLOAD) : { *(.bss .bss.* COMMON) } > RAM

  /DISCARD/ : { *(.ARM.exidx*) *(.comment) *(.note*) *(.ARM.attributes) }
}
