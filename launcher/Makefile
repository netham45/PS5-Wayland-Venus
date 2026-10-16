REPO := $(abspath ..)
PAYLOAD ?= $(REPO)/build/ps5/compositor/ps5wl
DEMO ?= $(REPO)/build/ps5/clients/ps5wl-demo.elf
.PHONY: all clean FORCE
all: ps5wl-install.elf ps5wl-uninstall.elf ps5wl-launch-test.elf ps5wl-launch-state.elf ps5wl-close.elf ps5wl-launch-probe.elf
ifeq ($(PS5WL_BUILD_ENV),1)
CC := $(REPO)/cross/bin/prospero-clang
CPPFLAGS += -I$(REPO)/common/ps5log -Ivendor
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -pthread
LDLIBS += -pthread -lkernel_sys -lSceSystemService -lSceUserService -lSceAppInstUtil
SOURCES := main.c hbldr.c fs.c vendor/elfldr.c vendor/pt.c $(REPO)/common/ps5log/ps5log.c
ps5wl-launch-probe.elf: probe.c $(REPO)/common/ps5log/ps5log.c app.h Makefile $(CC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@
ps5wl-close.elf: close.c $(REPO)/common/ps5log/ps5log.c app.h Makefile $(CC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@
payload.elf: $(PAYLOAD) FORCE
	cp $(PAYLOAD) $@
demo.elf: $(DEMO) FORCE
	cp $(DEMO) $@
ps5wl-launch-test.elf: trigger.c $(REPO)/common/ps5log/ps5log.c Makefile $(CC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@
ps5wl-launch-state.elf: state.c $(REPO)/common/ps5log/ps5log.c Makefile $(CC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@
icon0.png: icon.py
	python3 $< $@
ps5wl-install.elf: $(SOURCES) hbldr.h fs.h payload.elf demo.elf icon0.png Makefile $(CC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(SOURCES) $(LDLIBS) -o $@
ps5wl-uninstall.elf: $(SOURCES) hbldr.h fs.h Makefile $(CC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -DUNINSTALL $(SOURCES) $(LDLIBS) -o $@
else
ps5wl-install.elf ps5wl-uninstall.elf:
	$(REPO)/env/run $(MAKE) -C $(CURDIR) all PAYLOAD=$(PAYLOAD) DEMO=$(DEMO)
endif
clean:
	rm -f ps5wl-install.elf ps5wl-uninstall.elf ps5wl-launch-probe.elf ps5wl-launch-test.elf ps5wl-launch-state.elf ps5wl-close.elf payload.elf demo.elf icon0.png
