# 导入 Windows 3.1 盘

Windows 3.1 / 3.11 有版权，仓库里不带任何微软文件，盘镜像要维护者用自己的正版安装盘做。
`*.img` 已在 `.gitignore` 里，别手动 `git add -f`。

## 盘的要求

- raw 硬盘镜像，带 MBR，一个 FAT16 主分区，CHS 几何 x/16/63（tiny386 的 IDE 按 16 头 63 扇区报几何）
- FreeDOS 内核用 KERNL86（286 版）或默认 386 版都行；DOS 要用 Windows 自带的 `HIMEM.SYS`（3.07），不用 HIMEMX
- **不要加 `DOS=HIGH`**：热复位（`ctrl 1` / 设备上的复位键，内存不清）后 HIMEM 会报
  `VDISK memory allocator already installed`，XMS 装不上，Windows 起不来
- 标准模式（`WIN /S`），删掉 KRNL386.EXE 后 386 上也走 KRNL286，2 MB 客户机内存够用
- Windows 里的 MS-DOS Prompt 已知会坏（黑屏或 FreeDOS 报 Invalid Opcode），别点

一份能用的做法（维护者私有工具目录里的 `cs80-x86-disk`，不在本仓库）：FreeDOS 1.3 LiteUSB 引导 +
无人值守 `SETUP /H` 装进临时盘，再按清单精简成约 6 MB 的盘（CHS 12/16/63）。

## 放进来

```
cp <你的 win31.img> work/win31.img          # work/ 不进仓库
make host
build/x86host -bios work/bios.bin -vgabios work/vgabios.bin -hda work/win31.img -mem 2M \
  -script tests/host/boot_win31.txt          # 截图在 build/shots/w31-*.png
```

客户机写盘只进内存覆盖层，`work/win31.img` 不会被改。

## 上设备

盘放外挂 flash 的 `EFS:/monita/x86/hda.img`（只读打开），见 README 的"上真机"一节。
