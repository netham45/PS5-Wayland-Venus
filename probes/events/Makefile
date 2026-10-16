REPO := $(abspath ../..)
CC := $(REPO)/cross/bin/prospero-clang
CFLAGS := -O2 -g -Wall -Wextra -pthread -I$(REPO)/common/ps5log -I$(REPO)/build/ps5/sysroot/include/libepoll-shim
COMPAT_DEPS := $(REPO)/deps/ps5/shim/fd_compat.c $(REPO)/cross/compiler.sh $(REPO)/cross/bootstrap-compat.sh
.PHONY: all clean
all: ps5wl-probe-events.elf ps5wl-probe-events-shim.elf
ps5wl-probe-events.elf: main.c Makefile $(CC) $(COMPAT_DEPS) $(REPO)/common/ps5log/ps5log.c
	@test "$(PS5WL_BUILD_ENV)" = 1
	$(CC) $(CFLAGS) -DNATIVE_ONLY main.c $(REPO)/common/ps5log/ps5log.c -o $@
ps5wl-probe-events-shim.elf: main.c Makefile $(CC) $(COMPAT_DEPS) $(REPO)/common/ps5log/ps5log.c $(REPO)/build/ps5/sysroot/lib/libepoll-shim.a
	@test "$(PS5WL_BUILD_ENV)" = 1
	$(CC) $(CFLAGS) -DSHIM_ONLY main.c $(REPO)/common/ps5log/ps5log.c -L$(REPO)/build/ps5/sysroot/lib -lepoll-shim -o $@
clean:
	rm -f ps5wl-probe-events.elf ps5wl-probe-events-shim.elf
