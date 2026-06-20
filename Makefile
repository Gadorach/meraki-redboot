SHELL := /usr/bin/env bash
.DEFAULT_GOAL := all

# Named profiles select independent CRC and size policies. A custom profile is
# also allowed when CRC_POLICY and SIZE_POLICY are supplied explicitly.
VARIANT ?= development
CRC_POLICY_strict := strict
SIZE_POLICY_strict := legacy-strict
CRC_POLICY_development := warn
SIZE_POLICY_development := legacy-warn
CRC_POLICY_permissive := off
SIZE_POLICY_permissive := hard-only
CRC_POLICY ?= $(CRC_POLICY_$(VARIANT))
SIZE_POLICY ?= $(SIZE_POLICY_$(VARIANT))
UART_RAMLOADER_strict := 0
UART_RAMLOADER_development := 1
UART_RAMLOADER_permissive := 0
UART_RAMLOADER ?= $(if $(UART_RAMLOADER_$(VARIANT)),$(UART_RAMLOADER_$(VARIANT)),0)
UART_RAMLOADER_MAX_SIZE ?= 0x00400000
UART_RAMLOADER_RAM_START ?= 0x81000000
UART_RAMLOADER_RAM_END ?= 0x87f00000
UART_RAMLOADER_PROBE_TIMEOUT_MS ?= 3000
UART_RAMLOADER_INTERBYTE_TIMEOUT_MS ?= 3000
UART_MENU_TIMEOUT_MS ?= 5000
UART_RAMLOADER_COUNT_HZ ?= 208000000
UART_STAGE1_ADDRESS ?= 0xa7f00000
UART_STAGE1_MAX_SIZE ?= 0x00100000
UART_STAGE1_RANGE_END := $(shell printf '0x%08x' $$(( $(UART_STAGE1_ADDRESS) + $(UART_STAGE1_MAX_SIZE) )))

VALID_CRC_POLICIES := strict warn off
VALID_SIZE_POLICIES := legacy-strict legacy-warn hard-only
ifeq ($(filter $(CRC_POLICY),$(VALID_CRC_POLICIES)),)
$(error CRC_POLICY must be one of: $(VALID_CRC_POLICIES); named profiles are strict, development, permissive)
endif
ifeq ($(filter $(SIZE_POLICY),$(VALID_SIZE_POLICIES)),)
$(error SIZE_POLICY must be one of: $(VALID_SIZE_POLICIES); named profiles are strict, development, permissive)
endif

CROSS_COMPILE ?= mipsel-linux-gnu-
REQUIRED_GCC_MAJOR ?= 10
JOBS ?= $(shell nproc 2>/dev/null || echo 1)
SOURCE_DATE_EPOCH ?= 1605204110
BUILD_TARGET ?= all

LOADER_REGION_SIZE := 0x00040000
FALLBACK_REGION_SIZE ?= 0x00400000
PAYLOAD_SLOT_END ?= $(FALLBACK_REGION_SIZE)
HARD_PAYLOAD_LIMIT ?= $(shell printf '0x%08x' $$(( $(PAYLOAD_SLOT_END) - $(LOADER_REGION_SIZE) - 0x20 )))
LEGACY_PAYLOAD_LIMIT ?= $(HARD_PAYLOAD_LIMIT)

CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
NM := $(CROSS_COMPILE)nm
READELF := $(CROSS_COMPILE)readelf

BUILD_DIR := build/$(VARIANT)
OUT_DIR := out/$(VARIANT)
ARTIFACT_DIR := artifacts
ARTIFACT := $(ARTIFACT_DIR)/vcoreiii-linuxloader-$(VARIANT).bin
LOADER_ELF := $(BUILD_DIR)/loader.elf
LOADER_BIN := $(BUILD_DIR)/loader.bin
LOADER_MAP := $(BUILD_DIR)/loader.map
LOADER_SYM := $(BUILD_DIR)/loader.sym
WRAPPER_ELF := $(BUILD_DIR)/boot-region.elf
WRAPPER_OBJ := $(BUILD_DIR)/boot_wrapper.o
TOOLCHAIN_INFO := $(BUILD_DIR)/toolchain.txt
CODEGEN_REPORT := $(BUILD_DIR)/loader-codegen-report.txt
UART_STAGE1_OBJ := $(BUILD_DIR)/uart-stage1.o
UART_STAGE1_ELF := $(BUILD_DIR)/uart-stage1.elf
UART_STAGE1_BIN := $(BUILD_DIR)/uart-stage1.bin
UART_STAGE1_MAP := $(BUILD_DIR)/uart-stage1.map
UART_STAGE1_SYM := $(BUILD_DIR)/uart-stage1.sym
UART_STAGE1_REPORT := $(BUILD_DIR)/uart-stage1-codegen-report.txt
UART_STAGE1_ARTIFACT := $(ARTIFACT_DIR)/vcoreiii-uart-stage1-$(VARIANT).bin
UART_RECOVERY_DIR := $(BUILD_DIR)/embedded-recovery
UART_RECOVERY_BUILD_DIR := $(UART_RECOVERY_DIR)/build
UART_RECOVERY_ARTIFACT_DIR := $(UART_RECOVERY_DIR)/artifacts
UART_RECOVERY_STAMP := $(UART_RECOVERY_DIR)/.built
UART_RECOVERY_LUTON26_BIN := $(UART_RECOVERY_ARTIFACT_DIR)/recovery-luton26.bin
UART_RECOVERY_JAGUAR1_BIN := $(UART_RECOVERY_ARTIFACT_DIR)/recovery-jaguar1.bin
UART_RECOVERY_BLOBS_OBJ := $(BUILD_DIR)/uart-recovery-blobs.o
UART_RECOVERY_LUTON26_ARTIFACT := $(ARTIFACT_DIR)/vcoreiii-recovery-luton26.bin
UART_RECOVERY_JAGUAR1_ARTIFACT := $(ARTIFACT_DIR)/vcoreiii-recovery-jaguar1.bin
WRAPPER_FIT_SHELL = fallback=$$((0x40000 - len)); active=$$((fallback - len)); test $$active -ge 4096 || { echo 'loader is too large for dual-slot 256 KiB boot region' >&2; exit 1; }

CPP_CRC_strict := -DCONFIG_CRC_POLICY_STRICT=1
CPP_CRC_warn := -DCONFIG_CRC_POLICY_WARN=1
CPP_CRC_off := -DCONFIG_CRC_POLICY_OFF=1
CPP_SIZE_legacy-strict := -DCONFIG_SIZE_POLICY_LEGACY_STRICT=1
CPP_SIZE_legacy-warn := -DCONFIG_SIZE_POLICY_LEGACY_WARN=1
CPP_SIZE_hard-only := -DCONFIG_SIZE_POLICY_HARD_ONLY=1
POLICY_CPPFLAGS := \
	$(CPP_CRC_$(CRC_POLICY)) \
	$(CPP_SIZE_$(SIZE_POLICY)) \
	-DCONFIG_FALLBACK_REGION_SIZE=$(FALLBACK_REGION_SIZE) \
	-DCONFIG_PAYLOAD_SLOT_END=$(PAYLOAD_SLOT_END) \
	-DCONFIG_LEGACY_PAYLOAD_LIMIT=$(LEGACY_PAYLOAD_LIMIT) \
	-DCONFIG_HARD_PAYLOAD_LIMIT=$(HARD_PAYLOAD_LIMIT) \
	-DCONFIG_LOADER_REGION_SIZE=$(LOADER_REGION_SIZE) \
	$(if $(filter 1 y yes true,$(UART_RAMLOADER)),-DCONFIG_UART_RAMLOADER=1,) \
	-DCONFIG_UART_RAMLOADER_MAX_SIZE=$(UART_RAMLOADER_MAX_SIZE) \
	-DCONFIG_UART_RAMLOADER_RAM_START=$(UART_RAMLOADER_RAM_START) \
	-DCONFIG_UART_RAMLOADER_RAM_END=$(UART_RAMLOADER_RAM_END) \
	-DCONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS=$(UART_RAMLOADER_PROBE_TIMEOUT_MS) \
	-DCONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS=$(UART_RAMLOADER_INTERBYTE_TIMEOUT_MS) \
	-DCONFIG_UART_MENU_TIMEOUT_MS=$(UART_MENU_TIMEOUT_MS) \
	-DCONFIG_UART_RAMLOADER_COUNT_HZ=$(UART_RAMLOADER_COUNT_HZ) \
	-DCONFIG_UART_STAGE1_ADDRESS=$(UART_STAGE1_ADDRESS) \
	-DCONFIG_UART_STAGE1_MAX_SIZE=$(UART_STAGE1_MAX_SIZE)

# GCC 10 early-init C is deliberately data-free and no-ABI.  Assembly forms
# each leaf stage address as runtime-loader-base + link-time offset.  The C
# stages therefore need no GOT, no absolute loader data, no calls, and no stack.
# Ordinary fixed-RAM stage 1 remains non-PIC C after DDR is operational.
ARCH_FLAGS := -EL -mabi=32 -march=mips32r2 -msoft-float
FREESTANDING_FLAGS := \
	-ffreestanding -fno-builtin -fno-common \
	-fno-stack-protector -fomit-frame-pointer \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-Os -pipe -nostdinc -I$(CURDIR)/include -I$(CURDIR)/src \
	-Wa,-mips32r2 -Wa,--trap

C_BASE_FLAGS := $(ARCH_FLAGS) $(FREESTANDING_FLAGS) \
	-mno-abicalls -fno-pic -fno-pie -G 0 \
	-std=gnu89 -Wall -Wextra -Werror=implicit-function-declaration \
	-Wno-unused-function -Wno-unused-parameter -Wno-sign-compare \
	-Wno-error=date-time -Werror=attributes -Werror=inline \
	-fno-jump-tables -fno-tree-switch-conversion \
	-fno-merge-constants -fno-merge-all-constants -fno-strict-aliasing \
	-fno-tree-loop-distribute-patterns -fno-tree-loop-vectorize \
	-fno-tree-slp-vectorize

# Optional source-regression mode for the supplied GCC 4.7.3 reference
# toolchain. It changes only flags that did not exist in GCC 4.7; the
# authoritative release target remains GCC 10.
REFERENCE_GCC47 ?= 0
ifeq ($(REFERENCE_GCC47),1)
C_BASE_FLAGS := $(filter-out -fno-tree-loop-vectorize -Wno-error=date-time -fno-pie,$(C_BASE_FLAGS))
endif

# s0-s7 are scratch only in the stackless reset-time stage translation units.
# The post-DDR UART engine retains the normal o32 saved-register convention.
PRE_DDR_CALL_USED_FLAGS := \
	-fcall-used-s0 -fcall-used-s1 -fcall-used-s2 -fcall-used-s3 \
	-fcall-used-s4 -fcall-used-s5 -fcall-used-s6 -fcall-used-s7
INIT_CFLAGS := $(C_BASE_FLAGS) $(PRE_DDR_CALL_USED_FLAGS) -DLOADER_EARLY_DATA_FREE=1
INIT_ASSEMBLE_FLAGS := $(ARCH_FLAGS) \
	-mno-abicalls -fno-pic -G 0 \
	-Wa,-mips32r2 -Wa,--trap
UART_STAGE1_CFLAGS := $(ARCH_FLAGS) $(FREESTANDING_FLAGS) \
	-mno-abicalls -fno-pic -fno-pie -G 0 \
	-std=gnu89 -Wall -Wextra -Werror=implicit-function-declaration \
	-Wno-unused-function -Wno-unused-parameter -Wno-sign-compare \
	-Wno-error=date-time -Werror=attributes \
	-fno-jump-tables -fno-tree-switch-conversion -fno-strict-aliasing \
	-fno-tree-loop-distribute-patterns -fno-tree-loop-vectorize \
	-fno-tree-slp-vectorize
ifeq ($(REFERENCE_GCC47),1)
UART_STAGE1_CFLAGS := $(filter-out -fno-tree-loop-vectorize -Wno-error=date-time -fno-pie,$(UART_STAGE1_CFLAGS))
endif

# The hand-written relocatable dispatcher and data-free C stages share the
# no-ABI model.  All runtime loader references are explicitly base-relative.
ASFLAGS := $(ARCH_FLAGS) $(FREESTANDING_FLAGS) \
	-mno-abicalls -fno-pic -G 0 -D__ASSEMBLY__ -x assembler-with-cpp
LDFLAGS_LOADER := -EL -m elf32ltsmip -G 0 -static -n -nostdlib \
	-T src/loader.lds -Map $(LOADER_MAP)
LDFLAGS_WRAPPER := -EL -m elf32ltsmip -G 0 -static -n -nostdlib \
	-T src/wrapper.lds -Map $(BUILD_DIR)/boot-region.map
LDFLAGS_UART_STAGE1 := -EL -m elf32ltsmip -G 0 -static -n -nostdlib \
	--defsym=UART_STAGE1_ADDRESS=$(UART_STAGE1_ADDRESS) \
	-T src/uart_stage1.lds -Map $(UART_STAGE1_MAP)

LOADER_OBJECTS := $(BUILD_DIR)/head.o $(BUILD_DIR)/init_luton26.o $(BUILD_DIR)/init_jaguar.o
UART_STAGE1_HEAD_DEPS :=
UART_STAGE1_HEAD_CPPFLAGS :=
UART_STAGE1_IMAGE_OUTPUTS :=
UART_STAGE1_OBJECTS :=
UART_STAGE1_MANIFEST_DEPS :=
UART_STAGE1_MANIFEST_ARGS :=
UART_STAGE1_VALIDATE_ARGS :=
ifneq ($(filter 1 y yes true,$(UART_RAMLOADER)),)
UART_STAGE1_HEAD_DEPS := $(UART_STAGE1_BIN)
UART_STAGE1_OBJECTS := $(UART_STAGE1_OBJ) $(UART_RECOVERY_BLOBS_OBJ)
UART_STAGE1_HEAD_CPPFLAGS = -DUART_STAGE1_FILE=\"$(abspath $(UART_STAGE1_BIN))\" -DCONFIG_UART_STAGE1_SIZE=$(shell stat -c %s $(UART_STAGE1_BIN) 2>/dev/null || echo 0)
UART_STAGE1_IMAGE_OUTPUTS := \
	$(OUT_DIR)/uart-stage1-$(VARIANT).elf \
	$(OUT_DIR)/uart-stage1-$(VARIANT).bin \
	$(OUT_DIR)/uart-stage1-$(VARIANT).map \
	$(OUT_DIR)/uart-stage1-$(VARIANT).sym \
	$(OUT_DIR)/uart-stage1-$(VARIANT).dis \
	$(UART_STAGE1_ARTIFACT) $(UART_STAGE1_ARTIFACT).sha256 \
	$(UART_RECOVERY_LUTON26_ARTIFACT) $(UART_RECOVERY_LUTON26_ARTIFACT).sha256 \
	$(UART_RECOVERY_JAGUAR1_ARTIFACT) $(UART_RECOVERY_JAGUAR1_ARTIFACT).sha256
UART_STAGE1_MANIFEST_DEPS := $(UART_STAGE1_BIN) $(UART_STAGE1_REPORT) \
	$(UART_RECOVERY_LUTON26_BIN) $(UART_RECOVERY_JAGUAR1_BIN)
UART_STAGE1_MANIFEST_ARGS := --uart-stage1 $(UART_STAGE1_BIN) \
	--uart-stage1-address $(UART_STAGE1_ADDRESS) \
	--uart-stage1-report $(UART_STAGE1_REPORT) \
	--recovery-luton26 $(UART_RECOVERY_LUTON26_BIN) \
	--recovery-jaguar1 $(UART_RECOVERY_JAGUAR1_BIN)
UART_STAGE1_VALIDATE_ARGS := --uart-stage1 $(UART_STAGE1_BIN)
endif

JAGUAR_STAGE_ENTRIES := \
	init_jaguar_stage_probe,init_jaguar_stage_console,init_jaguar_stage_pll,\
	init_jaguar_stage_spi,init_jaguar_stage_memctl_config,init_jaguar_stage_memctl_wait,init_jaguar_stage_memtrain,\
	init_jaguar_stage_irq,init_jaguar_stage_cache_prepare,init_jaguar_stage_icache,\
	init_jaguar_stage_dcache,init_jaguar_stage_cache_enable,init_jaguar_stage_pi,\
	init_jaguar_stage_board,init_jaguar_stage_finish
LUTON26_STAGE_ENTRIES := \
	init_luton26_stage_probe,init_luton26_stage_console,init_luton26_stage_pll,\
	init_luton26_stage_spi,init_luton26_stage_memctl_config,init_luton26_stage_memctl_wait,init_luton26_stage_memtrain,\
	init_luton26_stage_irq,init_luton26_stage_cache_prepare,init_luton26_stage_icache,\
	init_luton26_stage_dcache,init_luton26_stage_cache_enable,init_luton26_stage_pi,\
	init_luton26_stage_board,init_luton26_stage_finish

.PHONY: all local-all raw image validate inspect variants reference-check check-tools \
	check-config clean distclean deps distrobox refresh-source test test-wrapper-fit payload \
	recovery-payloads local-recovery-payloads uart-smoke-test local-uart-smoke-test help

# `all` is the user-facing entry point. Build natively when the GNU MIPS
# toolchain is installed; otherwise route the same local build target through
# Distrobox. The inner container invokes local-all to avoid recursion.
all:
	@if command -v "$(CC)" >/dev/null 2>&1 && \
	    command -v "$(LD)" >/dev/null 2>&1 && \
	    command -v "$(OBJCOPY)" >/dev/null 2>&1 && \
	    command -v "$(OBJDUMP)" >/dev/null 2>&1 && \
	    command -v "$(NM)" >/dev/null 2>&1 && \
	    command -v "$(READELF)" >/dev/null 2>&1; then \
	  $(MAKE) --no-print-directory local-all; \
	elif command -v distrobox >/dev/null 2>&1; then \
	  echo "GNU MIPS toolchain not found on host; building through Distrobox."; \
	  $(MAKE) --no-print-directory distrobox BUILD_TARGET=local-all; \
	else \
	  echo "GNU MIPS toolchain is not installed and distrobox is unavailable." >&2; \
	  echo "Run 'make deps' on Debian/Ubuntu, or install Distrobox and rerun make." >&2; \
	  exit 2; \
	fi

local-all: check-tools check-config image validate

raw: check-tools check-config $(LOADER_BIN)

image: check-tools $(ARTIFACT) \
       $(OUT_DIR)/loader-$(VARIANT).elf \
       $(OUT_DIR)/loader-$(VARIANT).bin \
       $(OUT_DIR)/loader-$(VARIANT).map \
       $(OUT_DIR)/loader-$(VARIANT).sym \
       $(OUT_DIR)/loader-$(VARIANT).dis \
       $(OUT_DIR)/boot-region-$(VARIANT).elf \
       $(OUT_DIR)/boot-region-$(VARIANT).dis \
       $(UART_STAGE1_IMAGE_OUTPUTS) \
       $(ARTIFACT).sha256 \
       $(ARTIFACT).manifest.json \
       $(ARTIFACT).manifest.json.sha256

check-tools:
	@missing=0; \
	for tool in "$(CC)" "$(LD)" "$(OBJCOPY)" "$(OBJDUMP)" "$(NM)" "$(READELF)" python3 sha256sum stat; do \
	  command -v "$$tool" >/dev/null 2>&1 || { echo "missing required tool: $$tool" >&2; missing=1; }; \
	done; \
	if (( missing )); then \
	  echo "Install the cross-toolchain with 'make deps' or use 'make distrobox'." >&2; \
	  exit 2; \
	fi; \
	version=$$($(CC) -dumpfullversion -dumpversion); major=$${version%%.*}; \
	if [[ -n '$(REQUIRED_GCC_MAJOR)' && $$major != '$(REQUIRED_GCC_MAJOR)' ]]; then \
	  echo "unsupported GNU MIPS compiler $$version; this release requires GCC major $(REQUIRED_GCC_MAJOR)" >&2; \
	  exit 2; \
	fi; \
	echo "toolchain: $$($(CC) --version | head -n1); $$($(LD) --version | head -n1)"

check-config:
	@python3 scripts/check_config.py \
	  --crc-policy '$(CRC_POLICY)' \
	  --size-policy '$(SIZE_POLICY)' \
	  --fallback-region-size '$(FALLBACK_REGION_SIZE)' \
	  --payload-slot-end '$(PAYLOAD_SLOT_END)' \
	  --loader-region-size '$(LOADER_REGION_SIZE)' \
	  --legacy-payload-limit '$(LEGACY_PAYLOAD_LIMIT)' \
	  --hard-payload-limit '$(HARD_PAYLOAD_LIMIT)' \
	  --uart-ramloader '$(UART_RAMLOADER)' \
	  --uart-max-size '$(UART_RAMLOADER_MAX_SIZE)' \
	  --uart-ram-start '$(UART_RAMLOADER_RAM_START)' \
	  --uart-ram-end '$(UART_RAMLOADER_RAM_END)' \
	  --uart-probe-timeout-ms '$(UART_RAMLOADER_PROBE_TIMEOUT_MS)' \
	  --uart-interbyte-timeout-ms '$(UART_RAMLOADER_INTERBYTE_TIMEOUT_MS)' \
	  --uart-menu-timeout-ms '$(UART_MENU_TIMEOUT_MS)' \
	  --uart-count-hz '$(UART_RAMLOADER_COUNT_HZ)' \
	  --uart-stage1-address '$(UART_STAGE1_ADDRESS)' \
	  --uart-stage1-max-size '$(UART_STAGE1_MAX_SIZE)'

$(BUILD_DIR):
	@mkdir -p $@

$(TOOLCHAIN_INFO): | $(BUILD_DIR)
	@{ \
	  echo "compiler=$$($(CC) --version | head -n1)"; \
	  echo "compiler_dumpversion=$$($(CC) -dumpfullversion -dumpversion)"; \
	  echo "linker=$$($(LD) --version | head -n1)"; \
	  echo "c_flags=$(C_BASE_FLAGS)"; \
	  echo "pre_ddr_flags=$(PRE_DDR_CALL_USED_FLAGS)"; \
	  echo "uart_stage1_address=$(UART_STAGE1_ADDRESS)"; \
	  echo "uart_stage1_flags=$(UART_STAGE1_CFLAGS)"; \
	} > $@

$(OUT_DIR):
	@mkdir -p $@

$(ARTIFACT_DIR):
	@mkdir -p $@

$(BUILD_DIR)/head.o: src/head.S $(UART_STAGE1_HEAD_DEPS) | $(BUILD_DIR)
	@echo "  AS      $@ ($(CRC_POLICY)/$(SIZE_POLICY))"
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(ASFLAGS) $(POLICY_CPPFLAGS) \
	  $(UART_STAGE1_HEAD_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/init_luton26.gcc.s: src/init_luton26.c src/init.h | $(BUILD_DIR)
	@echo "  CC-S    $@"
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(INIT_CFLAGS) -S $< -o $@

$(BUILD_DIR)/init_luton26.s: $(BUILD_DIR)/init_luton26.gcc.s scripts/normalize_mips_local_jumps.py
	@echo "  PCREL   $@"
	@python3 scripts/normalize_mips_local_jumps.py --input $< --output $@ \
	  --count-file $(BUILD_DIR)/init_luton26.local-jumps.txt

$(BUILD_DIR)/init_luton26.o: $(BUILD_DIR)/init_luton26.s
	@echo "  AS-C    $@"
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(INIT_ASSEMBLE_FLAGS) -c $< -o $@

$(BUILD_DIR)/init_jaguar.gcc.s: src/init_jaguar.c src/init.h | $(BUILD_DIR)
	@echo "  CC-S    $@"
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(INIT_CFLAGS) -S $< -o $@

$(BUILD_DIR)/init_jaguar.s: $(BUILD_DIR)/init_jaguar.gcc.s scripts/normalize_mips_local_jumps.py
	@echo "  PCREL   $@"
	@python3 scripts/normalize_mips_local_jumps.py --input $< --output $@ \
	  --count-file $(BUILD_DIR)/init_jaguar.local-jumps.txt

$(BUILD_DIR)/init_jaguar.o: $(BUILD_DIR)/init_jaguar.s
	@echo "  AS-C    $@"
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(INIT_ASSEMBLE_FLAGS) -c $< -o $@

$(UART_RECOVERY_STAMP): payloads/uart-firmware-recovery/recovery.c \
  payloads/uart-firmware-recovery/Makefile payloads/uart-firmware-recovery/linker.ld \
  payloads/uart-firmware-recovery/write_descriptor.py include/postmerkos_uart_crypto.h \
  scripts/validate_fixed_payload.py | $(BUILD_DIR)
	@echo "  BUILD   embedded Luton26/Jaguar1 recovery payloads"
	@mkdir -p '$(UART_RECOVERY_DIR)'
	@$(MAKE) --no-print-directory -C payloads/uart-firmware-recovery \
	  CROSS_COMPILE='$(CROSS_COMPILE)' REQUIRED_GCC_MAJOR='$(REQUIRED_GCC_MAJOR)' \
	  REFERENCE_GCC47='$(REFERENCE_GCC47)' \
	  BUILD_DIR='$(abspath $(UART_RECOVERY_BUILD_DIR))' \
	  ARTIFACT_DIR='$(abspath $(UART_RECOVERY_ARTIFACT_DIR))' all
	@test -s '$(UART_RECOVERY_LUTON26_BIN)'
	@test -s '$(UART_RECOVERY_JAGUAR1_BIN)'
	@touch '$@'

$(UART_RECOVERY_LUTON26_BIN) $(UART_RECOVERY_JAGUAR1_BIN): $(UART_RECOVERY_STAMP)
	@test -s '$@'

$(UART_RECOVERY_BLOBS_OBJ): src/uart_recovery_blobs.S \
  $(UART_RECOVERY_LUTON26_BIN) $(UART_RECOVERY_JAGUAR1_BIN) | $(BUILD_DIR)
	@echo "  AS      $@ (embedded recovery data)"
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(ASFLAGS) \
	  -DUART_RECOVERY_LUTON26_FILE=\"$(abspath $(UART_RECOVERY_LUTON26_BIN))\" \
	  -DUART_RECOVERY_JAGUAR1_FILE=\"$(abspath $(UART_RECOVERY_JAGUAR1_BIN))\" \
	  -c $< -o $@

$(UART_STAGE1_OBJ): src/uart_ramloader.c include/postmerkos_uart_crypto.h | $(BUILD_DIR)
	@echo "  CC      $@ (fixed RAM stage-1)"
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(UART_STAGE1_CFLAGS) \
	  $(POLICY_CPPFLAGS) -c $< -o $@

$(UART_STAGE1_ELF): $(UART_STAGE1_OBJECTS) src/uart_stage1.lds scripts/validate_uart_stage1.py
	@echo "  LD      $@ (fixed RAM stage-1 + embedded recovery)"
	@$(LD) $(LDFLAGS_UART_STAGE1) -o $@ $(UART_STAGE1_OBJECTS)
	@python3 scripts/validate_uart_stage1.py --elf $@ \
	  --entry $(UART_STAGE1_ADDRESS) --maximum-size $(UART_STAGE1_MAX_SIZE) \
	  --range-end $(UART_STAGE1_RANGE_END) --objdump $(OBJDUMP) \
	  --readelf $(READELF) --nm $(NM) \
	  --recovery-luton26 $(UART_RECOVERY_LUTON26_BIN) \
	  --recovery-jaguar1 $(UART_RECOVERY_JAGUAR1_BIN) \
	  --report $(UART_STAGE1_REPORT)

$(UART_STAGE1_REPORT): $(UART_STAGE1_ELF)
	@test -s $@

$(UART_STAGE1_MAP): $(UART_STAGE1_ELF)
	@test -s $@

$(UART_STAGE1_SYM): $(UART_STAGE1_ELF)
	@$(NM) -n $< > $@

$(UART_STAGE1_BIN): $(UART_STAGE1_ELF) scripts/pad_binary.py
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) -O binary $< $@
	@python3 scripts/pad_binary.py --alignment 4 $@
	@test $$(stat -c %s $@) -gt 0
	@test $$(stat -c %s $@) -le $$(( $(UART_STAGE1_MAX_SIZE) ))
	@test $$(( $$(stat -c %s $@) % 4 )) -eq 0

$(LOADER_ELF): $(LOADER_OBJECTS) $(TOOLCHAIN_INFO) src/loader.lds scripts/validate_loader_codegen.py
	@echo "  LD      $@"
	@set -e; tmp='$@.tmp'; rm -f "$$tmp"; \
	  trap 'rm -f "$$tmp"' EXIT; \
	  $(LD) $(LDFLAGS_LOADER) -o "$$tmp" $(LOADER_OBJECTS); \
	  python3 scripts/validate_loader_codegen.py \
	    --elf "$$tmp" --objdump '$(OBJDUMP)' --readelf '$(READELF)' --nm '$(NM)' \
	    --report '$(CODEGEN_REPORT)' \
	    --pre-ddr-object '$(BUILD_DIR)/init_luton26.o:$(LUTON26_STAGE_ENTRIES)' \
	    --pre-ddr-object '$(BUILD_DIR)/init_jaguar.o:$(JAGUAR_STAGE_ENTRIES)' \
	    $(foreach object,$(filter-out $(BUILD_DIR)/head.o,$(LOADER_OBJECTS)),--c-object '$(object)'); \
	  mv "$$tmp" '$@'; trap - EXIT

# validate_loader_codegen.py writes this report while validating LOADER_ELF.
# Declare that side effect explicitly so parallel Make can satisfy manifest
# prerequisites without treating the report as an unbuildable standalone file.
$(CODEGEN_REPORT): $(LOADER_ELF)
	@test -s $@ || { echo "missing or empty code-generation report: $@" >&2; exit 1; }

$(LOADER_SYM): $(LOADER_ELF)
	@$(NM) -n $< > $@

$(LOADER_MAP): $(LOADER_ELF)
	@test -f $@

$(LOADER_BIN): $(LOADER_ELF)
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) -O binary $< $@
	@python3 scripts/pad_binary.py --alignment 4 $@
	@test $$(stat -c %s $@) -gt 0
	@test $$(( $$(stat -c %s $@) % 4 )) -eq 0

$(WRAPPER_OBJ): src/boot_wrapper.S $(LOADER_BIN) | $(BUILD_DIR)
	@echo "  AS      $@"
	@len=$$(stat -c %s $(LOADER_BIN)); \
	 $(WRAPPER_FIT_SHELL); \
	 SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(ASFLAGS) \
	   -DACTIVE_OFFSET=$$active -DFALLBACK_OFFSET=$$fallback -DLOADER_LENGTH=$$len \
	   -DLOADER_FILE=\"$(abspath $(LOADER_BIN))\" -c $< -o $@

$(WRAPPER_ELF): $(WRAPPER_OBJ) src/wrapper.lds
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS_WRAPPER) -o $@ $<

$(ARTIFACT): $(WRAPPER_ELF) | $(ARTIFACT_DIR)
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) -O binary -j .boot $< $@
	@test $$(stat -c %s $@) -eq 262144

$(OUT_DIR)/loader-$(VARIANT).elf: $(LOADER_ELF) | $(OUT_DIR)
	@cp -f $< $@

$(OUT_DIR)/loader-$(VARIANT).bin: $(LOADER_BIN) | $(OUT_DIR)
	@cp -f $< $@

$(OUT_DIR)/loader-$(VARIANT).map: $(LOADER_MAP) | $(OUT_DIR)
	@cp -f $< $@

$(OUT_DIR)/loader-$(VARIANT).sym: $(LOADER_SYM) | $(OUT_DIR)
	@cp -f $< $@

$(OUT_DIR)/loader-$(VARIANT).dis: $(LOADER_ELF) | $(OUT_DIR)
	@$(OBJDUMP) -drwC $< > $@

$(OUT_DIR)/boot-region-$(VARIANT).elf: $(WRAPPER_ELF) | $(OUT_DIR)
	@cp -f $< $@

$(OUT_DIR)/boot-region-$(VARIANT).dis: $(WRAPPER_ELF) | $(OUT_DIR)
	@$(OBJDUMP) -drwC $< > $@

$(OUT_DIR)/uart-stage1-$(VARIANT).elf: $(UART_STAGE1_ELF) | $(OUT_DIR)
	@cp -f $< $@

$(OUT_DIR)/uart-stage1-$(VARIANT).bin: $(UART_STAGE1_BIN) | $(OUT_DIR)
	@cp -f $< $@

$(OUT_DIR)/uart-stage1-$(VARIANT).map: $(UART_STAGE1_MAP) | $(OUT_DIR)
	@cp -f $< $@

$(OUT_DIR)/uart-stage1-$(VARIANT).sym: $(UART_STAGE1_SYM) | $(OUT_DIR)
	@cp -f $< $@

$(OUT_DIR)/uart-stage1-$(VARIANT).dis: $(UART_STAGE1_ELF) | $(OUT_DIR)
	@$(OBJDUMP) -drwC $< > $@

$(UART_STAGE1_ARTIFACT): $(UART_STAGE1_BIN) | $(ARTIFACT_DIR)
	@cp -f $< $@

$(UART_STAGE1_ARTIFACT).sha256: $(UART_STAGE1_ARTIFACT)
	@sha256sum $< > $@

$(UART_RECOVERY_LUTON26_ARTIFACT): $(UART_RECOVERY_LUTON26_BIN) | $(ARTIFACT_DIR)
	@cp -f $< $@

$(UART_RECOVERY_JAGUAR1_ARTIFACT): $(UART_RECOVERY_JAGUAR1_BIN) | $(ARTIFACT_DIR)
	@cp -f $< $@

$(UART_RECOVERY_LUTON26_ARTIFACT).sha256: $(UART_RECOVERY_LUTON26_ARTIFACT)
	@sha256sum $< > $@

$(UART_RECOVERY_JAGUAR1_ARTIFACT).sha256: $(UART_RECOVERY_JAGUAR1_ARTIFACT)
	@sha256sum $< > $@

$(ARTIFACT).sha256: $(ARTIFACT)
	@sha256sum $< > $@

$(ARTIFACT).manifest.json: $(ARTIFACT) $(LOADER_BIN) $(TOOLCHAIN_INFO) $(CODEGEN_REPORT) $(UART_STAGE1_MANIFEST_DEPS)
	@python3 scripts/write_manifest.py \
	  --variant '$(VARIANT)' --crc-policy '$(CRC_POLICY)' --size-policy '$(SIZE_POLICY)' \
	  --fallback-region-size '$(FALLBACK_REGION_SIZE)' --payload-slot-end '$(PAYLOAD_SLOT_END)' --legacy-payload-limit '$(LEGACY_PAYLOAD_LIMIT)' \
	  --hard-payload-limit '$(HARD_PAYLOAD_LIMIT)' \
	  --uart-ramloader '$(UART_RAMLOADER)' \
	  --uart-protocol-version 2 \
	  --uart-max-size '$(UART_RAMLOADER_MAX_SIZE)' \
	  --uart-ram-start '$(UART_RAMLOADER_RAM_START)' \
	  --uart-ram-end '$(UART_RAMLOADER_RAM_END)' \
	  --uart-probe-timeout-ms '$(UART_RAMLOADER_PROBE_TIMEOUT_MS)' \
	  --uart-interbyte-timeout-ms '$(UART_RAMLOADER_INTERBYTE_TIMEOUT_MS)' \
	  --uart-menu-timeout-ms '$(UART_MENU_TIMEOUT_MS)' \
	  --loader $(LOADER_BIN) --image $(ARTIFACT) \
	  --toolchain-info $(TOOLCHAIN_INFO) --codegen-report $(CODEGEN_REPORT) \
	  $(UART_STAGE1_MANIFEST_ARGS) --output $@

$(ARTIFACT).manifest.json.sha256: $(ARTIFACT).manifest.json
	@sha256sum $< > $@

validate: check-tools check-config $(ARTIFACT) $(LOADER_BIN) $(LOADER_SYM)
	@python3 scripts/validate_image.py \
	  --variant '$(VARIANT)' --crc-policy '$(CRC_POLICY)' --size-policy '$(SIZE_POLICY)' \
	  --loader $(LOADER_BIN) --image $(ARTIFACT) --symbols $(LOADER_SYM) \
	  $(UART_STAGE1_VALIDATE_ARGS)

inspect: $(ARTIFACT) $(LOADER_BIN) $(LOADER_SYM)
	@python3 scripts/inspect_image.py --loader $(LOADER_BIN) --image $(ARTIFACT) --symbols $(LOADER_SYM)

variants:
	@for variant in strict development permissive; do \
	  $(MAKE) --no-print-directory VARIANT=$$variant all || exit $$?; \
	done

payload:
	@test -n "$(KERNEL)" || { echo 'usage: make payload KERNEL=/path/vmlinuz.bin LOAD_ADDRESS=0x81000000 ENTRY_POINT=0x81000000 [PAYLOAD_OUTPUT=...]' >&2; exit 2; }
	@test -n "$(LOAD_ADDRESS)" || { echo 'LOAD_ADDRESS is required' >&2; exit 2; }
	@test -n "$(ENTRY_POINT)" || { echo 'ENTRY_POINT is required' >&2; exit 2; }
	@mkdir -p $(ARTIFACT_DIR)
	@out='$(PAYLOAD_OUTPUT)'; \
	 if [[ -z $$out ]]; then base=$$(basename '$(KERNEL)'); base=$${base%.*}; out='$(ARTIFACT_DIR)'/$$base.vcoreiii-payload.bin; fi; \
	 python3 tools/mkvcoreiii_payload.py pack --input '$(KERNEL)' --output "$$out" \
	   --load-address '$(LOAD_ADDRESS)' --entry-point '$(ENTRY_POINT)' \
	   --max-payload-size '$(HARD_PAYLOAD_LIMIT)' --metadata "$$out.json"; \
	 python3 tools/mkvcoreiii_payload.py verify "$$out" --max-payload-size '$(HARD_PAYLOAD_LIMIT)'

recovery-payloads:
	@if command -v "$(CC)" >/dev/null 2>&1 && command -v "$(LD)" >/dev/null 2>&1; then \
	  $(MAKE) --no-print-directory local-recovery-payloads; \
	elif command -v distrobox >/dev/null 2>&1; then \
	  echo "GNU MIPS toolchain not found on host; building recovery payloads through Distrobox."; \
	  $(MAKE) --no-print-directory distrobox BUILD_TARGET=local-recovery-payloads; \
	else \
	  echo "GNU MIPS toolchain is not installed and distrobox is unavailable." >&2; \
	  exit 2; \
	fi

local-recovery-payloads: check-tools
	@$(MAKE) -C payloads/uart-firmware-recovery CROSS_COMPILE='$(CROSS_COMPILE)' all

uart-smoke-test:
	@if command -v "$(CC)" >/dev/null 2>&1 && command -v "$(LD)" >/dev/null 2>&1; then \
	  $(MAKE) --no-print-directory local-uart-smoke-test; \
	elif command -v distrobox >/dev/null 2>&1; then \
	  echo "GNU MIPS toolchain not found on host; building UART smoke test through Distrobox."; \
	  $(MAKE) --no-print-directory distrobox BUILD_TARGET=local-uart-smoke-test; \
	else \
	  echo "GNU MIPS toolchain is not installed and distrobox is unavailable." >&2; \
	  exit 2; \
	fi

local-uart-smoke-test: check-tools
	@$(MAKE) -C payloads/uart-smoke-test CROSS_COMPILE='$(CROSS_COMPILE)' all

reference-check:
	@test -n "$(REFERENCE_IMAGE)" || { echo 'usage: make reference-check REFERENCE_IMAGE=/path/redboot-nocrc-sz.bin' >&2; exit 2; }
	@python3 scripts/analyze_reference.py "$(REFERENCE_IMAGE)"

refresh-source:
	@test -n "$(GPL_ARCHIVE)" || { echo 'usage: make refresh-source GPL_ARCHIVE=/path/MS42-GPL-sources-3-18-122-master.zip' >&2; exit 2; }
	@./scripts/refresh-from-gpl.sh "$(GPL_ARCHIVE)"

deps:
	@./scripts/install-deps.sh

distrobox:
	@./scripts/distrobox-build.sh "$(BUILD_TARGET)" \
	  "VARIANT=$(VARIANT)" "CRC_POLICY=$(CRC_POLICY)" "SIZE_POLICY=$(SIZE_POLICY)" \
	  "FALLBACK_REGION_SIZE=$(FALLBACK_REGION_SIZE)" "PAYLOAD_SLOT_END=$(PAYLOAD_SLOT_END)" \
	  "LEGACY_PAYLOAD_LIMIT=$(LEGACY_PAYLOAD_LIMIT)" \
	  "HARD_PAYLOAD_LIMIT=$(HARD_PAYLOAD_LIMIT)" \
	  "UART_RAMLOADER=$(UART_RAMLOADER)" \
	  "UART_RAMLOADER_MAX_SIZE=$(UART_RAMLOADER_MAX_SIZE)" \
	  "UART_RAMLOADER_RAM_START=$(UART_RAMLOADER_RAM_START)" \
	  "UART_RAMLOADER_RAM_END=$(UART_RAMLOADER_RAM_END)" \
	  "UART_RAMLOADER_PROBE_TIMEOUT_MS=$(UART_RAMLOADER_PROBE_TIMEOUT_MS)" \
	  "UART_RAMLOADER_INTERBYTE_TIMEOUT_MS=$(UART_RAMLOADER_INTERBYTE_TIMEOUT_MS)" \
	  "UART_MENU_TIMEOUT_MS=$(UART_MENU_TIMEOUT_MS)" \
	  "UART_RAMLOADER_COUNT_HZ=$(UART_RAMLOADER_COUNT_HZ)" \
	  "UART_STAGE1_ADDRESS=$(UART_STAGE1_ADDRESS)" \
	  "UART_STAGE1_MAX_SIZE=$(UART_STAGE1_MAX_SIZE)"

test-wrapper-fit:
	@len=21080; $(WRAPPER_FIT_SHELL); test $$active -eq $$((0x40000 - 2 * len))

test:
	@python3 -m unittest discover -s tests -p 'test_*.py' -v
	@./scripts/test-codegen-validators.sh
	@./scripts/structural-test-clang.sh
	@./scripts/structural-test-recovery-clang.sh

clean:
	@rm -rf build out artifacts

distclean: clean
	@rm -f SOURCE-PROVENANCE.txt

help:
	@printf '%s\n' \
	  'Standalone VCore-III LinuxLoader (Meraki RedBoot) builder' \
	  '' \
	  'Named profiles:' \
	  '  make all VARIANT=strict       CRC strict; legacy size strict; hard slot limit' \
	  '  make all VARIANT=development  CRC warn; legacy size warn; hard slot limit (default)' \
	  '  make all VARIANT=permissive   CRC off/copy-only; hard slot limit only' \
	  '' \
	  'Custom policies:' \
	  '  make all VARIANT=custom CRC_POLICY=warn SIZE_POLICY=hard-only' \
	  '  CRC_POLICY: strict | warn | off' \
	  '  SIZE_POLICY: legacy-strict | legacy-warn | hard-only' \
	  '  PAYLOAD_SLOT_END: first byte after the kernel payload slot' \
	  '  UART_STAGE1_ADDRESS: fixed post-DDR UART engine address (default 0xa7f00000)' \
	  '' \
	  'Payload packaging:' \
	  '  make payload KERNEL=vmlinuz.bin LOAD_ADDRESS=0x81000000 ENTRY_POINT=0x81000000' \
	  '' \
	  'Other targets:' \
	  '  make variants                    Build all three named profiles' \
	  '  make distrobox                   Build default profile in Ubuntu 22.04 Distrobox' \
	  '  make inspect                     Print compiled loader/wrapper layout' \
	  '  make refresh-source GPL_ARCHIVE=/path/MS42-GPL-sources-3-18-122-master.zip' \
	  '  make recovery-payloads           Build Luton26 and Jaguar1 recovery payloads' \
	  '  make uart-smoke-test             Build non-destructive PMOSRAM execution test' \
	  '  make test                        Run host tests and Clang structural builds' \
	  '' \
	  'Flashable output:' \
	  '  artifacts/vcoreiii-linuxloader-<variant>.bin' \
	  '  artifacts/vcoreiii-uart-stage1-<variant>.bin (UART-enabled profiles)'
