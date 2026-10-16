REPO := $(abspath ../..)
PS5_PAYLOAD_SDK ?= $(REPO)/.sdk
CC := $(PS5_PAYLOAD_SDK)/bin/prospero-clang
CFLAGS := -O2 -g -Wall -Wextra -pthread -I$(REPO)/common/ps5log -I$(REPO)/build/ps5/sysroot/include/libepoll-shim
.PHONY: all clean
all: ps5wl-probe-events.elf ps5wl-probe-events-shim.elf
ps5wl-probe-events.elf: main.c Makefile $(REPO)/common/ps5log/ps5log.c
	@test "$(PS5WL_BUILD_ENV)" = 1
	$(CC) $(CFLAGS) -DNATIVE_ONLY main.c $(REPO)/common/ps5log/ps5log.c -o $@
ps5wl-probe-events-shim.elf: main.c Makefile $(REPO)/common/ps5log/ps5log.c $(REPO)/build/ps5/sysroot/lib/libepoll-shim.a
	@test "$(PS5WL_BUILD_ENV)" = 1
	$(CC) $(CFLAGS) -DSHIM_ONLY main.c $(REPO)/common/ps5log/ps5log.c -L$(REPO)/build/ps5/sysroot/lib -lepoll-shim -o $@
clean:
	rm -f ps5wl-probe-events.elf ps5wl-probe-events-shim.elf
