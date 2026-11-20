#!/usr/bin/env python3
"""Generate corestub.S: one Helios/Unisoc core_stub per imported symbol.

Stub ABI (matches R05 原厂 app, verified against full_flash dump):
  Thumb: df f8 00 f0   = ldr.w pc, [pc, #0]
  then 4 bytes = import hash. At load time the R05 loader looks the hash up in
  the AP export table and overwrites these 4 bytes with the resolved Thumb addr.

Hash form (verified on 49 R03 pairs + 199 R05 app stubs):
  stub 4 bytes (file order) = sha256(name).digest()[:4] reversed
  => assembler `.word H` with H = int.from_bytes(sha256(name)[:4], 'big')
     emits exactly those 4 little-endian bytes, and reading them back as a
     LE u32 gives H, which is the key stored in the R05 export table.
"""
import hashlib, sys, os

def h_be(name: str) -> int:
    return int.from_bytes(hashlib.sha256(name.encode()).digest()[:4], "big")

def main():
    # usage: gen_stubs.py imports_<app>.txt out.S out_hashes.txt
    src, out_s, out_h = sys.argv[1:4]
    names = []   # (symbol, hash)
    for line in open(src):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if "=" in line:   # unresolved export: "symbol = 0xHASH" (LE u32 key as in r05_exports.csv)
            sym, h = (x.strip() for x in line.split("=", 1))
            names.append((sym, int(h, 16)))
        else:
            names.append((line, h_be(line)))
    out = [".syntax unified", ".thumb", "", "@ auto-generated core_stub table -> .corestub @ 0x80fe0000", ""]
    for n, h in names:
        out += [
            f".section .corestub.{n},\"ax\",%progbits",
            ".align 2",
            ".global %s" % n,
            ".thumb_func",
            "%s:" % n,
            "    .byte 0xdf,0xf8,0x00,0xf0   @ ldr.w pc,[pc,#0]",
            "    .word 0x%08x               @ hash(%s)" % (h, n),
            "",
        ]
    open(out_s, "w").write("\n".join(out) + "\n")
    with open(out_h, "w") as f:
        for n, h in names:
            f.write("%08x %s\n" % (h, n))
    print("generated %s with %d stubs" % (out_s, len(names)))

if __name__ == "__main__":
    main()
