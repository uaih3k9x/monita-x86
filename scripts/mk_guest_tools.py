#!/usr/bin/env python3
"""把 tests/guest/*.S 汇编成 DOS .COM (clang 的 i386 目标 + .code16), 输出到 build/guest/。
不用链接器: 源码 .text 开头垫 0x100 字节 (PSP), 段内偏移 = COM 里的偏移; 这里按段基址 0 自己做
R_386_16 / R_386_32 重定位 (只许指向 .text 本身), 然后去掉前 0x100 字节。
  python3 scripts/mk_guest_tools.py
"""
import glob, os, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "guest")

def sections(elf):
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 0x2E)
    hdrs = [struct.unpack_from("<IIIIIIIIII", elf, shoff + i * shentsize) for i in range(shnum)]
    stroff = hdrs[shstrndx][4]
    def name(o): return elf[stroff + o: elf.index(b"\0", stroff + o)].decode()
    return {name(h[0]): h for h in hdrs}

def main():
    os.makedirs(OUT, exist_ok=True)
    for src in sorted(glob.glob(os.path.join(ROOT, "tests", "guest", "*.S"))):
        base = os.path.splitext(os.path.basename(src))[0]
        obj = os.path.join(OUT, base + ".o")
        subprocess.check_call(["clang", "-target", "i386-pc-none-elf", "-c", src, "-o", obj])
        elf = open(obj, "rb").read()
        sec = sections(elf)
        h = sec[".text"]
        text = bytearray(elf[h[4]: h[4] + h[5]])
        tidx = list(sec).index(".text")
        sym = sec[".symtab"]
        for n, r in sec.items():
            if not n.startswith(".rel") or not r[5]:
                continue
            if n != ".rel.text":
                sys.exit("%s: 意外的重定位段 %s" % (src, n))
            for k in range(r[5] // 8):
                off, info = struct.unpack_from("<II", elf, r[4] + 8 * k)
                st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", elf, sym[4] + 16 * (info >> 8))
                if st_shndx != tidx:
                    sys.exit("%s: 重定位指向 .text 以外" % src)
                t = info & 0xff
                if t == 20:   # R_386_16
                    v, = struct.unpack_from("<H", text, off); struct.pack_into("<H", text, off, (v + st_value) & 0xffff)
                elif t == 1:  # R_386_32
                    v, = struct.unpack_from("<I", text, off); struct.pack_into("<I", text, off, (v + st_value) & 0xffffffff)
                else:
                    sys.exit("%s: 不支持的重定位类型 %d" % (src, t))
        com = bytes(text[0x100:])
        dst = os.path.join(OUT, base.upper() + ".COM")
        open(dst, "wb").write(com)
        os.remove(obj)
        print("%s  %d B" % (os.path.relpath(dst, ROOT), len(com)))

if __name__ == "__main__":
    main()
