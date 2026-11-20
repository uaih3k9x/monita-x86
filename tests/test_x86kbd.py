#!/usr/bin/env python3
"""mac/x86kbd.py 的纯逻辑单测 (不碰 USB / 终端): python3 -I tests/test_x86kbd.py"""
import os, struct, sys, unittest, zlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mac"))
import x86kbd as k

D, E0 = 0x01, 0x02
def press(code, e0=False):    # 按下 + 松开
    f = E0 if e0 else 0
    return [(D | f, code), (f, code)]

class Frame(unittest.TestCase):
    def test_encode(self):
        b = k.frame(0x01, 7, b"\x01\x02")
        self.assertEqual(b[:6], bytes([0xA5, 0x5A, 0x01, 7, 2, 0]))
        self.assertEqual(struct.unpack("<I", b[-4:])[0], zlib.crc32(b[2:-4]) & 0xffffffff)
    def test_parse_back(self):
        p = k.Parser()
        out = p.feed(b"\x00\xff" + k.frame(0x81, 1, bytes(16)) + k.frame(0xE3, 2, b"x" * 88)[:10])
        self.assertEqual([(t, len(pl)) for t, s, pl in out], [(0x81, 16)])
        self.assertEqual(p.feed(k.frame(0xE3, 2, b"x" * 88)[10:]), [(0xE3, 2, b"x" * 88)])
    def test_bad_crc_dropped(self):
        b = bytearray(k.frame(0x81, 1, bytes(16))); b[-1] ^= 1
        self.assertEqual(k.Parser().feed(bytes(b)), [])
    def test_kbd_payload(self):
        self.assertEqual(k.kbd_payload([(D, 0x1E), (0, 0x1E)]), bytes([2, 1, 0x1E, 0, 0x1E]))
    def test_mouse_payload(self):
        self.assertEqual(k.mouse_payload(-3, 70000, 1, 5), struct.pack("<hhbB", -3, 32767, 1, 5))

class Keys(unittest.TestCase):
    def setUp(self):
        self.t = k.Term()
    def keys(self, data):
        ev = self.t.feed(data) + self.t.flush()
        return [e for kind, e in ev if kind == "key" for e in e]
    def test_ascii(self):
        self.assertEqual(self.keys(b"a"), press(0x1E))
        self.assertEqual(self.keys(b"A"), [(D, 0x2A)] + press(0x1E) + [(0, 0x2A)])
        self.assertEqual(self.keys(b"1!"), press(0x02) + [(D, 0x2A)] + press(0x02) + [(0, 0x2A)])
        self.assertEqual(self.keys(b" \r\t\x7f"), press(0x39) + press(0x1C) + press(0x0F) + press(0x0E))
    def test_ctrl_letter(self):
        self.assertEqual(self.keys(b"\x03"), [(D, 0x1D)] + press(0x2E) + [(0, 0x1D)])     # Ctrl+C
    def test_arrows_and_fkeys(self):
        self.assertEqual(self.keys(b"\x1b[A\x1b[D"), press(0x48, True) + press(0x4B, True))
        self.assertEqual(self.keys(b"\x1bOP\x1b[15~\x1b[24~"), press(0x3B) + press(0x3F) + press(0x58))
        self.assertEqual(self.keys(b"\x1b[3~\x1b[H\x1b[F"), press(0x53, True) + press(0x47, True) + press(0x4F, True))
    def test_lone_esc_and_alt(self):
        self.assertEqual(self.keys(b"\x1b"), press(0x01))                                # 后面没东西 = Esc
        self.assertEqual(self.keys(b"\x1bf"), [(D, 0x38)] + press(0x21) + [(0, 0x38)])   # Option(Meta)+f = Alt+F
    def test_split_sequence(self):
        self.assertEqual(self.t.feed(b"\x1b["), [])                                       # 半截先不出
        self.assertEqual([e for _, e in self.t.feed(b"B")][0], press(0x50, True))

class Mouse(unittest.TestCase):
    def test_sgr(self):
        t = k.Term(cell=(8, 16))
        ev = t.feed(b"\x1b[<35;10;5M")             # 移动 (32 + 3 = 无键), 第一次只记位置
        self.assertEqual(ev, [])
        ev = t.feed(b"\x1b[<35;12;4M")
        self.assertEqual(ev, [("mouse", (16, -16, 0, 0))])
        self.assertEqual(t.feed(b"\x1b[<0;12;4M"), [("mouse", (0, 0, 0, 1))])            # 左键按下
        self.assertEqual(t.feed(b"\x1b[<32;13;4M"), [("mouse", (8, 0, 0, 1))])           # 按着拖
        self.assertEqual(t.feed(b"\x1b[<0;13;4m"), [("mouse", (0, 0, 0, 0))])            # 松开
        self.assertEqual(t.feed(b"\x1b[<2;13;4M"), [("mouse", (0, 0, 0, 2))])            # 右键
        self.assertEqual(t.feed(b"\x1b[<64;13;4M"), [("mouse", (0, 0, -1, 2))])          # 滚轮上

class Hotkeys(unittest.TestCase):
    def test_prefix(self):
        t = k.Term()
        self.assertEqual(t.feed(b"\x1dq"), [("cmd", "q")])                              # Ctrl+] 然后 q
        self.assertEqual(t.feed(b"\x1d\x1d"), [("key", [(D, 0x1D)] + press(0x1B) + [(0, 0x1D)])])   # 连按两次 = 送 Ctrl+]

if __name__ == "__main__":
    unittest.main()
