# 第三方代码

| 路径 | 来源 | 作者 | 许可证 |
|---|---|---|---|
| `vendor/tiny386/`（除下面几行） | github.com/hchunhui/tiny386，commit `a6a78937a09203da7cd915ce998893169cce922d` | Chunhui He | BSD-3-Clause（`vendor/tiny386/LICENSE`） |
| `vendor/tiny386/i8042.c`、`i8254.c`、`i8259.c`、`ide.c`、`ide.h`、`vga.c`、`pci.c`、`pci.h` | 原出自 QEMU / TinyEMU，经 tiny386 修改 | Fabrice Bellard | MIT 式（文件头原文保留） |
| `vendor/tiny386/i8257.c` | 原出自 QEMU，经 tiny386 修改 | Vassili Karpov (malc) | MIT 式（文件头原文保留） |
| `vendor/tiny386/pcspk.c` | 原出自 QEMU，经 tiny386 修改 | Joachim Henke | MIT 式（文件头原文保留） |
| `vendor/tiny386/ini.c`、`ini.h` | inih | Ben Hoyt | BSD-3-Clause（文件头原文保留） |
| `vendor/tiny386/vgafont.c` | VGA 8x16 / 8x8 字模数据（同 QEMU vgafont.h 一系） | — | 随 tiny386 分发 |

## 对 tiny386 的改动

都包在 `MONITA_X86` / `MONITA_FW` 宏里（i386.c 的修正除外），`git log -p vendor/tiny386` 可查：
- `i386.c`：修 VERW 对代码段返回 ZF=1 的错（代码段永远不可写；tests/guest/pmtest.S 第 1 阶段查这个，QEMU 对照通过）。
- `ide.c`：盘改走 `core/mxblk.c`（只读打开 + 内存写覆盖层）。
- `pc.c`：设备版的 `PC_STEP_COUNT` / `pcmalloc_init`；固件不抓终端。
- `misc.c`：固件里不编串口终端代码。
- `vga.c` / `vga.h`：加两个只读查询（显示模式、文本格），给文本导出和状态栏用。

## 不在仓库里、也不编进固件的

- **SeaBIOS / SeaVGABIOS**（LGPL-3.0）：不提交二进制。`scripts/fetch_bios.sh` 从 tiny386 的 release 里取预编译的 `bios.bin` / `vgabios.bin`
  （版本串 `rel-1.17.0-24-g28aa4634`）；对应源码 = SeaBIOS 上游 commit 28aa4634 + tiny386 仓库 `seabios/` 下的 config 和 patch。
  自己编译的脚本还没写（见 README"还没做"）。
  生成的 bin 作为独立文件放在设备的 `EFS:/monita/x86/`，运行时读进客户机内存，不嵌进固件。分发 bin 时要附 SeaBIOS 源码出处。
- tiny386 里的 `fmopl.c`（LGPL）、`adlib*`、`ne2000.c`、`sb16.c`、`amd64*`、`kvm*`、`fpu.c` 和 sdl / wasm / android / esp / rawdraw 平台层：没有导入。
  `adlib.h`、`ne2000.h`、`sb16.h` 只为 `pc.h` 能编译而保留，对应函数由 `port/nodev.c` 给空实现。
- FreeDOS（GPL 等）：只提交下载 / 校验 / 制作脚本，不提交镜像。
- Windows 3.1：有版权，维护者自备正版，仓库里只有导入步骤（`docs/import-win31.md`）。

## 取自维护者 Monita-CS80 仓库的文件（再授权待确认）

`fw/link/proto.h`、`fw/lcd/lcd.h`、`fw/sdk/gen_stubs.py`、`fw/sdk/pack.py`、`fw/sdk/validate.py`（加了 x86 白名单）、
`fw/sdk/r05_api.h`、`fw/sdk/app.ld` 取自维护者本人的 Monita-CS80 固件仓库（该仓库许可证是 PolyForm Noncommercial）。
**以本仓库的 BSD-3-Clause 再授权这几份文件，需要维护者本人确认；确认之前别公开本仓库。**
`fw/sdk/r05_fs_ro.h` 是本仓库自己写的（只声明读类接口），接口事实来自私有工具 cs80-extflash 的实机结果。
`mac/x86kbd.py` 是本仓库自己写的 pyusb 版，没有复制 Monita 的 Swift 代码。
`r05_exports.csv`（R05 导出表的哈希清单）属于原厂固件派生数据，不放进本仓库，构建时从 Monita-CS80 仓库读（见 `fw/build.sh`）。
