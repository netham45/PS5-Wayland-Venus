REPO := $(abspath ../..)
PS5_PAYLOAD_SDK ?= $(REPO)/.sdk

.PHONY: all clean
all: hello.elf

ifeq ($(PS5WL_BUILD_ENV),1)
export LLVM_CONFIG ?= /usr/bin/llvm-config
CC := $(REPO)/cross/bin/prospero-clang
CPPFLAGS += -I$(REPO)/common/ps5log
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -pthread
LDLIBS += -pthread

hello.elf: main.c $(REPO)/common/ps5log/ps5log.c $(REPO)/common/ps5log/ps5log.h Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(REPO)/common/ps5log/ps5log.c $(LDFLAGS) $(LDLIBS) -o $@
else
.PHONY: hello.elf
hello.elf:
	$(REPO)/env/run $(MAKE) -C $(CURDIR) all
endif

clean:
	rm -f hello.elf
