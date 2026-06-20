SHELL := /usr/bin/env bash
CROSS_COMPILE ?= mipsel-linux-gnu-
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
READELF := $(CROSS_COMPILE)readelf
NM := $(CROSS_COMPILE)nm
ROOT := ../..
BUILD := build
ARTIFACTS := artifacts
CFLAGS := -EL -mabi=32 -march=mips32r2 -msoft-float -mno-abicalls -fno-pic -fno-pie -G0 \
	-ffreestanding -fno-builtin -fno-common -fno-stack-protector -fomit-frame-pointer \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -Os -pipe -nostdinc \
	-std=gnu89 -Wall -Wextra -Werror=implicit-function-declaration
LDFLAGS := -EL -m elf32ltsmip -G0 -static -nostdlib -T linker.ld -Map $(BUILD)/uart-smoke-test.map

.PHONY: all clean
all: $(ARTIFACTS)/uart-smoke-test.bin $(ARTIFACTS)/uart-smoke-test.bin.sha256

$(BUILD) $(ARTIFACTS):
	@mkdir -p $@

$(BUILD)/smoke.o: smoke.c | $(BUILD)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/uart-smoke-test.elf: $(BUILD)/smoke.o linker.ld | $(BUILD)
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS) -o $@ $(BUILD)/smoke.o
	@python3 $(ROOT)/scripts/validate_fixed_payload.py --elf $@ --entry 0x81000000 \
	  --entry-symbol _start --maximum-size 0x10000 --range-end 0x81010000 \
	  --readelf '$(READELF)' --nm '$(NM)' --report $(BUILD)/validation.txt

$(ARTIFACTS)/uart-smoke-test.bin: $(BUILD)/uart-smoke-test.elf | $(ARTIFACTS)
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) -O binary $< $@
	@python3 $(ROOT)/scripts/pad_binary.py --alignment 4 $@

$(ARTIFACTS)/uart-smoke-test.bin.sha256: $(ARTIFACTS)/uart-smoke-test.bin
	@sha256sum $< > $@

clean:
	@rm -rf $(BUILD) $(ARTIFACTS)
