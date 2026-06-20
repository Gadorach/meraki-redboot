SHELL := /usr/bin/env bash

CROSS_COMPILE ?= mipsel-linux-gnu-
REQUIRED_GCC_MAJOR ?= 10
REFERENCE_GCC47 ?= 0
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
NM := $(CROSS_COMPILE)nm
READELF := $(CROSS_COMPILE)readelf
SHA256SUM ?= sha256sum
PYTHON ?= python3
ROOT := ../..
BUILD_DIR ?= build
ARTIFACT_DIR ?= artifacts
COMMON_CFLAGS := -EL -mabi=32 -march=mips32r2 -msoft-float -G 0 -mno-abicalls \
	-fno-pic -fno-pie -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
	-fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables -Os -Wall -Wextra -Werror=implicit-function-declaration \
	-Werror=attributes -fno-jump-tables -fno-tree-switch-conversion \
	-fno-tree-loop-distribute-patterns -fno-tree-loop-vectorize \
	-fno-tree-slp-vectorize -fno-strict-aliasing \
	-std=gnu89 -nostdlib -nostdinc -I$(ROOT)/include
ifeq ($(REFERENCE_GCC47),1)
COMMON_CFLAGS := $(filter-out -fno-pie -fno-tree-loop-vectorize,$(COMMON_CFLAGS))
endif

LDFLAGS := -EL -m elf32ltsmip -G 0 -static -n -nostdlib -T linker.ld

.PHONY: all clean test check-tools
all: check-tools $(ARTIFACT_DIR)/recovery-luton26.bin \
     $(ARTIFACT_DIR)/recovery-jaguar1.bin \
     $(ARTIFACT_DIR)/recovery-luton26.bin.sha256 \
     $(ARTIFACT_DIR)/recovery-jaguar1.bin.sha256 \
     $(ARTIFACT_DIR)/recovery-luton26.descriptor.json \
     $(ARTIFACT_DIR)/recovery-jaguar1.descriptor.json

check-tools:
	@for tool in "$(CC)" "$(LD)" "$(OBJCOPY)" "$(NM)" "$(READELF)" "$(PYTHON)"; do \
	  command -v "$$tool" >/dev/null 2>&1 || { echo "missing required tool: $$tool" >&2; exit 2; }; \
	done; \
	version=$$($(CC) -dumpfullversion -dumpversion); major=$${version%%.*}; \
	if [[ -n '$(REQUIRED_GCC_MAJOR)' && $$major != '$(REQUIRED_GCC_MAJOR)' ]]; then \
	  echo "unsupported recovery compiler $$version; GCC major $(REQUIRED_GCC_MAJOR) is required" >&2; \
	  exit 2; \
	fi

$(BUILD_DIR) $(ARTIFACT_DIR):
	@mkdir -p $@

$(BUILD_DIR)/recovery-luton26.o: recovery.c $(ROOT)/include/postmerkos_uart_crypto.h | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) -DRECOVERY_SOC_FAMILY=1 -c $< -o $@

$(BUILD_DIR)/recovery-jaguar1.o: recovery.c $(ROOT)/include/postmerkos_uart_crypto.h | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) -DRECOVERY_SOC_FAMILY=2 -c $< -o $@

$(BUILD_DIR)/recovery-luton26.elf: $(BUILD_DIR)/recovery-luton26.o linker.ld $(ROOT)/scripts/validate_fixed_payload.py
	$(LD) $(LDFLAGS) $< -o $@
	$(PYTHON) $(ROOT)/scripts/validate_fixed_payload.py --elf $@ --entry 0x81000000 --readelf $(READELF) --nm $(NM)

$(BUILD_DIR)/recovery-jaguar1.elf: $(BUILD_DIR)/recovery-jaguar1.o linker.ld $(ROOT)/scripts/validate_fixed_payload.py
	$(LD) $(LDFLAGS) $< -o $@
	$(PYTHON) $(ROOT)/scripts/validate_fixed_payload.py --elf $@ --entry 0x81000000 --readelf $(READELF) --nm $(NM)

$(ARTIFACT_DIR)/recovery-%.bin: $(BUILD_DIR)/recovery-%.elf | $(ARTIFACT_DIR)
	$(OBJCOPY) -O binary $< $@
	@test $$(stat -c %s $@) -gt 0
	@test $$(stat -c %s $@) -le 4194304

$(ARTIFACT_DIR)/%.bin.sha256: $(ARTIFACT_DIR)/%.bin
	@cd $(ARTIFACT_DIR) && $(SHA256SUM) $*.bin > $*.bin.sha256

$(ARTIFACT_DIR)/recovery-luton26.descriptor.json: $(ARTIFACT_DIR)/recovery-luton26.bin
	@$(PYTHON) write_descriptor.py --family luton26 --family-id 1 --spi-address 0x70000064 --load-address 0x81000000 --entry-address 0x81000000 --binary $< --output $@

$(ARTIFACT_DIR)/recovery-jaguar1.descriptor.json: $(ARTIFACT_DIR)/recovery-jaguar1.bin
	@$(PYTHON) write_descriptor.py --family jaguar1 --family-id 2 --spi-address 0x70000068 --load-address 0x81000000 --entry-address 0x81000000 --binary $< --output $@

clean:
	rm -rf $(BUILD_DIR) $(ARTIFACT_DIR)
