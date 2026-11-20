# 固件和 QEMU 估速共用的交叉编译参数和源文件 (被 fw/build.sh、qemu/run.sh source)。
# 和 Monita 主固件同一组 (fw/build.sh), 区别: -O2 (解释器热路径), -mfloat-abi=soft (整个 app 不碰 VFP/NEON,
# 内核任务切换是否保存 VFP 现场没证实), -DNDEBUG, -mno-unaligned-access (R05 上非对齐访问会不会 fault 没核实;
# 不加的话 tiny386 的 fetch16 等会对 phys_mem+奇数地址做 ldrh, 对齐检查打开时第一条客户机指令就数据中止)。
ARM_CC=arm-none-eabi-gcc
ARM_CFLAGS="-mcpu=cortex-a5 -mthumb -mfloat-abi=soft -mlong-calls -mno-unaligned-access -O2 -ffreestanding -fno-builtin \
  -fno-tree-loop-distribute-patterns -fno-tree-vectorize -ffunction-sections -fdata-sections -nostdlib -DNDEBUG"
ARM_DEFS="-DMONITA_X86 -DMONITA_FW -DBPP=16 -DVGA_STORE_CHANGED -DI386_ALIGNED_ONLY -Ivendor/tiny386 -Icore -Iport -Ifw/link"
ARM_VSRC="vendor/tiny386/pc.c vendor/tiny386/i8042.c vendor/tiny386/i8254.c vendor/tiny386/i8257.c vendor/tiny386/i8259.c
  vendor/tiny386/ide.c vendor/tiny386/vga.c vendor/tiny386/vgafont.c vendor/tiny386/pcspk.c vendor/tiny386/pci.c
  vendor/tiny386/misc.c vendor/tiny386/i386.c"
ARM_CSRC="core/mxalloc.c core/overlay.c core/rcache.c core/mxblk.c core/machine.c core/decor.c port/nodev.c
  port/aeabi_div.c port/mxlibc.c"

# arm_objs <输出目录>: 编出全部公共 .o (vendor 不报警告, 自己的代码 -Werror)
arm_objs(){
  local D=$1 f; mkdir -p "$D"
  for f in $ARM_VSRC; do
    $ARM_CC $ARM_CFLAGS $ARM_DEFS -include core/mxvendor.h -w -c "$f" -o "$D/$(basename "$f" .c).o" || return 1
  done
  for f in $ARM_CSRC; do
    $ARM_CC $ARM_CFLAGS $ARM_DEFS -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-misleading-indentation -Werror -c "$f" -o "$D/$(basename "$f" .c).o" || return 1
  done
}
