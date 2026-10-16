REPO := $(abspath ../..)
CC := $(REPO)/cross/bin/prospero-clang
CFLAGS := -O2 -g -Wall -Wextra -pthread -I$(REPO)/common/ps5log
COMMON := $(REPO)/common/ps5log/ps5log.c
COMPAT_DEPS := $(REPO)/deps/ps5/shim/fd_compat.c $(REPO)/cross/compiler.sh $(REPO)/cross/bootstrap-compat.sh
.PHONY: all clean
all: ps5wl-probe-spawn-parent.elf ps5wl-probe-spawn-child.elf
ps5wl-probe-spawn-parent.elf: main.c Makefile $(CC) $(COMPAT_DEPS) $(COMMON)
	@test "$(PS5WL_BUILD_ENV)" = 1
	$(CC) $(CFLAGS) main.c $(COMMON) -o $@
ps5wl-probe-spawn-child.elf: main.c Makefile $(CC) $(COMPAT_DEPS) $(COMMON)
	@test "$(PS5WL_BUILD_ENV)" = 1
	$(CC) $(CFLAGS) -DCHILD main.c $(COMMON) -o $@
clean:
	rm -f ps5wl-probe-spawn-parent.elf ps5wl-probe-spawn-child.elf
