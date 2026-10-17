# Top-level dispatcher. Real logic lives in the scripts each target calls.
REPO := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export PS5_PAYLOAD_SDK ?= $(REPO)/.sdk
export PS5_HOST ?= 192.168.3.211
export PLDMGR_PORT ?= 8084

.PHONY: sdk hello deps-host deps-ps5 compositor-host compositor-ps5 deploy logs \
	mesa-ps5 gpud-ps5 clients-ps5 vkcube-ps5 launcher ps5-app

sdk:
	$(REPO)/toolchain/install-sdk.sh

hello:
	$(MAKE) -C $(REPO)/examples/hello

deps-host:
	$(REPO)/deps/host/build.sh

deps-ps5:
	$(REPO)/deps/ps5/build.sh

compositor-host:
	$(REPO)/compositor/build.sh host

compositor-ps5:
	$(REPO)/compositor/build.sh ps5

# PS5 RADV (bundled by the launcher) and the Venus client driver.
mesa-ps5:
	$(REPO)/env/run $(REPO)/deps/mesa/build.sh tools
	$(REPO)/env/run $(REPO)/deps/mesa/build.sh ps5
	$(REPO)/env/run $(REPO)/deps/mesa/build.sh ps5 --driver venus

gpud-ps5:
	$(REPO)/env/run python3 $(REPO)/gpu-server/build.py ps5

clients-ps5:
	$(REPO)/clients/build.sh ps5

vkcube-ps5:
	$(REPO)/env/run $(REPO)/clients/vkcube/build.sh ps5 --driver venus

launcher:
	$(REPO)/env/run $(MAKE) -C $(REPO)/launcher all

# Everything needed for the installable PS5 app, in dependency order.
ps5-app: sdk deps-host deps-ps5 mesa-ps5 gpud-ps5
	$(MAKE) compositor-ps5 clients-ps5 vkcube-ps5 launcher

# make deploy ELF=path/to/payload.elf
deploy:
	$(REPO)/tools/ps5ctl run $(ELF)

# make logs PORT=9530
logs:
	$(REPO)/tools/ps5ctl logs --port $(or $(PORT),9530)
