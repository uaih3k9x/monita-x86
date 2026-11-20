# 主机侧: 单测 + Mac host 版模拟器。固件在 fw/build.sh, QEMU 估速在 qemu/run.sh。
#   make test        跑全部主机单测 (C + 固件导入守卫 + Mac 转发工具, python)
#   make host        build/x86host (headless, 帧导出 PNG/PPM/TXT, 脚本注入键盘鼠标)
CC      ?= cc
B       := build
V       := vendor/tiny386
# 和固件同一组设备参数: BPP=16 + VGA_STORE_CHANGED (见 core/machine.h)
VDEFS   := -DMONITA_X86 -DBPP=16 -DVGA_STORE_CHANGED -DI386_ALIGNED_ONLY
CFLAGS  ?= -O2 -g
WARN    := -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function
INC     := -I$(V) -Icore -Iport -Ifw/link

VSRC    := $(V)/pc.c $(V)/i8042.c $(V)/i8254.c $(V)/i8257.c $(V)/i8259.c $(V)/ide.c $(V)/vga.c \
           $(V)/vgafont.c $(V)/pcspk.c $(V)/pci.c $(V)/misc.c $(V)/i386.c
CORE    := core/mxalloc.c core/overlay.c core/rcache.c core/mxblk.c core/machine.c core/decor.c port/nodev.c
HOSTSRC := host/main.c host/host_plat.c host/imgout.c
HDRS    := $(wildcard core/*.h port/*.h host/*.h $(V)/*.h)

TESTS   := overlay rcache x86proto imgout kbdmap memplan aeabi dirty mxblk hostalloc civil mxlibc xdisp

.PHONY: all test fw-test host clean
all: test host

# vendor 另编: -include core/mxvendor.h 把它直接调的 malloc / free 改走 plat_alloc (记账)
$(B)/o/$(V)/%.o: $(V)/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(VDEFS) -include core/mxvendor.h $(INC) -w -c -o $@ $<
$(B)/o/%.o: %.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(VDEFS) $(WARN) $(INC) -c -o $@ $<

HOSTOBJ := $(patsubst %.c,$(B)/o/%.o,$(VSRC) $(CORE) $(HOSTSRC))
$(B)/x86host: $(HOSTOBJ)
	$(CC) $(CFLAGS) -o $@ $^

host: $(B)/x86host

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
T_hostalloc := host/host_plat.c core/mxalloc.c
T_mxlibc   := -DMXLIBC_TEST port/mxlibc.c

$(B)/test_%: tests/test_%.c $(HDRS)
	@mkdir -p $(B)
	$(CC) -O1 -g $(WARN) -fsanitize=address,undefined $(INC) -Ihost -o $@ $< $(T_$*)

test: $(addprefix $(B)/test_,$(TESTS))
	@set -e; for t in $(TESTS); do ./$(B)/test_$$t; done
	@python3 -I fw/sdk/test_validate.py
	@python3 -I tests/test_x86kbd.py

# 固件导入守卫 (要 fw/app.imports)
fw-test:
	python3 -I fw/sdk/test_validate.py

clean:
	rm -rf $(B)
