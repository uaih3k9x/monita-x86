#!/usr/bin/env python3
"""把 BIOS / 盘镜像写进 CS80 外挂 flash 的 EFS:/monita/x86/ (monita-x86 固件从那里只读打开)。

前提: 设备上跑的是 cs80-extflash 的 extdump v2 (只许写 EFS:/monita/ 下面、写类命令要先 ARM)。
本脚本只调它的主机客户端 Client.put_file (先写 <名字>.part, fsync, 删旧, 改名), 目标只能是 EFS:/monita/x86/<名字>。

  **设备刚上电 (刷完 extdump_v2 / 重新插电) 就跑, 中间别跑别的 extflash 命令**:
  extdump v2 同一次上电第二次 INIT 会返回错误并把"已挂载"清掉, 之后所有读写都失败。本脚本只 INIT 一次。

  EXTFLASH_HOST=<cs80-extflash>/host python3 scripts/efs_put.py --port COM5 \\
      work/bios.bin work/vgabios.bin work/win31.img:hda.img
  参数: 本地文件[:设备上的名字], 名字默认 = 本地文件名; 只许 [a-z0-9._-], 不许 .part 结尾。
  --port usb 是 Mac 上 pyusb 直开接口 7 (extdump 那边注明没上机验证过); --no-verify 不整个读回 (读回约 74 KB/s)。

空间: 每个文件写 .part 时旧文件还在, 所以要 剩余 - 新文件 >= 256 KiB (同 extdump v2 固件的保留线);
写完旧文件删掉, 剩余 = 剩余 + 旧 - 新。所有文件先整体算一遍, 有一个不够就一个都不写。
纯逻辑单测: tests/test_efs_put.py。
"""
import argparse, hashlib, os, re, sys, time

RESERVE = 256 * 1024
DIR = "EFS:/monita/x86"
NAME_RE = re.compile(r"^[a-z0-9._-]{1,48}$")


class SpaceError(Exception):
    pass


try:
    from extflash_v2 import DevError                   # 有 EXTFLASH_HOST 时用它的
except ImportError:
    class DevError(Exception):
        pass


def parse_spec(spec):
    """'本地[:名字]' -> (本地路径, EFS:/monita/x86/名字); 名字不合规 ValueError"""
    local, _, name = spec.rpartition(":")
    if not local or "/" in name or "\\" in name:        # 没给名字 (含 Windows 盘符 C:\...): 用本地文件名
        local, name = spec, re.split(r"[\\/]", spec)[-1]
    if not NAME_RE.match(name) or name.strip(".") == "" or name.endswith(".part"):
        raise ValueError("设备上的名字不合规: %r (只许 [a-z0-9._-], 不许 .part 结尾 / 全是点)" % name)
    return local, DIR + "/" + name


def check_space(free, new_len):
    """写 .part 期间 (旧文件还在) 的剩余; 低于保留线 SpaceError"""
    during = free - new_len
    if during < RESERVE:
        raise SpaceError("空间不够: 剩 %d B, 写 %d B 的 .part 时只剩 %d B < 保留线 %d B (旧文件这时还没删)"
                         % (free, new_len, during, RESERVE))
    return during


def upload(c, files, verify=True, out=print):
    """files: [(本地名, 数据, 设备路径)]。只 INIT 一次; 先整体算空间再写。"""
    out("HELLO:", c.hello())
    try:
        out("INIT:", c.init())
    except DevError as e:
        raise DevError("%s\n这多半是同一次上电已经 INIT 过 (extdump v2 第二次 INIT 会清掉挂载): "
                       "拔电重新上电后马上再跑本脚本, 中间别跑别的 extflash 命令" % e)
    free = c.info()["free"]
    plan = []
    for local, data, path in files:
        old = c.stat(path) or 0
        during = check_space(free, len(data))
        free = free + old - len(data)
        plan.append((local, data, path, old, during))
    for local, data, path, old, during in plan:
        out("计划 %s -> %s: %d B (旧 %d B), 写的时候剩 %.2f MiB" % (local, path, len(data), old, during / 1048576))
    c.arm()
    c.mkdir("EFS:/monita")                               # 已存在也无妨
    c.mkdir(DIR)
    for local, data, path, old, during in plan:
        t0 = time.time()
        fs = c.put_file(path, data)
        dt = max(time.time() - t0, 1e-6)
        out("写完 %s: %.1f s (%.1f KiB/s), fsync=%d" % (path, dt, len(data) / 1024 / dt, fs))
        if verify:
            back = c.get_file(path)
            if back is None or hashlib.sha256(back).digest() != hashlib.sha256(data).digest():
                raise DevError("读回 %s 和本地不一致" % path)
            out("读回 %s: sha256 一致 %s" % (path, hashlib.sha256(data).hexdigest()))
    out("完成。接着刷 build/fw/app_x86.bin 到 0x60270000。")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", help="本地文件[:设备上的名字]")
    ap.add_argument("--port", required=True, help="COMx (ThinkPad) 或 usb (Mac)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--no-verify", action="store_true")
    a = ap.parse_args()
    files = []
    for spec in a.files:
        local, path = parse_spec(spec)
        files.append((local, open(local, "rb").read(), path))
    host = os.environ.get("EXTFLASH_HOST", "")
    if not os.path.isfile(os.path.join(host, "extflash_v2.py")):
        sys.exit("要设 EXTFLASH_HOST=<cs80-extflash>/host")
    sys.path.insert(0, host)
    global DevError
    import extlink as L                                  # noqa: E402
    from extflash_v2 import Client, DevError             # noqa: E402
    c = Client(L.Link(L.open_transport(a.port, a.baud), a.timeout))
    try:
        upload(c, files, verify=not a.no_verify)
    except (SpaceError, DevError) as e:
        sys.exit("停手: %s" % e)


if __name__ == "__main__":
    main()
