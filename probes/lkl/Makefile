REPO := $(abspath ../..)
CC := $(REPO)/cross/bin/prospero-clang
OUT := $(REPO)/build/ps5/lklcap
GROUPS := execmem backing process threads umtx faults cpu ptrace limits network storage introspection abi helper
PAYLOADS := $(addprefix $(OUT)/ps5wl-lklcap-,$(addsuffix .elf,$(GROUPS)))
CFLAGS := -O2 -g -Wall -Wextra -Wno-unused-function -pthread -I$(REPO)/common/ps5log
COMMON := $(REPO)/common/ps5log/ps5log.c
.PHONY: all
all: $(PAYLOADS)
$(OUT)/ps5wl-lklcap-%.elf: %.c cap.h cpu_features.h Makefile $(COMMON)
	@test "$(PS5WL_BUILD_ENV)" = 1 || (echo 'Use env/run make -C probes/lkl' >&2; exit 1)
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $< $(COMMON) -o $@
