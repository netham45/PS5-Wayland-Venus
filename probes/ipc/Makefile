REPO := $(abspath ../..)
PS5_PAYLOAD_SDK ?= $(REPO)/.sdk
CC := $(PS5_PAYLOAD_SDK)/bin/prospero-clang
CFLAGS := -O2 -g -Wall -Wextra -pthread -I$(REPO)/common/ps5log
COMMON := $(REPO)/common/ps5log/ps5log.c
.PHONY: all clean
all: ps5wl-probe-ipc-server.elf ps5wl-probe-ipc-client.elf
ps5wl-probe-ipc-server.elf: main.c $(COMMON)
	@test "$(PS5WL_BUILD_ENV)" = 1
	$(CC) $(CFLAGS) -DSERVER main.c $(COMMON) -o $@
ps5wl-probe-ipc-client.elf: main.c $(COMMON)
	@test "$(PS5WL_BUILD_ENV)" = 1
	$(CC) $(CFLAGS) main.c $(COMMON) -o $@
clean:
	rm -f ps5wl-probe-ipc-server.elf ps5wl-probe-ipc-client.elf
