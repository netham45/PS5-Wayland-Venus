# Top-level dispatcher. Real logic lives in the scripts each target calls.
REPO := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export PS5_PAYLOAD_SDK ?= $(REPO)/.sdk
export PS5_HOST ?= 192.168.3.211
export PLDMGR_PORT ?= 8084

.PHONY: sdk hello deps-host deps-ps5 compositor-host compositor-ps5 deploy logs

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

# make deploy ELF=path/to/payload.elf
deploy:
	$(REPO)/tools/ps5ctl run $(ELF)

# make logs PORT=9530
logs:
	$(REPO)/tools/ps5ctl logs --port $(or $(PORT),9530)
