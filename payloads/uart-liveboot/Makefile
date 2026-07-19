CROSS_COMPILE ?= mipsel-linux-gnu-
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
READELF := $(CROSS_COMPILE)readelf
NM := $(CROSS_COMPILE)nm
SHA256SUM ?= sha256sum
PYTHON ?= python3
ROOT := ../..
BUILD_DIR ?= build
ARTIFACT_DIR ?= artifacts
COMMON_CFLAGS := -EL -mabi=32 -march=mips32r2 -msoft-float -G 0 -mno-abicalls \
	-fno-pic -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
	-fomit-frame-pointer -ffunction-sections -fdata-sections -Os -Wall -Wextra \
	-Werror=implicit-function-declaration -Wno-unused-function -Wno-unused-variable \
	-std=gnu89 -nostdlib -nostdinc -I$(ROOT)/include
LDFLAGS := -EL -m elf32ltsmip -G 0 -static -n -nostdlib --gc-sections -T linker.ld

.PHONY: all clean test
all: $(ARTIFACT_DIR)/pmoslive-jaguar1.bin \
     $(ARTIFACT_DIR)/pmoslive-jaguar1.bin.sha256 \
     $(ARTIFACT_DIR)/pmoslive-jaguar1.descriptor.json

$(BUILD_DIR) $(ARTIFACT_DIR):
	@mkdir -p $@

$(BUILD_DIR)/entry.o: entry.S | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

$(BUILD_DIR)/liveboot.o: liveboot.c ../uart-firmware-recovery/recovery.c $(ROOT)/include/postmerkos_uart_crypto.h | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) -DRECOVERY_SOC_FAMILY=2 -c $< -o $@

$(BUILD_DIR)/pmoslive-jaguar1.elf: $(BUILD_DIR)/entry.o $(BUILD_DIR)/liveboot.o linker.ld
	$(LD) $(LDFLAGS) $(BUILD_DIR)/entry.o $(BUILD_DIR)/liveboot.o -o $@
	@$(READELF) -h $@ | grep -Eq "Entry point address:[[:space:]]+0x86c00000$$"
	@test -z "$$($(NM) -u $@)"

$(ARTIFACT_DIR)/pmoslive-jaguar1.bin: $(BUILD_DIR)/pmoslive-jaguar1.elf | $(ARTIFACT_DIR)
	$(OBJCOPY) -O binary $< $@
	@test $$(stat -c %s $@) -gt 0
	@test $$(stat -c %s $@) -le 4194304
	@$(PYTHON) write_descriptor.py --binary $@ \
		--output $(BUILD_DIR)/pmoslive-jaguar1.validation.json

$(ARTIFACT_DIR)/%.bin.sha256: $(ARTIFACT_DIR)/%.bin
	@cd $(ARTIFACT_DIR) && $(SHA256SUM) $*.bin > $*.bin.sha256

$(ARTIFACT_DIR)/pmoslive-jaguar1.descriptor.json: $(ARTIFACT_DIR)/pmoslive-jaguar1.bin
	@$(PYTHON) write_descriptor.py --binary $< --output $@

clean:
	rm -rf $(BUILD_DIR) $(ARTIFACT_DIR)
