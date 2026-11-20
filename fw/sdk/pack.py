#!/usr/bin/env python3
"""Pack app.elf -> 2PPA container (base 0x60270000). Static only; does not flash.

Container layout (checked byte-for-byte against the stock R05 app container, which is NOT shipped here):
  u32 magic 0x41505032 "2PPA" | u32 total_len | u32 crc32(@8=0) | u32 version
  u32 enter | u32 exit | u32 0 | u32 0
  then 16-byte load descriptors (kind, fileoff, size, addr), kind=0 terminates:
    kind=1/2 RAM copy (bytes at fileoff -> addr), kind=3 bss zero-fill,
    kind=4 XIP flash region (fileoff == addr-0x60270000), one size=0 end-marker.
"""
import struct, subprocess, zlib, sys, os

BASE  = 0x60270000
READELF = "arm-none-eabi-readelf"
OBJCOPY = "arm-none-eabi-objcopy"

def sh(*a): return subprocess.check_output(a).decode()

def sections(elf):
    secs = {}
    for ln in sh(READELF, "-S", "-W", elf).splitlines():
        ln = ln.strip()
        if not ln.startswith("[") or "]" not in ln: continue
        body = ln.split("]", 1)[1].split()
        if len(body) < 6: continue
        name = body[0]
        try:
            typ, addr, off, size = body[1], int(body[2],16), int(body[3],16), int(body[4],16)
        except ValueError:
            continue
        secs[name] = dict(type=typ, addr=addr, off=off, size=size)
    return secs

def symaddr(elf, name):
    for ln in sh(READELF, "-s", "-W", elf).splitlines():
        p = ln.split()
        if len(p) >= 8 and p[7] == name:
            return int(p[1], 16)
    raise KeyError(name)

def secbytes(elf, name):
    tmp = name.strip(".") + ".bin"
    subprocess.check_call([OBJCOPY, "-O", "binary", "--only-section", name, elf, tmp])
    d = open(tmp, "rb").read(); os.remove(tmp)
    return d

def align(n, a=4): return (n + a - 1) & ~(a - 1)

def main():
    elf = sys.argv[1]; out = sys.argv[2]
    s = sections(elf)
    corestub = secbytes(elf, ".corestub") if ".corestub" in s and s[".corestub"]["size"] else b""
    text     = secbytes(elf, ".text")
    data     = secbytes(elf, ".data") if ".data" in s and s[".data"]["size"] else b""
    enter = symaddr(elf, "appimg_enter")
    exit_ = symaddr(elf, "appimg_exit")
    text_addr = s[".text"]["addr"]
    stub_addr = s[".corestub"]["addr"] if corestub else 0x80fe0000
    data_addr = s[".data"]["addr"] if data else 0
    bss_addr  = s[".bss"]["addr"]; bss_size = s[".bss"]["size"]

    # file layout: header(0x80) | corestub@0x80 | data | text@(text_addr-BASE)
    HDR = 0x80
    off_stub = HDR
    off_data = align(off_stub + len(corestub), 0x10)
    off_text = text_addr - BASE
    assert off_data + len(data) <= off_text, "corestub+data overflow into text slot"

    # 原厂: kind4 size 不对齐 (0x9f6fa), total_len/末尾标记对齐到 0x20 (0xa09a0)
    total = align(off_text + len(text), 0x20)
    buf = bytearray(total)
    buf[off_stub:off_stub+len(corestub)] = corestub
    if data: buf[off_data:off_data+len(data)] = data
    buf[off_text:off_text+len(text)] = text

    descs = []
    if corestub: descs.append((2, off_stub, len(corestub), stub_addr))
    descs.append((4, off_text, len(text), text_addr))
    descs.append((4, total, 0, BASE + total))            # XIP end-marker
    if data: descs.append((1, off_data, len(data), data_addr))
    if bss_size: descs.append((3, 0, bss_size, bss_addr))
    descs.append((0, 0, 0, 0))                            # terminator

    struct.pack_into("<8I", buf, 0,
                     0x41505032, total, 0, 0x00010000, enter, exit_, 0, 0)
    o = 0x20
    for d in descs:
        struct.pack_into("<4I", buf, o, *d); o += 16
    assert o <= HDR, "descriptor table overflows header"

    crc = zlib.crc32(bytes(buf)) & 0xffffffff   # @8 already 0
    struct.pack_into("<I", buf, 8, crc)
    open(out, "wb").write(buf)
    print("packed %s  %d B (%#x)  enter=%#x exit=%#x crc32@8=%08x"
          % (out, total, total, enter, exit_, crc))
    print("  descriptors:")
    for k, fo, sz, ad in descs:
        print("    kind=%d off=%#06x size=%#07x addr=%#010x" % (k, fo, sz, ad))

if __name__ == "__main__":
    main()
