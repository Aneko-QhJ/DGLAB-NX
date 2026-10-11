# Keep the public make/package interface; CMake owns both isolated NRO builds.
UI_BACKEND ?= borealis
APP_TITLE := DGLAB-NX
APP_AUTHOR := Aneko
APP_VERSION ?= $(shell cat ../VERSION)
BUILD_STAMP := $(shell git describe --always --dirty 2>/dev/null || echo unknown)
CMAKE ?= cmake
JOBS ?= 8
BUILD := build/$(UI_BACKEND)
RELEASE_DIR := ../build/DGLAB-NX
DGLAB_PORTLIBS ?= $(if $(wildcard $(DEVKITPRO)/portlibs/switch/lib/libglfw3.a),$(DEVKITPRO)/portlibs/switch,$(CURDIR)/build/portlibs)

ifeq ($(filter $(UI_BACKEND),borealis framebuffer),)
$(error UI_BACKEND must be borealis or framebuffer)
endif
ifeq ($(strip $(DEVKITPRO)),)
$(error Please set DEVKITPRO)
endif

.PHONY: all package configure deps clean
all: package
deps:
	python3 tools/setup_portlibs.py
configure:
	$(CMAKE) -S . -B $(BUILD) -DCMAKE_TOOLCHAIN_FILE=$(DEVKITPRO)/cmake/Switch.cmake \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
		-DUI_BACKEND=$(UI_BACKEND) -DDGLAB_PORTLIBS="$(DGLAB_PORTLIBS)" \
		-DAPP_TITLE="$(APP_TITLE)" -DAPP_AUTHOR="$(APP_AUTHOR)" \
		-DAPP_VERSION="$(APP_VERSION)" -DBUILD_STAMP="$(BUILD_STAMP)"
package: configure
	$(CMAKE) --build $(BUILD) --parallel $(JOBS)
	@test -n "$(wildcard ../lang/*.json)" || { echo "error: no language files"; exit 1; }
	@mkdir -p $(RELEASE_DIR)/lang
	@cp $(BUILD)/DGLAB-NX.nro $(RELEASE_DIR)/DGLAB-NX.nro
	@cp ../lang/*.json $(RELEASE_DIR)/lang/
	@echo "released $(RELEASE_DIR)/DGLAB-NX.nro ($(UI_BACKEND), $(APP_VERSION), $(BUILD_STAMP))"
clean:
	rm -rf build DGLAB-NX.elf DGLAB-NX.nro DGLAB-NX.nacp DGLAB-NX.map
