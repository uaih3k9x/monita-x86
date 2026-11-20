# 主机侧: 单测 + Mac host 版模拟器。固件在 fw/build.sh, QEMU 粗估在 qemu/run.sh, Mac 键鼠转发在 mac/。
#   make test        跑全部主机单测 (C + python)
#   make host        build/x86host (headless, 帧导出 PPM/PNG/TXT, 脚本注入键盘)
#   make host-fast   同上, CPU 用 fast/ 快路径 (-DUSE_FAST)
#   make host-verify 快路径 + FAST_VERIFY (每条指令解释器重做一遍对比, 慢)
CC      ?= cc
B       := build
V       := vendor/tiny386
# 和固件同一组设备参数: BPP=16 + VGA_STORE_CHANGED (见 core/machine.h)
VDEFS   := -DMONITA_X86 -DBPP=16 -DVGA_STORE_CHANGED -DI386_ALIGNED_ONLY
CFLAGS  ?= -O2 -g
WARN    := -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function
INC     := -I$(V) -Icore -Iport -Ifw/link

VSRC    := $(V)/pc.c $(V)/i8042.c $(V)/i8254.c $(V)/i8257.c $(V)/i8259.c $(V)/ide.c $(V)/vga.c \
           $(V)/vgafont.c $(V)/pcspk.c $(V)/pci.c $(V)/misc.c
CORE    := core/overlay.c core/rcache.c core/mxblk.c core/machine.c core/decor.c port/nodev.c
HOSTSRC := host/main.c host/host_plat.c host/imgout.c

TESTS   := overlay rcache x86proto imgout kbdmap memplan aeabi dirty mxblk

.PHONY: all test fw-test host host-fast host-verify clean
all: test host

$(B):
	mkdir -p $(B)

$(B)/x86host: $(VSRC) $(V)/i386.c $(CORE) $(HOSTSRC) | $(B)
	$(CC) $(CFLAGS) $(VDEFS) $(INC) -w -o $@ $(VSRC) $(V)/i386.c $(CORE) $(HOSTSRC)

$(B)/x86host-fast: $(VSRC) $(V)/fast/fast.c $(CORE) $(HOSTSRC) | $(B)
	$(CC) $(CFLAGS) $(VDEFS) -DUSE_FAST $(INC) -w -o $@ $(VSRC) $(V)/fast/fast.c $(CORE) $(HOSTSRC)

$(B)/x86host-verify: $(VSRC) $(V)/fast/fast.c $(CORE) $(HOSTSRC) | $(B)
	$(CC) $(CFLAGS) $(VDEFS) -DUSE_FAST -DFAST_VERIFY $(INC) -w -o $@ $(VSRC) $(V)/fast/fast.c $(CORE) $(HOSTSRC)

host: $(B)/x86host
host-fast: $(B)/x86host-fast
host-verify: $(B)/x86host-verify

# 单测: 每个 tests/test_X.c 单独编, 依赖的 core 源在下面列
T_overlay  := core/overlay.c
T_rcache   := core/rcache.c
T_x86proto :=
T_imgout   := host/imgout.c
T_kbdmap   :=
T_memplan  :=
T_aeabi    := -DAEABI_TEST port/aeabi_div.c
T_dirty    :=
T_mxblk    := core/mxblk.c core/overlay.c core/rcache.c

$(B)/test_%: tests/test_%.c $(wildcard core/*.h port/*.h host/*.h) | $(B)
	$(CC) -O1 -g $(WARN) -fsanitize=address,undefined $(INC) -Ihost -o $@ $< $(T_$*)

test: $(addprefix $(B)/test_,$(TESTS))
	@set -e; for t in $(TESTS); do ./$(B)/test_$$t; done

# 固件导入守卫 (要 fw/app.imports)
fw-test:
	python3 -I fw/sdk/test_validate.py

clean:
	rm -rf $(B)
