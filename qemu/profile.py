#!/usr/bin/env python3
"""穷人版采样剖析: 通过 QEMU gdbstub 定时打断、读 PC, 按函数汇总。
  QEMU_EXTRA="-gdb unix:/tmp/x86q.gdb,server=on,wait=off" bash qemu/run.sh win31 2 30 &
  python3 -I qemu/profile.py /tmp/x86q.gdb build/qemu/x86q.elf [采样数]"""
import bisect, socket, subprocess, sys, time

sock_path, elf = sys.argv[1], sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 400

def pkt(s):
    return b"$%s#%02x" % (s.encode(), sum(s.encode()) & 0xff)

for _ in range(100):
    try:
        s = socket.socket(socket.AF_UNIX); s.connect(sock_path); break
    except OSError:
        time.sleep(0.1)
s.settimeout(5)

def recv_packet():
    buf = b""
    while True:
        c = s.recv(1)
        if c == b"$":
            buf = b""
            while True:
                c = s.recv(1)
                if c == b"#":
                    s.recv(2); s.sendall(b"+"); return buf.decode()
                buf += c

s.sendall(pkt("?")); recv_packet()                         # 连上时 QEMU 会停住客户机: 握手后让它接着跑
s.sendall(pkt("c"))

syms = []
for ln in subprocess.check_output(["arm-none-eabi-nm", "-n", elf]).decode().splitlines():
    p = ln.split()
    if len(p) == 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]
hist = {}
for i in range(n):
    s.sendall(b"\x03"); recv_packet()                       # 停下 (T05...)
    s.sendall(pkt("p f")); r = recv_packet()                # PC
    pc = int.from_bytes(bytes.fromhex(r[:8]), "little")
    j = bisect.bisect_right(addrs, pc & ~1) - 1
    name = syms[j][1] if j >= 0 else "?"
    hist[name] = hist.get(name, 0) + 1
    s.sendall(pkt("c"))
    time.sleep(0.01)
s.sendall(b"\x03"); recv_packet(); s.sendall(pkt("k"))
for name, c in sorted(hist.items(), key=lambda x: -x[1])[:25]:
    print("%5.1f%%  %s" % (100.0 * c / n, name))
