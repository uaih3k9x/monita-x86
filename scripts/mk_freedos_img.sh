#!/bin/bash
# 下载 FreeDOS 1.4 官方 LiteUSB 镜像 (校验 sha256), 做一块 12 MiB 的 FAT16 盘 work/hda.img。不提交任何镜像。
#   bash scripts/mk_freedos_img.sh [大小 MiB, 默认 12]
# 依赖: curl, unzip, python3, mtools (brew install mtools), clang (汇编 tests/guest 里的自测 .COM)。
#
# 关于 Win3.1: FreeDOS 内核要带 WIN31SUPPORT 才能跑 Win3.1 标准模式。FreeDOS 1.4 的 KERNEL.SYS 是否默认带,
# 官方没写明【推测: 2043 起的发行版默认开】; 维护者装好 Win3.1 后若 win /s 报 DOS 版本 / 内核不兼容,
# 换一份自己编的带 WIN31SUPPORT 的内核 (FreeDOS kernel 源码 config.txt)。Win3.1 要用它自带的 HIMEM.SYS,
# 不用这里默认的 HIMEMX (见 docs/import-win31.md)。
set -euo pipefail
cd "$(dirname "$0")/.."
SIZE=${1:-12}
URL=https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-LiteUSB.zip
SHA=857dcd2ebf9d3d094320154db5fb5b830acba6fb98f981a95a0ca7ab3350338b   # 官方 verify.txt
mkdir -p work/dl
Z=work/dl/FD14-LiteUSB.zip
if [ ! -f "$Z" ]; then
  echo "== 下载 $URL"
  curl -fL --retry 3 -o "$Z.part" "$URL"
  mv "$Z.part" "$Z"
fi
echo "$SHA  $Z" | shasum -a 256 -c -
rm -rf work/dl/lite && mkdir -p work/dl/lite
unzip -q -o "$Z" FD14LITE.img -d work/dl/lite
python3 -I scripts/mk_guest_tools.py
python3 -I scripts/mkimg.py work/dl/lite/FD14LITE.img work/hda.img --size-mib "$SIZE" --extra build/guest
ls -la work/hda.img
shasum -a 256 work/hda.img
