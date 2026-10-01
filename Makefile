# SPDX-License-Identifier: GPL-3.0-or-later
# PS5Cemu build. `make help` lists the targets. Missing dependencies are fetched at their pinned
# revisions (tools/deps.json); nothing that already exists is changed. The build runs on Linux with
# clang-18, lld-18 and the LLVM 18 tools, cmake, ninja, git, python3 and make.

SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := help
MAKEFLAGS += --no-print-directory

VERSION := 0.1.0
APP := build/app/PPSA99360
JOBS ?= $(shell nproc)
export JOBS

.PHONY: help deps deps-status radv build check package release clean distclean

help: ## List the targets
	@echo 'PS5Cemu build - make [target] [VARIABLE=value]'
	@echo
	@awk 'BEGIN { FS = ":.*## " } /^[a-z][a-z-]*:.*## / { printf "  %-12s %s\n", $$1, $$2 }' $(MAKEFILE_LIST)
	@echo
	@echo 'Variables:'
	@echo '  JOBS=$(JOBS)                parallel compile jobs'
	@echo '  RADV_ARCHIVE, RADV_SDK      a RADV built elsewhere, and the SDK fork it was built with'

deps: ## Fetch the pinned inputs and build the libraries Cemu needs for the PS5
	python3 -B tools/deps.py fetch
	bash tools/build-deps.sh

deps-status: ## Show every dependency and whether it matches its pin
	@python3 -B tools/deps.py status

radv: deps ## Build RADV, the Vulkan driver, with PS5_Vulkan's recipe
	bash tools/build-radv.sh

build: deps $(if $(RADV_ARCHIVE),,radv) ## Build Cemu for the PS5 and link it with RADV: build/cemu/ps5cemu.elf
	bash tools/build-cemu.sh

check: deps ## Build and package everything with a stand-in for RADV, to check the build (not an app)
	PS5CEMU_LINK_CHECK=1 bash tools/build-cemu.sh
	bash tools/package.sh --check

package: build ## The app folder: build/app/PPSA99360
	bash tools/package.sh

release: package ## dist/PS5Cemu-vVERSION.zip (the PPSA99360 folder) and SHA256SUMS
	@mkdir -p dist
	rm -f dist/PS5Cemu-v$(VERSION).zip
	cd build/app && python3 -m zipfile -c ../../dist/PS5Cemu-v$(VERSION).zip PPSA99360
	cd dist && sha256sum PS5Cemu-v$(VERSION).zip > SHA256SUMS
	@cat dist/SHA256SUMS

clean: ## Remove build outputs (build/, dist/); dependencies stay
	rm -rf build dist

distclean: clean ## Also remove the fetched dependencies in .deps
	python3 -B tools/deps.py clean
