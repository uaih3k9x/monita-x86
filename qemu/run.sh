#!/bin/bash
# QEMU 估速: 和固件同一批 .o 跑在 qemu-system-arm (virt, cortex-a7, 无窗口, -icount)。不连设备。
#   bash qemu/run.sh freedos [shift]          # work/hda.img 到 "monita-x86 FreeDOS ready"
#   bash qemu/run.sh win31   [shift] [尾秒]   # work/win31.img 进图形模式后再跑尾秒 (默认 20)
# shift: -icount shift, 1 条 ARM 指令 = 2^shift ns 虚拟时间 (1 -> 有效 500 MIPS, 2 -> 250, 3 -> 125)。
# 产物: build/qemu/<场景>-s<shift>.log 和 .png (帧缓冲截图)。QEMU 用 pidfile 管, 结束 / 超时都只杀自己起的那个。
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/armflags.sh
SCEN=${1:-freedos}; SH=${2:-2}; TAIL=${3:-20}; LIMIT=${LIMIT:-600}; REFRESH_US=${REFRESH_US:-0}
case $SCEN in freedos) S=0; IMG=work/hda.img;; win31) S=1; IMG=work/win31.img;; *) echo "场景: freedos | win31"; exit 2;; esac
for f in work/bios.bin work/vgabios.bin $IMG; do [ -f $f ] || { echo "缺 $f (见 README)"; exit 1; }; done
O=build/qemu; mkdir -p $O/o
arm_objs $O/o
$ARM_CC $ARM_CFLAGS $ARM_DEFS -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -Werror -c qemu/qplat.c -o $O/o/qplat.o
$ARM_CC -mcpu=cortex-a5 -c qemu/start.S -o $O/o/start.o
arm-none-eabi-ld -T qemu/qemu.ld --gc-sections -o $O/x86q.elf $O/o/*.o
arm-none-eabi-size $O/x86q.elf
sz(){ stat -f %z "$1"; }
ZB=$(sz work/bios.bin); ZV=$(sz work/vgabios.bin); ZH=$(sz $IMG)
LOG=$O/$SCEN-s$SH.log; PID=$O/qemu.pid; rm -f $PID $O/qemu-fb.raw
(cd $O && exec qemu-system-arm -M virt -cpu cortex-a7 -m 256 -display none -nic none -monitor none \
  -serial stdio -semihosting -icount shift=$SH,sleep=off -pidfile qemu.pid -kernel x86q.elf ${QEMU_EXTRA:-} \
  -device loader,file=../../work/bios.bin,addr=0x41000000,force-raw=on \
  -device loader,file=../../work/vgabios.bin,addr=0x41100000,force-raw=on \
  -device loader,file=../../$IMG,addr=0x42000000,force-raw=on \
  -device loader,addr=0x40F00000,data=$ZB,data-len=4 \
  -device loader,addr=0x40F00004,data=$ZV,data-len=4 \
  -device loader,addr=0x40F00008,data=$ZH,data-len=4 \
  -device loader,addr=0x40F0000c,data=$SH,data-len=4 \
  -device loader,addr=0x40F00010,data=$S,data-len=4 \
  -device loader,addr=0x40F00014,data=$TAIL,data-len=4 \
  -device loader,addr=0x40F00018,data=$LIMIT,data-len=4 \
  -device loader,addr=0x40F0001c,data=$REFRESH_US,data-len=4) > $LOG 2>&1 &
QP=$!
trap '[ -f $PID ] && kill $(cat $PID) 2>/dev/null || true' EXIT
T0=$(date +%s)
while kill -0 $QP 2>/dev/null; do
  sleep 5
  if [ $(( $(date +%s) - T0 )) -gt 3600 ]; then echo "墙钟超过 1 小时, 停"; break; fi
done
wait $QP || true
cat $LOG | tail -5
echo "墙钟 $(( $(date +%s) - T0 )) s"
if [ -f $O/qemu-fb.raw ]; then
  python3 -I scripts/raw565png.py $O/qemu-fb.raw 800 1280 $O/$SCEN-s$SH.png && echo "截图 $O/$SCEN-s$SH.png"
fi
