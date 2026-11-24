#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Static check of a packed 2PPA app against the R05 loader rules. Does NOT touch hardware.

Rules below are read from the R05 AP loader (see docs/r05-notes.md §3):
  magic 2PPA; 0x80 < total_len; base+total_len <= 0x60370000
  version: hi16 == kernel hi16 (原厂 app = 0x0001), lo16 <= kernel lo16 (原厂 = 0)
  enter != 0; enter/exit/@0x18/@0x1c (if !=0) in [base, base+total) or [0x80fe0000, 0x81000000)
  crc32@8 = zlib.crc32(whole file with @8 = 0)
  6 descriptor slots @0x20..0x80, kind 0 or size 0 = skipped; kind 1..4 only
    kind1 copy / kind2 core_stub / kind3 zero: addr > 0x80fdffff, addr+size <= 0x81000000
    kind1/2/4: fileoff+size <= total_len
    kind2: 8-byte stubs, every hash must be in the AP export table (else load fails)
Extra (ours): enter/exit inside the kind4 code region, no VFP/NEON instructions (needs ELF).
Import guard (ours): every stub hash is checked against ALWAYS_DENY (format / erase / raw write / NV write,
  incl. audio calls that write the audio NV file every time); a target listed in TARGET_IMPORT_ALLOW must
  import nothing outside its list (unnamed "name = 0xHASH" imports must be listed by hash).
  usage: validate.py app.bin [app.elf] [--target app]
Optional: if $R05_ORIG_APP points at a dump of the stock app container (not shipped in
  this repo), every stub is also compared byte-for-byte with the stock stub for the same import.
"""
import struct, zlib, sys, csv, os, subprocess, hashlib

BASE = 0x60270000
HERE = os.path.dirname(os.path.abspath(__file__))
# R05 导出表哈希清单: 原厂固件派生数据, 不进本仓库; fw/build.sh 从 Monita-CS80 仓库指过来
CSV  = os.environ.get("R05_EXPORTS", os.path.join(HERE, "r05_exports.csv"))
ORIG = os.environ.get("R05_ORIG_APP")      # 可选: 原厂 app 容器 dump, 仓库里不带
ORIG_APP_SIZE = 0xa09a0      # 原厂 app 大小; 之后 0x60320000 起是资源区

# ---- 导入守卫 ----
# 永久禁止 (任何目标): 格式化 / 擦除 / 裸写 / 改 key / 截断 / 删除 / 文件写, 以及会写内部 flash NV 的音频接口。
#   ql_aud_set_volume: 内核每调一次都把音频配置整个写回 NV 文件 (相同值也写), 见 docs/audio.md; 音量在固件里软件缩放。
ALWAYS_DENY_NAMES = [
    "ql_spi6_ext_nor_flash_sffs_mkfs", "ql_spi6_ext_nor_flash_deinit", "ql_spi6_ext_nor_flash_sffs_unmount",
    "ql_nvm_fwrite", "ql_sfs_setkey", "ql_ftruncate", "ql_rmdir", "ql_rmdir_ex", "ql_unlink",
    "ql_fs_erase", "ql_flash_erase", "ql_spi6_ext_nor_flash_erase", "ql_fs_format", "ql_fs_mkfs",
    "ql_spi_nor_init", "ql_spi_nor_init_ext", "ql_spi_nor_write", "ql_spi_nor_erase_sector",
    "ql_spi_nor_erase_chip", "ql_spi_nor_write_8bit_status", "ql_spi_nor_write_16bit_status",
    "ql_spi_nand_init", "ql_spi_nand_erase_block", "ql_spi_nand_reset",
    "ql_fwrite", "ql_fsync", "ql_mkdir", "ql_rename", "ql_remove",
    "ql_aud_set_volume", "ql_aud_set_icvolume_level_gain", "ql_aud_codec_write_nv", "ql_aud_set_calib_real_time",
    "ql_aud_set_pa_type", "ql_aud_set_icmic_gain", "ql_aud_set_icsidet_gain",
]
# 逐个点名的导入白名单 (独立于 .imports 再写一遍): 名单外的任何导入一律 FAIL, 没解名的导出按哈希点名。
TARGET_IMPORT_ALLOW = {
    # monita-x86 固件 (fw/app_x86.c): 大屏 + USB 链路 + 外挂 flash 只读 (盘镜像 / BIOS)。没有任何写文件 / 写 flash 接口。
    "x86": {
        "ql_pin_set_func", "ql_gpio_init", "ql_gpio_deinit", "ql_gpio_set_level", "ql_gpio_get_level",
        "ql_rtos_task_create", "ql_rtos_task_sleep_ms",
        "ql_rtos_semaphore_create", "ql_rtos_semaphore_wait", "ql_rtos_semaphore_release",
        "ql_dev_cfg_wdt", "ql_dev_feed_wdt", "osiUpTimeUS",
        "ql_uart_open", "ql_uart_read", "ql_uart_write", "ql_uart_register_cb",
        "ql_mipi_lcd_init", "ql_mipi_lcd_write_screen", 0xac3ae871,     # r05_lcd_pm_lock (名字未解出)
        "malloc", "free", "ql_dev_memory_size_query",
        "ql_spi6_ext_nor_flash_init", "ql_spi6_ext_nor_flash_sffs_mount",   # mount 内部传 0: 挂不上也不格式化
        "ql_fopen", "ql_fread", "ql_fseek", "ql_fclose", "ql_fsize",
    },
    "app": {
        "ql_pin_set_func", "ql_gpio_init", "ql_gpio_deinit", "ql_gpio_set_level", "ql_gpio_set_direction", "ql_gpio_get_level",
        "ql_delay_us", "ql_rtos_task_create", "ql_rtos_task_sleep_ms",
        "ql_rtos_semaphore_create", "ql_rtos_semaphore_wait", "ql_rtos_semaphore_release",
        "ql_dev_cfg_wdt", "ql_dev_feed_wdt", "osiUpTimeUS",
        "ql_uart_open", "ql_uart_read", "ql_uart_write", "ql_uart_register_cb",
        "ql_mipi_lcd_init", "ql_mipi_lcd_write_screen", 0xac3ae871,     # r05_lcd_pm_lock (名字未解出)
        "malloc", "free", "ql_dev_memory_size_query",
        "ql_pwrkey_shutdown_time_set", "ql_pwrkey_callback_register", "ql_pwrkey_press_cb_register",
        "ql_pwrkey_release_cb_register", "ql_usb_bind_hotplug_cb",
        # 喇叭 (fw v13, docs/audio.md): 只放播放路径; 调用序列和参数同原厂 app
        "ql_set_audio_path_speaker", "ql_pcm_open", "ql_pcm_write", "ql_pcm_close", "ql_aud_data_done",
    },
}

def _key(name):   # LE u32 key as stored in r05_exports.csv (= gen_stubs.py h_be)
    return int.from_bytes(hashlib.sha256(name.encode()).digest()[:4], "big")
ALWAYS_DENY = {_key(n): n for n in ALWAYS_DENY_NAMES}

def import_violations(keys, target=None):
    """keys: 导入 key 列表; target: 目标名 (不在 TARGET_IMPORT_ALLOW 里 = 只查禁止名单)。返回 [(key, name, why)]"""
    allow = TARGET_IMPORT_ALLOW.get(target or "")
    strict = None if allow is None else {k if isinstance(k, int) else _key(k) for k in allow}
    out = []
    for h in keys:
        if h in ALWAYS_DENY:
            out.append((h, ALWAYS_DENY[h], "永久禁止 (擦除 / 写 flash / 写 NV 类)"))
        elif strict is not None and h not in strict:
            out.append((h, "?", "不在目标 %s 的导入白名单里" % target))
    return out

def main():
    argv = sys.argv[1:]; target = None
    if "--target" in argv:
        i = argv.index("--target"); target = argv[i + 1]; del argv[i:i + 2]
    path = argv[0]; elf = argv[1] if len(argv) > 1 else None
    d = open(path, "rb").read()
    ok = True
    def chk(c, msg):
        nonlocal ok
        print(("  OK  " if c else " FAIL ") + msg); ok = ok and bool(c)
    magic, total, crc, ver, enter, exit_, x18, x1c = struct.unpack_from("<8I", d, 0)
    chk(magic == 0x41505032, "magic 2PPA")
    chk(total == len(d) and total > 0x80, "total_len %d == file %d (>0x80)" % (total, len(d)))
    chk(BASE + total <= 0x60370000, "base+total <= 0x60370000 (loader)")
    chk(total <= ORIG_APP_SIZE, "total <= 原厂 app 大小 %#x (不压资源区)" % ORIG_APP_SIZE)
    z = bytearray(d); z[8:12] = b"\0\0\0\0"
    chk((zlib.crc32(bytes(z)) & 0xffffffff) == crc, "crc32@8 = %08x" % crc)
    chk(ver >> 16 == 1 and ver & 0xffff == 0, "version %#010x (= 原厂)" % ver)

    exp = {int(r["hash"], 16) for r in csv.DictReader(open(CSV))}
    orig_stub = {}
    if ORIG and os.path.exists(ORIG):
        o_ = open(ORIG, "rb").read()
        for i in range(0x638 // 8):
            b = o_[0x80 + 8*i: 0x88 + 8*i]
            orig_stub[struct.unpack_from("<I", b, 4)[0]] = b

    in_ram = lambda a, n=0: a > 0x80fdffff and a + n <= 0x81000000
    code = []; stubs = []
    for slot in range(6):
        k, fo, sz, ad = struct.unpack_from("<4I", d, 0x20 + 16*slot)
        if k == 0 or sz == 0: continue
        chk(1 <= k <= 4, "slot%d kind %d valid" % (slot, k))
        if k in (1, 2, 3):
            chk(in_ram(ad, sz), "slot%d kind%d RAM [%#x,+%#x) in app window" % (slot, k, ad, sz))
        if k in (1, 2, 4):
            chk(fo + sz <= total, "slot%d kind%d file range in-bounds" % (slot, k))
        if k == 4:
            chk(fo == ad - BASE, "slot%d kind4 XIP fileoff==addr-base" % slot); code.append((ad, sz))
        if k == 2:
            chk(sz % 8 == 0, "slot%d kind2 size multiple of 8" % slot); stubs.append((fo, sz))
    in_code = lambda a: any(s <= (a & ~1) < s + n for s, n in code)
    chk(enter and enter & 1 and in_code(enter), "enter %#x Thumb, in kind4 code" % enter)
    chk(exit_ & 1 and in_code(exit_), "exit  %#x Thumb, in kind4 code" % exit_)
    chk(x18 == 0 and x1c == 0, "@0x18/@0x1c = 0")

    n = bad = same = 0; keys = []
    for fo, sz in stubs:
        for p in range(fo, fo + sz, 8):
            b = d[p:p+8]; h = struct.unpack_from("<I", b, 4)[0]; n += 1; keys.append(h)
            if b[:4] != bytes.fromhex("dff800f0") or h not in exp:
                bad += 1; print("  FAIL  stub @%#x %s not resolvable" % (p, b.hex()))
            elif h in orig_stub:
                if orig_stub[h] == b: same += 1
                else: bad += 1; print("  FAIL  stub @%#x differs from 原厂" % p)
    chk(bad == 0, "%d stubs resolve in R05 exports" % n
        + ("; %d byte-identical to 原厂 app stubs" % same if orig_stub else " (未提供 R05_ORIG_APP, 跳过逐字节比对)"))

    viol = import_violations(keys, target)
    for h, nm, why in viol: print("  FAIL  import %08x %s: %s" % (h, nm, why))
    chk(not viol, "import guard: %d imports, none denied" % len(keys)
        + (", all in %s allowlist" % target if target in TARGET_IMPORT_ALLOW else " (目标 %s 无白名单, 只查禁止名单)" % target))

    if elf:
        dis = subprocess.check_output(["arm-none-eabi-objdump", "-d", "-j", ".text", elf]).decode()
        vfp = [l for l in dis.splitlines() if "\t" in l and l.split("\t")[-1].startswith("v")]
        chk(not vfp, "no VFP/NEON instructions in .text" + ("" if not vfp else ": " + vfp[0]))
    print("\nRESULT:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
