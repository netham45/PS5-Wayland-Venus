REPO := $(abspath ../..)
PS5_PAYLOAD_SDK ?= $(REPO)/.sdk
CC := $(PS5_PAYLOAD_SDK)/bin/prospero-clang
CFLAGS := -O2 -g -Wall -Wextra -pthread -I$(REPO)/common/ps5log
COMMON := $(REPO)/common/ps5log/ps5log.c
.PHONY: all clean
all: ps5wl-probe-spawn-parent.elf ps5wl-probe-spawn-child.elf
ps5wl-probe-spawn-parent.elf: main.c $(COMMON)
	@test "$(PS5WL_BUILD_ENV)" = 1
	$(CC) $(CFLAGS) main.c $(COMMON) -o $@
ps5wl-probe-spawn-child.elf: main.c $(COMMON)
	@test "$(PS5WL_BUILD_ENV)" = 1
	$(CC) $(CFLAGS) -DCHILD main.c $(COMMON) -o $@
clean:
	rm -f ps5wl-probe-spawn-parent.elf ps5wl-probe-spawn-child.elf
