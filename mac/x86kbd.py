#!/usr/bin/env python3
"""monita-x86 键盘鼠标转发 (Mac 终端 -> USB -> CS80 上的 x86 固件)。

  python3 mac/x86kbd.py              # 终端窗口里打字 = 客户机键盘, 在窗口里移动 / 点鼠标 = PS/2 鼠标
  python3 mac/x86kbd.py --stat       # 只问一次状态 (PC_STAT) 就退出
  python3 mac/x86kbd.py --cell 4x8   # 鼠标灵敏度: 终端一格折合多少像素 (默认 8x16)

热键: Ctrl+] 之后再按  q 退出  r 硬复位  R 硬复位并清空写覆盖层  d Ctrl+Alt+Del  p 暂停/继续  b 状态栏  s 状态  Ctrl+] 送 Ctrl+]
终端只给"按了一下", 没有抬起: 每个键都按下后立刻松开; 组合键用 Option(Meta) = Alt、Ctrl+字母。
鼠标用终端的 SGR 鼠标上报 (Terminal.app / iTerm2 都支持), 只在终端窗口里有效。

USB: 设备 VID 0x2c7c PID 0x0901 的接口 7 (固件 ql_uart_open(4), 厂商类, macOS 不绑驱动), pyusb + libusb 直接开。
     Monita 的 Swift 上位机在同一个接口上实测通过; 这个 pyusb 版没上过真机。
帧格式同 Monita link (A5 5A | type | seq | u16 len | payload | crc32), x86 载荷见 core/x86proto.h。
只依赖 pyusb (pip install pyusb; brew install libusb)。纯逻辑部分单测: tests/test_x86kbd.py。
"""
import argparse, os, select, struct, sys, termios, threading, time, tty, zlib

HELLO, HELLO_ACK, ACK = 0x01, 0x81, 0xFE
PC_KBD, PC_MOUSE, PC_CTRL, PC_STAT, PC_STAT_R = 0xA0, 0xA1, 0xA2, 0xA3, 0xE3
X86_CAP = 0x0800
OPS = {"r": (1, 0), "R": (1, 1), "d": (2, None), "p": (3, None), "b": (4, None)}

# ---- 链路帧 ----
def frame(typ, seq, payload=b""):
    body = bytes([typ, seq & 0xFF]) + struct.pack("<H", len(payload)) + payload
    return b"\xA5\x5A" + body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)

class Parser:
    """流式拆帧: feed(bytes) -> [(type, seq, payload)]; CRC 错的丢掉"""
    def __init__(self):
        self.buf = b""
    def feed(self, data):
        self.buf += data
        out = []
        while True:
            i = self.buf.find(b"\xA5\x5A")
            if i < 0:
                self.buf = self.buf[-1:] if self.buf.endswith(b"\xA5") else b""
                return out
            self.buf = self.buf[i:]
            if len(self.buf) < 6:
                return out
            n = struct.unpack_from("<H", self.buf, 4)[0]
            if len(self.buf) < 6 + n + 4:
                return out
            body, crc = self.buf[2:6 + n], struct.unpack_from("<I", self.buf, 6 + n)[0]
            if zlib.crc32(body) & 0xFFFFFFFF == crc:
                out.append((body[0], body[1], body[4:]))
                self.buf = self.buf[6 + n + 4:]
            else:
                self.buf = self.buf[2:]

def kbd_payload(events):
    return bytes([len(events)]) + b"".join(bytes([f, c]) for f, c in events)

def mouse_payload(dx, dy, dz, buttons):
    clamp = lambda v, lo, hi: max(lo, min(hi, v))
    return struct.pack("<hhbB", clamp(dx, -32768, 32767), clamp(dy, -32768, 32767), clamp(dz, -128, 127), buttons & 7)

# ---- 键位: 和 core/kbdmap.h 同一套 set-1 扫描码 (美式) ----
DOWN, E0 = 0x01, 0x02
SC_LSHIFT, SC_LCTRL, SC_LALT = 0x2A, 0x1D, 0x38
ROWS = [("1234567890-=", "!@#$%^&*()_+", 0x02), ("qwertyuiop[]", "QWERTYUIOP{}", 0x10),
        ("asdfghjkl;'`", "ASDFGHJKL:\"~", 0x1E), ("\\zxcvbnm,./", "|ZXCVBNM<>?", 0x2B)]
ASCII = {" ": (0x39, False), "\r": (0x1C, False), "\n": (0x1C, False), "\t": (0x0F, False),
         "\x7f": (0x0E, False), "\x08": (0x0E, False)}
for plain, shifted, base in ROWS:
    for i, ch in enumerate(plain):
        ASCII[ch] = (base + i, False)
    for i, ch in enumerate(shifted):
        ASCII[ch] = (base + i, True)
CSI_FINAL = {"A": (0x48, True), "B": (0x50, True), "C": (0x4D, True), "D": (0x4B, True),
             "H": (0x47, True), "F": (0x4F, True), "P": (0x3B, False), "Q": (0x3C, False),
             "R": (0x3D, False), "S": (0x3E, False)}
CSI_TILDE = {1: (0x47, True), 2: (0x52, True), 3: (0x53, True), 4: (0x4F, True), 5: (0x49, True),
             6: (0x51, True), 7: (0x47, True), 8: (0x4F, True), 15: (0x3F, False), 17: (0x40, False),
             18: (0x41, False), 19: (0x42, False), 20: (0x43, False), 21: (0x44, False),
             23: (0x57, False), 24: (0x58, False)}

def tap(code, e0=False):
    f = E0 if e0 else 0
    return [(DOWN | f, code), (f, code)]

def wrap(mod, events):
    return [(DOWN, mod)] + events + [(0, mod)]

def ascii_key(ch):
    if ch not in ASCII:
        return None
    code, shift = ASCII[ch]
    return wrap(SC_LSHIFT, tap(code)) if shift else tap(code)

class Term:
    """终端字节 -> 事件: ("key", [(flags, code), ...]) / ("mouse", (dx, dy, dz, buttons)) / ("cmd", 字母)"""
    def __init__(self, cell=(8, 16)):
        self.cw, self.ch = cell
        self.pend = b""
        self.prefix = False
        self.pos = None
        self.buttons = 0

    def flush(self):
        """读超时了还剩半截: 单独的 ESC 就是 Esc 键, 别的丢掉"""
        out = [("key", tap(0x01))] if self.pend == b"\x1b" else []
        self.pend = b""
        return out

    def feed(self, data):
        self.pend += data
        out = []
        while self.pend:
            n, ev = self._one(self.pend)
            if n == 0:
                break                                    # 半截, 等后面的字节
            self.pend = self.pend[n:]
            if ev:
                out.append(ev)
        return out

    def _one(self, b):
        c = b[0]
        if self.prefix:
            self.prefix = False
            if c == 0x1D:
                return 1, ("key", wrap(SC_LCTRL, tap(0x1B)))
            return 1, ("cmd", chr(c))
        if c == 0x1D:
            self.prefix = True
            return 1, None
        if c == 0x1B:
            return self._esc(b)
        ch = chr(c)
        if ch in ASCII:
            return 1, ("key", ascii_key(ch))
        if 1 <= c <= 26:                                 # Ctrl+字母
            return 1, ("key", wrap(SC_LCTRL, tap(ASCII[chr(c + 96)][0])))
        return 1, None                                   # 不认识的 (含非 ASCII) 丢掉

    def _esc(self, b):
        if len(b) < 2:
            return 0, None
        if b[1:2] == b"[":
            i = 2
            while i < len(b) and not (0x40 <= b[i] <= 0x7E):
                i += 1
            if i >= len(b):
                return 0, None
            params, final = b[2:i].decode("ascii", "replace"), chr(b[i])
            if params.startswith("<") and final in "Mm":
                return i + 1, self._mouse(params[1:], final == "M")
            if final == "~":
                k = CSI_TILDE.get(int(params.split(";")[0] or 0))
            else:
                k = CSI_FINAL.get(final)
            return i + 1, ("key", tap(k[0], k[1])) if k else None
        if b[1:2] == b"O":
            if len(b) < 3:
                return 0, None
            k = CSI_FINAL.get(chr(b[2]))
            return 3, ("key", tap(k[0], k[1])) if k else None
        k = ascii_key(chr(b[1]))                         # Option(Meta)+键 = Alt+键
        return 2, ("key", wrap(SC_LALT, k)) if k else None

    def _mouse(self, params, press):
        try:
            code, x, y = (int(v) for v in params.split(";"))
        except ValueError:
            return None
        dx = dy = dz = 0
        if self.pos is not None:
            dx, dy = (x - self.pos[0]) * self.cw, (y - self.pos[1]) * self.ch
        first = self.pos is None
        self.pos = (x, y)
        if code & 64:                                    # 滚轮
            dz = -1 if (code & 1) == 0 else 1
        elif not code & 32:                              # 按下 / 松开
            bit = {0: 1, 1: 4, 2: 2}.get(code & 3, 0)
            self.buttons = (self.buttons | bit) if press else (self.buttons & ~bit)
        elif first:
            return None                                  # 第一次移动只记位置
        return ("mouse", (dx, dy, dz, self.buttons))

# ---- USB ----
class Usb:
    def __init__(self, iface=7):
        import usb.core, usb.util
        self.dev = usb.core.find(idVendor=0x2C7C, idProduct=0x0901)
        if self.dev is None:
            sys.exit("没找到 CS80 (VID 2c7c PID 0901): USB 插好了吗?")
        cfg = self.dev.get_active_configuration()
        intf = cfg[(iface, 0)]
        usb.util.claim_interface(self.dev, iface)
        eps = [e for e in intf if usb.util.endpoint_type(e.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK]
        self.ep_in = next(e for e in eps if usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)
        self.ep_out = next(e for e in eps if usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
    def write(self, b):
        self.ep_out.write(b, timeout=1000)
    def read(self, timeout_ms=100):
        try:
            return bytes(self.ep_in.read(4096, timeout=timeout_ms))
        except Exception:
            return b""

class Link:
    def __init__(self, port):
        self.port, self.seq, self.parser = port, 0, Parser()
    def send(self, typ, payload=b""):
        self.seq = (self.seq + 1) & 0xFF
        self.port.write(frame(typ, self.seq, payload))
    def wait(self, want, timeout=2.0):
        t0 = time.time()
        while time.time() - t0 < timeout:
            for typ, seq, pl in self.parser.feed(self.port.read(100)):
                if typ == want:
                    return pl
        return None

def show_stat(pl):
    w = struct.unpack_from("<14I", pl, 0)
    log = pl[56:88].split(b"\0")[0].decode("ascii", "replace")
    names = ["ver", "flags", "kips", "fps_x10", "ov_used", "ov_cap", "ov_rejects", "heap_free_kib", "mem_kib",
             "vga_kib", "uptime_ms", "reset_reason", "yields_per_s", "in_dropped"]
    return "  ".join("%s=%d" % (n, v) for n, v in zip(names, w)) + ("  log=%r" % log if log else "")

def hello(link):
    tz = -time.timezone // 60 if not time.localtime().tm_isdst else -time.altzone // 60
    link.send(HELLO, struct.pack("<Ih", int(time.time()), tz))
    pl = link.wait(HELLO_ACK)
    if pl is None:
        sys.exit("设备没回 HELLO_ACK")
    caps = struct.unpack_from("<H", pl, 10)[0]
    if not caps & X86_CAP:
        sys.exit("设备上跑的不是 x86 固件 (caps=%04x)" % caps)

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stat", action="store_true")
    ap.add_argument("--cell", default="8x16", help="终端一格 = 多少像素 (鼠标灵敏度)")
    ap.add_argument("--no-mouse", action="store_true")
    a = ap.parse_args(argv)
    link = Link(Usb())
    hello(link)
    if a.stat:
        link.send(PC_STAT)
        pl = link.wait(PC_STAT_R)
        print(show_stat(pl) if pl else "没回状态")
        return
    cw, chh = (int(v) for v in a.cell.split("x"))
    term = Term((cw, chh))
    lock = threading.Lock()
    msgs = []
    def reader():
        while True:
            for typ, seq, pl in link.parser.feed(link.port.read(200)):
                if typ == PC_STAT_R:
                    msgs.append(show_stat(pl))
    threading.Thread(target=reader, daemon=True).start()
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    print("monita-x86 键盘鼠标转发: Ctrl+] q 退出, r 复位, d Ctrl+Alt+Del, p 暂停, s 状态\r")
    try:
        tty.setraw(fd)
        if not a.no_mouse:
            sys.stdout.write("\x1b[?1003h\x1b[?1006h"); sys.stdout.flush()
        while True:
            r, _, _ = select.select([fd], [], [], 0.05)
            evs = term.feed(os.read(fd, 256)) if r else term.flush()
            for kind, e in evs:
                with lock:
                    if kind == "key":
                        link.send(PC_KBD, kbd_payload(e))
                    elif kind == "mouse":
                        link.send(PC_MOUSE, mouse_payload(*e))
                    elif e == "q":
                        return
                    elif e == "s":
                        link.send(PC_STAT)
                    elif e in OPS:
                        op, arg = OPS[e]
                        link.send(PC_CTRL, bytes([op]) if arg is None else bytes([op, arg]))
            while msgs:
                sys.stdout.write("\r\n" + msgs.pop(0) + "\r\n"); sys.stdout.flush()
    finally:
        sys.stdout.write("\x1b[?1003l\x1b[?1006l"); sys.stdout.flush()
        termios.tcsetattr(fd, termios.TCSADRAIN, old)

if __name__ == "__main__":
    main()
