# monita-x86

拉卡拉 CS80 客显屏（Monita 自制固件平台：R05 基带，Cortex-A5 500 MHz，内核堆约 6.6 MB）上的 x86 PC 模拟器，
基于 [tiny386](https://github.com/hchunhui/tiny386)。目标：开 FreeDOS，开 Windows 3.1 标准模式。

```
core/      机器胶水 (machine.c)、只读盘 + 内存写覆盖层、读缓存、内存分档、脏区、平台接口 plat.h
vendor/    tiny386 子集 (改动都包在 MONITA_X86 / MONITA_FW 宏里, 见 THIRD_PARTY.md)
host/      Mac 主机版 x86host: 不开窗口, 脚本注入键盘鼠标, 导出 PNG / 文本屏
qemu/      QEMU 估速 (run.sh)、fwsim (fwsim.sh: 真固件代码 + 假 R05 导入)、采样剖析 (profile.py)
fw/        固件 app_x86 (R05 app 槽 2PPA) 和打包 / 校验脚本
mac/       x86kbd.py: Mac 终端 -> USB 的键盘鼠标转发
port/      最小 libc、整数除法、没编进来的外设空实现
scripts/   下载 BIOS / FreeDOS、做盘、交叉编译参数
tests/     主机单测 (C + python)、客户机自测程序、主机版脚本
```

## 现在的状态（2026-11-20）

| 项目 | 结果 |
|---|---|
| 主机版 FreeDOS | 过：SeaBIOS → FreeDOS → HIMEMX/XMS → PMTEST（286 保护模式自测）→ VGA 12h/13h，2 MB 和 1.5 MB 都过；硬复位、硬复位 + 清覆盖层、Ctrl+Alt+Del 后都回到提示符，XMS 还在 |
| 主机版 Win3.1 | 过：2 MB 和 1.5 MB 都进 Program Manager；键盘开扫雷、PS/2 鼠标能动；硬复位和 Ctrl+Alt+Del 后都重新进 Windows |
| 脏区 | 两个脚本里多处 `expect-clean`：影子缓冲只收 clean 回调报过的字节，和帧缓冲逐字节相同（故意少报一行文字时能抓到） |
| QEMU（ARM，和固件同一批 .o） | FreeDOS 到提示符、Win3.1 到 Program Manager 都过 |
| fwsim（真固件代码 + 假 R05 导入） | FreeDOS 收到键盘帧敲 `ver`、Win3.1 进 Program Manager 收到鼠标帧，都过；缺 `bios.bin` / `hda.img` 时屏上打原因、停在应答循环里，STAT 带回原因 |
| 固件构建 | `fw/build.sh`：181,568 B，29 个导入全在 x86 白名单里，没有 VFP/NEON，静态校验 PASS |
| 真机 | **没上过**。速度、XIP 代码执行、外挂 flash 读、看门狗、基带共存都要上机才知道 |

单测：`make test`（13 个 C 单测 + 固件导入守卫 + Mac 转发工具）。

## 速度（QEMU 估算，不是实测）

QEMU 没有 Cortex-A5，用 virt + cortex-a7 跑和固件同一批目标文件，`-icount shift=N` 让每条 ARM 指令走 2^N ns 虚拟时间。
所以下面的"秒"是**假设 A5 的有效速度**（500 MHz × IPC）时设备上的时间；IPC 是假设，没测过。
QEMU 看不到的：代码在内部 NOR 上 XIP 执行的取指延迟、cache 缺失、让给基带的时间，**真机可能比表里慢好几倍**。

| 假设的有效速度 | FreeDOS 到 `C:\>` | Win3.1 进图形模式 | Program Manager 画完 | Windows 里客户机速度 |
|---|---|---|---|---|
| 500 MIPS（IPC 1.0，上限） | 6.2 s | 6.3 s | — | — |
| 250 MIPS（IPC 0.5） | 8.0 s | 8.8 s | 12–15 s | 1.39 MIPS（fwsim 带让出 CPU：1.2–1.3） |
| 125 MIPS（IPC 0.25） | 11.4 s | 13.2 s | 23 s 以后（截图停在没画完的画面） | 0.65 MIPS |

- 表里是 `qemu/run.sh`（不让出 CPU）。固件每跑 15 ms 睡 2 ms，fwsim 里 Windows 是 1.2–1.3 MIPS。
- 含 SeaBIOS "Press ESC for boot menu" 固定等的约 2.5 s。
- 解释器折合每条 x86 指令约 140–200 条 ARM 指令（含刷屏）。剖析（`qemu/profile.py`，按 QEMU 墙钟采样，有偏）：CPU 核约 70%，刷屏约 4%。
- 真机标定：Monita 文档里设备上的 CPU 耗时只有"整屏 clean ≈ 1.3 ms"这类访存为主的数，标不了解释器的 IPC。
  IPC 是未知数；解释器热代码 100 KB 以上，在内部 NOR 上 XIP 执行，客户机内存在 PSRAM，这是头号不确定项。
- 主机版在这台 Mac 上约 140 MIPS，只用来调功能，不代表设备。
- 复现：`bash qemu/run.sh freedos 2`、`bash qemu/run.sh win31 2 15`（日志和截图在 `build/qemu/`）。

## 内存预算（设备，第 0 档）

内核堆 `[0x80791280, 0x80de0000)`；Monita 实机测到 avail 4.36 MB、最大块 4.35 MB。原厂 fb `0x80de0000` 起 2 MB 不在堆里，直接用作单缓冲。

| 用途 | 大小 | 说明 |
|---|---|---|
| 客户机内存 | 2048 KiB | 第 1 档 1536 KiB（Win3.1 也能进） |
| 显存 | 256 KiB | |
| 写覆盖层 | 256 KiB + 4 KiB 表 | 第 1 档 128 KiB；满了拒写（状态栏 FULL） |
| 读缓存 | 64 KiB | 第 1 档 32 KiB |
| tiny386 小块 | 预留 288 KiB | 实测：开机 40 KiB，Windows 下峰值 190 KiB（VGA 平面模式缓存 150 KiB），IDE 写聚合最多 64 KiB |
| 任务栈 | 16 KiB | QEMU 测到高水位不到 2 KiB |
| app 窗口 .data + .bss | 约 5.5 KiB | 128 KiB 窗口里 |

- 分配时要求拿完四大块后堆里还能再拿 `288 KiB + 256 KiB`（最差 `288 + 128 KiB`），不够就降一档，两档都不行就不启动客户机，只应答链路。
- 每档总占用（不含余量）不超过 4100 KiB，等于 Monita 三缓冲在机上稳定跑过的量（单测 `test_memplan` 查）。
- fwsim 按 4.35 MB 的堆跑：第 0 档，分完四大块后最大块还剩 1722 KiB。

## 主机版

```
brew install mtools                   # 做 FreeDOS 盘要
bash scripts/fetch_bios.sh            # work/bios.bin、work/vgabios.bin (SeaBIOS, LGPL)
bash scripts/mk_freedos_img.sh        # work/hda.img: 下载 FreeDOS 1.4 LiteUSB (校验 sha256), 做 12 MB 盘
make test host
build/x86host -bios work/bios.bin -vgabios work/vgabios.bin -hda work/hda.img -script tests/host/boot_freedos.txt
```

Win3.1 的盘有版权，要自己做，见 `docs/import-win31.md`；放到 `work/win31.img` 后跑 `tests/host/boot_win31.txt`。
主机版只读打开盘镜像，客户机写只进内存。脚本命令见 `host/main.c` 文件头。

## 固件

- 单任务：开大屏 → 挂外挂 flash → 预检三个文件（能开、大小像样）→ 分内存 → 读 BIOS → 循环（收链路帧、跑客户机 15 ms、睡 2 ms、每秒喂狗）。客户机 HLT 时也睡 1 ms。
- 盘和 BIOS 只从 `EFS:/monita/x86/{bios.bin, vgabios.bin, hda.img}` 用 `"rb"` 打开，一次最多读 4 KiB（extdump 实机验证过的长度）；客户机写盘只进内存覆盖层，复位（带清空）或断电就没了。
- 导入里没有任何写文件 / 写 flash / 格式化接口（`validate.py --target x86` 和单测查）。
- 起不来（挂不上 EFS、内存不够、BIOS / 盘打不开、tiny386 abort）：屏上打原因，停在应答循环里，`--stat` 能看到日志。
- 链路帧见 `docs/protocol-x86.md`。

构建：`bash fw/build.sh`（要 Monita-CS80 仓库里的 `fw/sdk/r05_exports.csv`，默认路径 `~/code/Monita-CS80-lakala/`，可用 `R05_EXPORTS=` 指定）。
上机前自查：`bash qemu/fwsim.sh freedos`、`bash qemu/fwsim.sh win31`。

## 上真机（维护者亲手做）

设备上只放一块盘：`win31.img` 改名成 `hda.img`（建空文件 `C:\NOWIN` 就停在 DOS 提示符）。EFS 空闲 14.02 MiB，放得下这块盘 + 两个 BIOS（约 6.1 MB），放不下再加 12 MB 的 FreeDOS 盘。

1. 构建、自查：
   ```
   cd ~/code/monita-x86 && make test && bash qemu/fwsim.sh win31 && bash fw/build.sh
   ```
2. 把三个文件写进 `EFS:/monita/x86/`：`work/bios.bin`、`work/vgabios.bin`、`work/win31.img`（改名 `hda.img`）。
   **这一步的工具还没有**：要先刷 cs80-extflash 的 `extdump_v2.bin`，再用它的守卫写路径（`*.part` → rename，只在 `EFS:/monita/` 下）
   上传；`extflash_v2.py` 现在没有 `put` 子命令，要补（见"还没做"）。
3. 刷固件到 app 槽 `0x60270000`（维护者的刷写机上，用 win-flash 里自带的 python）：
   ```
   py\python.exe dump_flash.py --restore app_x86.bin --addr 0x60270000
   ```
4. **先**在 Mac 上停掉 Monita 上位机（它占着接口 7，而且一连上就会朝设备推整帧），**再**把设备插到 Mac，然后开转发：
   ```
   launchctl bootout gui/$(id -u) ~/Library/LaunchAgents/com.monita.cs80.plist
   python3 mac/x86kbd.py --stat          # 先看一眼状态
   python3 mac/x86kbd.py                 # 打字 / 鼠标; Ctrl+] q 退出
   launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.monita.cs80.plist   # 用完恢复
   ```
5. 回到日常固件：照常把已知良好的 Monita app 刷回 `0x60270000`。

上机要看的：屏上开机几行（EFS 挂载返回值、三个文件的大小、分到哪一档、堆剩多少）；状态栏的 MIPS / fps；基带和 USB 是否稳（占空比 `YIELD_MS` 在 `fw/app_x86.c`）；看门狗会不会咬（fwsim 里最长 1.03 s 喂一次）。

## 还没做

- 上真机：上面所有数字都是主机 / QEMU 上的。
- EFS 上传工具：给 cs80-extflash 的 `extflash_v2.py` 加 `put`（mkdir `EFS:/monita/x86` → 写 `*.part` → rename），或者单独的小工具。
- `mac/x86kbd.py` 的 pyusb 打开接口 7 没上过真机（Monita 的 Swift 上位机在同一个接口上是通的）。
- SeaBIOS 自己编译的脚本（现在用 tiny386 release 的预编译版）；可以顺便把开机菜单等待的 2.5 s 去掉。
- 速度：CPU 核占 70%，要快只能在解释器上下功夫（tiny386 上游的 fast 路径没导入）。
- Windows 里的 MS-DOS Prompt 已知会坏；声音没有（PC 喇叭没接）。
- 硬复位（PC_CTRL 1）不清客户机内存（tiny386 的复位只重载 BIOS、复位 CPU）。现有两块盘测过都没问题；盘里加了 `DOS=HIGH` + Windows 的 HIMEM 时可能出 VDISK 那个问题，到时候在 `mx_ctrl` 里复位前清内存。
- fwsim 不是 R05 内核的模拟：时序、USB、外挂 flash 都是假的，只用来跑通固件胶水。
