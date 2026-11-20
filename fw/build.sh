#!/bin/bash
# monita-x86 固件: 编译 + 打包 2PPA + 静态校验。不连设备、不刷机。
#   bash fw/build.sh            -> build/fw/app_x86.bin (写到 NOR 0x60270000 app 槽, 刷写由 9x 亲手做, 见 README)
# 依赖: arm-none-eabi-gcc / binutils, python3; R05 导出表哈希 (原厂派生数据, 不在本仓库):
#   R05_EXPORTS=<路径> 或默认 ~/code/Monita-CS80-lakala/fw/sdk/r05_exports.csv
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/armflags.sh
export R05_EXPORTS=${R05_EXPORTS:-$HOME/code/Monita-CS80-lakala/fw/sdk/r05_exports.csv}
[ -f "$R05_EXPORTS" ] || { echo "缺 R05 导出表: $R05_EXPORTS"; exit 1; }
O=build/fw; rm -rf $O; mkdir -p $O/o
arm_objs $O/o
FWINC="-Ifw -Ifw/sdk -Ifw/lcd -Ifw/link -Icore"
W="-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-misleading-indentation -Wno-unused-function -Werror"
for f in fw/app_x86.c fw/x86_plat.c; do
  $ARM_CC $ARM_CFLAGS $ARM_DEFS $FWINC $W -DMX_BUILD_EPOCH=$(date +%s)u -c $f -o $O/o/$(basename $f .c).o
done
python3 -I fw/sdk/gen_stubs.py fw/app_x86.imports $O/corestub.S $O/hashes.txt
$ARM_CC $ARM_CFLAGS -c $O/corestub.S -o $O/o/corestub.o
arm-none-eabi-ld -T fw/sdk/app.ld --gc-sections -o $O/app_x86.elf $O/o/*.o
arm-none-eabi-size -A $O/app_x86.elf | grep -E "^\.(corestub|text|data|bss) "
(cd $O && python3 -I ../../fw/sdk/pack.py app_x86.elf app_x86.bin)
python3 -I fw/sdk/validate.py $O/app_x86.bin $O/app_x86.elf --target x86
shasum -a 256 $O/app_x86.bin
