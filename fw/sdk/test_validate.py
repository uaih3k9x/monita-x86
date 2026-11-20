#!/usr/bin/env python3
"""validate.py 导入守卫的单测: python3 -I fw/sdk/test_validate.py (make fw-test)"""
import os, sys, unittest
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate as v

def keys_of(path):
    out = []
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if line:
            out.append(int(line.split("=", 1)[1].strip(), 16) if "=" in line else v._key(line))
    return out

HERE = os.path.dirname(os.path.abspath(__file__))

class Guard(unittest.TestCase):
    def test_x86_imports_pass(self):
        self.assertEqual(v.import_violations(keys_of(os.path.join(HERE, "..", "app_x86.imports")), "x86"), [])
    def test_x86_no_write_or_format(self):
        # x86 固件只读外挂 flash: 写 / 建目录 / 改名 / 删除 / 格式化一个都不许, 连白名单都不能放
        for name in ("ql_fwrite", "ql_fsync", "ql_mkdir", "ql_rename", "ql_remove", "ql_spi6_ext_nor_flash_sffs_mkfs",
                     "ql_spi_nor_write", "ql_nvm_fwrite", "ql_aud_set_volume", "ql_pcm_open"):
            self.assertEqual(len(v.import_violations([v._key(name)], "x86")), 1, name)
        for name in ("ql_fopen", "ql_fread", "ql_fseek", "ql_fclose", "ql_fsize", "ql_spi6_ext_nor_flash_sffs_mount"):
            self.assertEqual(v.import_violations([v._key(name)], "x86"), [], name)
    def test_write_and_nv_denied_everywhere(self):
        for name in ("ql_fwrite", "ql_nvm_fwrite", "ql_aud_set_volume", "ql_aud_codec_write_nv", "ql_spi_nor_erase_chip"):
            for t in ("app", "x86", "diag_probe", None):
                r = v.import_violations([v._key(name)], t)
                self.assertEqual(len(r), 1, (name, t)); self.assertIn("永久禁止", r[0][2])
    def test_app_strict_allowlist(self):
        r = v.import_violations([v._key("ql_aud_play_file_start"), 0x12345678], "app")
        self.assertEqual(len(r), 2)
        self.assertEqual(v.import_violations([v._key("ql_aud_play_file_start")], "diag_probe"), [])   # 无白名单目标只查禁止名单
    def test_unnamed_hash_must_be_listed(self):
        self.assertEqual(v.import_violations([0xac3ae871], "app"), [])
    def test_audio_play_path_allowed(self):
        for name in ("ql_pcm_open", "ql_pcm_write", "ql_pcm_close", "ql_aud_data_done", "ql_set_audio_path_speaker"):
            self.assertEqual(v.import_violations([v._key(name)], "app"), [], name)

if __name__ == "__main__":
    unittest.main()
