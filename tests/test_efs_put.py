#!/usr/bin/env python3
"""scripts/efs_put.py 的单测: 假的 extdump v2 客户端, 不碰设备。python3 -I tests/test_efs_put.py"""
import hashlib, os, sys, unittest
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))
import efs_put as E

MiB = 1 << 20

class FakeClient:
    def __init__(self, free, files=None, init_fail=False, corrupt=False):
        self.free, self.files, self.log = free, dict(files or {}), []
        self.init_fail, self.corrupt, self.armed = init_fail, corrupt, False
    def hello(self): self.log.append("hello"); return {"fw": 2}
    def init(self):
        self.log.append("init")
        if self.init_fail: raise E.DevError("挂载失败 init=0 mount=-2083323901")
        return {"init_ret": 0, "mount_ret": 0}
    def info(self): self.log.append("info"); return {"free": self.free, "mounted": 1}
    def stat(self, p): return len(self.files[p]) if p in self.files else None
    def arm(self): self.log.append("arm"); self.armed = True
    def mkdir(self, p): self.log.append("mkdir " + p); return (0, 0, 0)
    def put_file(self, p, data):
        assert self.armed
        assert self.free - len(data) >= E.RESERVE, "固件会拒: .part 写着时旧文件还在"
        self.log.append("put " + p)
        self.free += len(self.files.get(p, b"")) - len(data)
        self.files[p] = data if not self.corrupt else data[:-1] + b"\0"
        return 0
    def get_file(self, p): self.log.append("get " + p); return self.files.get(p)

class Names(unittest.TestCase):
    def test_parse(self):
        self.assertEqual(E.parse_spec("work/bios.bin"), ("work/bios.bin", "EFS:/monita/x86/bios.bin"))
        self.assertEqual(E.parse_spec("work/win31.img:hda.img"), ("work/win31.img", "EFS:/monita/x86/hda.img"))
    def test_windows_paths(self):
        self.assertEqual(E.parse_spec(r"C:\x\bios.bin"), (r"C:\x\bios.bin", "EFS:/monita/x86/bios.bin"))
        self.assertEqual(E.parse_spec(r"C:\x\win31.img:hda.img"), (r"C:\x\win31.img", "EFS:/monita/x86/hda.img"))
        self.assertEqual(E.parse_spec(r"work\vgabios.bin"), (r"work\vgabios.bin", "EFS:/monita/x86/vgabios.bin"))
    def test_only_x86_dir(self):
        # 冒号后面带路径分隔符 = 整个是本地路径, 设备上用基名: 无论如何都落在 EFS:/monita/x86/ 下
        self.assertEqual(E.parse_spec("a:../x"), ("a:../x", "EFS:/monita/x86/x"))
        self.assertEqual(E.parse_spec("a:sub/x.img"), ("a:sub/x.img", "EFS:/monita/x86/x.img"))
        for bad in ("a:HDA.IMG", "a:x.part", "a:", "a:..", "a:a b", "../..", "x/.."):
            with self.assertRaises(ValueError, msg=bad): E.parse_spec(bad)

class Plan(unittest.TestCase):
    def test_old_and_new_coexist(self):
        # 剩 7 MiB, 旧 hda 6 MiB 在: 新 6 MiB 写 .part 时两份都在 -> 7 - 6 = 1 MiB >= 256 KiB, 行
        self.assertEqual(E.check_space(7 * MiB, 6 * MiB), 7 * MiB - 6 * MiB)
        # 剩 6.1 MiB: 写 .part 时只剩 0.1 MiB < 保留线, 哪怕写完会腾回旧文件的空间也不行
        with self.assertRaises(E.SpaceError): E.check_space(6 * MiB + 100 * 1024, 6 * MiB)

class Flow(unittest.TestCase):
    def files(self):
        return [("bios", b"B" * 131072, "EFS:/monita/x86/bios.bin"), ("hda", b"H" * (6 * MiB), "EFS:/monita/x86/hda.img")]
    def test_happy(self):
        c = FakeClient(free=14 * MiB)
        E.upload(c, self.files(), verify=True, out=lambda *a: None)
        self.assertEqual(c.log.count("init"), 1)                       # 同次上电只 INIT 一次
        self.assertEqual(c.log[:2], ["hello", "init"])
        self.assertIn("mkdir EFS:/monita/x86", c.log)
        self.assertLess(c.log.index("arm"), c.log.index("put EFS:/monita/x86/bios.bin"))
        self.assertIn("get EFS:/monita/x86/hda.img", c.log)
    def test_space_checked_per_file_before_any_write(self):
        c = FakeClient(free=6 * MiB)                                   # bios 放得下, hda 放不下
        with self.assertRaises(E.SpaceError): E.upload(c, self.files(), verify=False, out=lambda *a: None)
        self.assertFalse(any(x.startswith("put") for x in c.log))      # 先整体算, 一个都不写
    def test_init_fail_tells_power_cycle(self):
        c = FakeClient(free=14 * MiB, init_fail=True)
        with self.assertRaises(E.DevError) as cm: E.upload(c, self.files(), verify=False, out=lambda *a: None)
        self.assertIn("重新上电", str(cm.exception))
        self.assertEqual(c.log.count("init"), 1)
    def test_verify_mismatch(self):
        c = FakeClient(free=14 * MiB, corrupt=True)
        with self.assertRaises(E.DevError): E.upload(c, self.files(), verify=True, out=lambda *a: None)

if __name__ == "__main__":
    unittest.main()
