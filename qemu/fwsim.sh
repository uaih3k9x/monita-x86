#!/bin/bash
# fwsim: 真的固件代码 (fw/build.sh 编出的 .o, 去掉 loader 桩) + qemu/r05sim.c 假装的 29 个 R05 导入, 跑在 QEMU 里。不连设备。
#   bash qemu/fwsim.sh freedos|win31 [秒数, 默认 20 / 40] [shift, 默认 2]
#   SIM_MISSING=bios|vgabios|hda  假装 EFS 上缺这个文件 (固件应该停在应答循环里, 屏上打原因)
#   SIM_HEAP=<字节>               假堆大小 (默认 4.35 MB = Monita 实机测到的最大块)
# 产物: build/fwsim/<场景>.log 和 .png。
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/armflags.sh
SCEN=${1:-freedos}; SH=${3:-2}
case $SCEN in freedos) S=0; IMG=work/hda.img; SECS=${2:-20};; win31) S=1; IMG=work/win31.img; SECS=${2:-40};; *) exit 2;; esac
bash fw/build.sh > /dev/null
O=build/fwsim; mkdir -p $O/o; rm -f $O/fwsim-fb.raw $O/qemu.pid
$ARM_CC $ARM_CFLAGS $ARM_DEFS -Ifw/link -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-misleading-indentation -Werror \
  -c qemu/r05sim.c -o $O/o/r05sim.o
$ARM_CC -mcpu=cortex-a5 -c qemu/start.S -o $O/o/start.o
OBJS=$(ls build/fw/o/*.o | grep -v corestub)
arm-none-eabi-ld -T qemu/fwsim.ld --gc-sections -o $O/fwsim.elf $OBJS $O/o/r05sim.o $O/o/start.o
sz(){ stat -f %z "$1"; }
ZB=$(sz work/bios.bin); ZV=$(sz work/vgabios.bin); ZH=$(sz $IMG)
case ${SIM_MISSING:-} in bios) ZB=0;; vgabios) ZV=0;; hda) ZH=0;; esac      # 假装 EFS 上缺这个文件
LOG=$O/$SCEN.log
(cd $O && exec qemu-system-arm -M virt -cpu cortex-a7 -m 1536 -display none -nic none -monitor none \
  -serial stdio -semihosting -icount shift=$SH,sleep=off -pidfile qemu.pid -kernel fwsim.elf \
  -device loader,file=../../work/bios.bin,addr=0x41000000,force-raw=on \
  -device loader,file=../../work/vgabios.bin,addr=0x41100000,force-raw=on \
  -device loader,file=../../$IMG,addr=0x42000000,force-raw=on \
  -device loader,addr=0x40F00000,data=$ZB,data-len=4 \
  -device loader,addr=0x40F00004,data=$ZV,data-len=4 \
  -device loader,addr=0x40F00008,data=$ZH,data-len=4 \
  -device loader,addr=0x40F0000c,data=$SH,data-len=4 \
  -device loader,addr=0x40F00010,data=$S,data-len=4 \
  -device loader,addr=0x40F00018,data=$SECS,data-len=4 \
  -device loader,addr=0x40F00020,data=${SIM_HEAP:-0},data-len=4) > $LOG 2>&1 &
QP=$!
trap '[ -f $O/qemu.pid ] && kill $(cat $O/qemu.pid) 2>/dev/null || true' EXIT
T0=$(date +%s)
while kill -0 $QP 2>/dev/null; do sleep 2; [ $(( $(date +%s) - T0 )) -gt 1800 ] && { echo "墙钟超时"; break; }; done
wait $QP && RC=0 || RC=$?
cat $LOG
[ -f $O/fwsim-fb.raw ] && python3 -I scripts/raw565png.py $O/fwsim-fb.raw 800 1280 $O/$SCEN.png && echo "截图 $O/$SCEN.png"
exit $RC
