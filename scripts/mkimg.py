#!/usr/bin/env python3
"""从 FreeDOS 官方 LiteUSB 镜像里取引导代码和文件, 做一块小的 FAT16 硬盘镜像 (给设备 / host 版用)。
被 scripts/mk_freedos_img.sh 调用; 依赖 mtools (mformat / mcopy / mmd / mtype) 和 unzip。

  mkimg.py <FD14LITE.img> <输出 hda.img> [--size-mib 12] [--extra 目录]

- MBR: 拷官方镜像的 446 字节引导代码, 分区表重写成一个活动分区 (LBA 63 起, CHS 按 16 头 / 63 扇区)
- 分区: mformat 建 FAT16 (簇 2 KiB), 再把官方分区引导扇区的跳转 + 引导代码 (0x3E..0x1FD) 拷过来, BPB 用新的
- 文件: KERNEL.SYS / COMMAND.COM + FREEDOS\\BIN 里挑的工具 + 包里的 CTMOUSE / SHARE, 写 FDCONFIG.SYS / AUTOEXEC.BAT
- --extra 目录里的文件拷到根目录 (比如 build/guest/*.COM)
"""
import os, shutil, struct, subprocess, sys, tempfile, zipfile, argparse

SPT, HEADS, START = 63, 16, 63
BIN_TOOLS = ["HIMEMX.EXE", "JEMM386.EXE", "JEMMEX.EXE", "MEM.EXE", "CPUID.EXE", "EDIT.EXE", "XCOPY.EXE",
             "ATTRIB.COM", "DELTREE.COM", "MORE.EXE", "SYS.COM", "FDAPM.COM", "SHSURDRV.EXE", "DEVLOAD.COM",
             "VCPI.EXE", "FC.EXE", "GREP.EXE"]
PKGS = {"CTMOUSE.ZIP": ["CTMOUSE.EXE"], "SHARE.ZIP": ["SHARE.COM", "SHARE.EXE"]}

FDCONFIG = r"""!LASTDRIVE=Z
!BUFFERS=20
!FILES=40
DOS=HIGH
DEVICE=\FREEDOS\BIN\HIMEMX.EXE
SHELL=\COMMAND.COM \ /E:1024 /P=\AUTOEXEC.BAT
"""
AUTOEXEC = r"""@ECHO OFF
SET DOSDIR=\FREEDOS
SET PATH=\FREEDOS\BIN;\
SET TEMP=\TEMP
ECHO monita-x86 FreeDOS ready
"""

def chs(lba):
    c = lba // (HEADS * SPT); h = (lba // SPT) % HEADS; s = lba % SPT + 1
    if c > 1023: c, h, s = 1023, HEADS - 1, SPT
    return bytes([h, ((c >> 2) & 0xC0) | s, c & 0xFF])

def mt(*args, env=None):
    subprocess.check_call(list(args), env=env, stdout=subprocess.DEVNULL)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("dst")
    ap.add_argument("--size-mib", type=int, default=12)
    ap.add_argument("--extra")
    a = ap.parse_args()
    total = a.size_mib * 1024 * 1024 // 512
    part = total - START
    src_off = "@@%d" % (START * 512)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    with open(a.src, "rb") as f:
        mbr = f.read(512)
        f.seek(START * 512); vbr_src = f.read(512)
    if mbr[510:512] != b"\x55\xaa" or vbr_src[510:512] != b"\x55\xaa" or vbr_src[0] != 0xEB:
        sys.exit("源镜像不像 FreeDOS LiteUSB (MBR / 分区引导扇区签名不对)")
    if struct.unpack_from("<I", mbr, 446 + 8)[0] != START:
        sys.exit("源镜像分区不从 LBA 63 开始")

    with open(a.dst, "wb") as f:
        f.truncate(total * 512)
    pt = bytes([0x80]) + chs(START) + bytes([0x04 if part * 512 < 32 * 1024 * 1024 else 0x06]) + chs(total - 1) \
        + struct.pack("<II", START, part)
    with open(a.dst, "r+b") as f:
        f.write(mbr[:446] + pt + bytes(48) + b"\x55\xaa")
    dst = a.dst + src_off
    mt("mformat", "-i", dst, "-T", str(part), "-h", str(HEADS), "-s", str(SPT), "-H", str(START), "-c", "4",
       "-v", "MONITAX86", "::", env=env)
    with open(a.dst, "r+b") as f:
        f.seek(START * 512); vbr = bytearray(f.read(512))
        if struct.unpack_from("<H", vbr, 0x16)[0] == 0:
            sys.exit("mformat 建出来的不是 FAT12/16")
        vbr[0:3] = vbr_src[0:3]
        vbr[0x3E:0x1FE] = vbr_src[0x3E:0x1FE]
        f.seek(START * 512); f.write(vbr)

    tmp = tempfile.mkdtemp(prefix="mkimg")
    try:
        src = a.src + src_off
        get = lambda p, out: mt("mcopy", "-n", "-o", "-i", src, "::" + p, out, env=env)
        put = lambda path, to: mt("mcopy", "-o", "-i", dst, path, "::" + to, env=env)
        for d in ("FREEDOS", "FREEDOS/BIN", "TEMP"):
            mt("mmd", "-i", dst, "::" + d, env=env)
        for n in ("KERNEL.SYS", "COMMAND.COM"):
            get(n, os.path.join(tmp, n)); put(os.path.join(tmp, n), n)
        for n in BIN_TOOLS:
            get("FREEDOS/BIN/" + n, os.path.join(tmp, n)); put(os.path.join(tmp, n), "FREEDOS/BIN/" + n)
        for z, names in PKGS.items():
            zp = os.path.join(tmp, z)
            get("PACKAGES/BASE/" + z, zp)
            with zipfile.ZipFile(zp) as zf:
                for info in zf.infolist():
                    base = os.path.basename(info.filename).upper()
                    if base in names and info.filename.upper().startswith("BIN/"):
                        out = os.path.join(tmp, base)
                        with open(out, "wb") as o: o.write(zf.read(info))
                        put(out, "FREEDOS/BIN/" + base)
        for name, text in (("FDCONFIG.SYS", FDCONFIG), ("AUTOEXEC.BAT", AUTOEXEC)):
            p = os.path.join(tmp, name)
            with open(p, "w", newline="\r\n") as o: o.write(text)
            put(p, name)
        if a.extra:
            for n in sorted(os.listdir(a.extra)):
                if n.upper().endswith((".COM", ".EXE", ".BAT")):
                    put(os.path.join(a.extra, n), n.upper())
        subprocess.check_call(["mdir", "-i", dst, "-w", "::"], env=env)
    finally:
        shutil.rmtree(tmp)

if __name__ == "__main__":
    main()
