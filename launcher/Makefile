REPO := $(abspath ..)
PS5_PAYLOAD_SDK ?= $(REPO)/.sdk
PAYLOAD ?= ps5wl-launch-probe.elf
.PHONY: all clean FORCE
all: ps5wl-install.elf ps5wl-uninstall.elf ps5wl-launch-test.elf
ifeq ($(PS5WL_BUILD_ENV),1)
CC := $(PS5_PAYLOAD_SDK)/bin/prospero-clang
CPPFLAGS += -I$(REPO)/common/ps5log -Ivendor
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -pthread
LDLIBS += -pthread -lkernel_sys -lSceSystemService -lSceUserService -lSceAppInstUtil
SOURCES := main.c hbldr.c fs.c vendor/elfldr.c vendor/pt.c $(REPO)/common/ps5log/ps5log.c
ps5wl-launch-probe.elf: probe.c $(REPO)/common/ps5log/ps5log.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@
payload.elf: $(PAYLOAD) FORCE
	cp $(PAYLOAD) $@
ps5wl-launch-test.elf: trigger.c $(REPO)/common/ps5log/ps5log.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@
icon0.png: icon.py
	python3 $< $@
ps5wl-install.elf: $(SOURCES) hbldr.h fs.h payload.elf icon0.png Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDLIBS) -o $@
ps5wl-uninstall.elf: $(SOURCES) hbldr.h fs.h Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -DUNINSTALL $(SOURCES) $(LDLIBS) -o $@
else
ps5wl-install.elf ps5wl-uninstall.elf:
	$(REPO)/env/run $(MAKE) -C $(CURDIR) all PAYLOAD=$(PAYLOAD)
endif
clean:
	rm -f ps5wl-install.elf ps5wl-uninstall.elf ps5wl-launch-probe.elf ps5wl-launch-test.elf payload.elf icon0.png
