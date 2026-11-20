# 第三方代码

| 路径 | 来源 | 作者 | 许可证 |
|---|---|---|---|
| `vendor/tiny386/`（除下面几行） | github.com/hchunhui/tiny386，commit `a6a78937a09203da7cd915ce998893169cce922d` | Chunhui He | BSD-3-Clause（`vendor/tiny386/LICENSE`） |
| `vendor/tiny386/i8042.c`、`i8254.c`、`i8259.c`、`ide.c`、`ide.h`、`vga.c`、`pci.c`、`pci.h` | 原出自 QEMU / TinyEMU，经 tiny386 修改 | Fabrice Bellard | MIT 式（文件头原文保留） |
| `vendor/tiny386/i8257.c` | 原出自 QEMU，经 tiny386 修改 | Vassili Karpov (malc) | MIT 式（文件头原文保留） |
| `vendor/tiny386/pcspk.c` | 原出自 QEMU，经 tiny386 修改 | Joachim Henke | MIT 式（文件头原文保留） |
| `vendor/tiny386/ini.c`、`ini.h` | inih | Ben Hoyt | BSD-3-Clause（文件头原文保留） |
| `vendor/tiny386/vgafont.c` | VGA 8x16 / 8x8 字模数据（同 QEMU vgafont.h 一系） | — | 随 tiny386 分发 |

## 不在仓库里、也不编进固件的

- **SeaBIOS / SeaVGABIOS**（LGPL-3.0）：只提交 `scripts/build_seabios.sh`，它从上游拉源码、套 tiny386 的 `seabios/config` 和 `seabios/patch`
  在本地编译；也可以用 `scripts/fetch_bios.sh` 从 tiny386 的 release 里取预编译的 `bios.bin` / `vgabios.bin`。
  生成的 bin 作为独立文件放在设备的 `EFS:/monita/x86/`，运行时读进客户机内存，不嵌进固件。分发 bin 时要附 SeaBIOS 源码出处。
- tiny386 里的 `fmopl.c`（LGPL）、`adlib*`、`ne2000.c`、`sb16.c`、`amd64*`、`kvm*`、`fpu.c` 和 sdl / wasm / android / esp / rawdraw 平台层：没有导入。
  `adlib.h`、`ne2000.h`、`sb16.h` 只为 `pc.h` 能编译而保留，对应函数由 `port/nodev.c` 给空实现。
- FreeDOS（GPL 等）：只提交下载 / 校验 / 制作脚本，不提交镜像。
- Windows 3.1：有版权，维护者自备正版，仓库里只有导入步骤（`docs/import-win31.md`）。

## 维护者自己的代码（再授权）

`fw/link/proto.h`、`fw/lcd/`、`fw/io/`、`fw/sdk/gen_stubs.py`、`fw/sdk/pack.py`、`fw/sdk/validate.py`、`fw/sdk/r05_api.h`、
`mac/` 里的 USB 链路（UsbLink、usbPadded）取自维护者本人的 Monita-CS80 固件仓库，维护者同意以本仓库的 BSD-3-Clause 再授权复用。
`r05_exports.csv`（R05 导出表的哈希清单）属于原厂固件派生数据，不放进本仓库，构建时从 Monita-CS80 仓库读（见 `fw/build.sh`）。
