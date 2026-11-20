#!/bin/bash
# 取 tiny386 release 里预编译的 SeaBIOS / SeaVGABIOS (LGPL-3.0, 源码见 scripts/build_seabios.sh 的出处说明) 到 work/。
#   bash scripts/fetch_bios.sh
# release 名叫 continuous, 会被上游覆盖, 所以 sha256 钉不住: 这里只打印, 维护者自己记一份上机用的。
# 本仓库验证用的那份 (2026-11-18 构建, SeaBIOS rel-1.17.0-24-g28aa4634):
#   bios.bin    9be3e26ccfbf9a1bcc0e4506962679ecbe9edf1bb9cad9b2e03ab735be35b5b2  131072 B
#   vgabios.bin 276b090d8a0b617c5b91559a72079ce26829a22da12976059d7536fb21acdbf0   39424 B
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p work/dl
T=work/dl/tiny386.tar.xz
[ -f "$T" ] || curl -fL --retry 3 -o "$T" https://github.com/hchunhui/tiny386/releases/download/continuous/tiny386.tar.xz
tar xJf "$T" -C work/dl tiny386/bios.bin tiny386/vgabios.bin
cp work/dl/tiny386/bios.bin work/dl/tiny386/vgabios.bin work/
shasum -a 256 work/bios.bin work/vgabios.bin
