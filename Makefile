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
UART_RAMLOADER_COUNT_HZ ?= 208000000

VALID_CRC_POLICIES := strict warn off
VALID_SIZE_POLICIES := legacy-strict legacy-warn hard-only
ifeq ($(filter $(CRC_POLICY),$(VALID_CRC_POLICIES)),)
$(error CRC_POLICY must be one of: $(VALID_CRC_POLICIES); named profiles are strict, development, permissive)
endif
ifeq ($(filter $(SIZE_POLICY),$(VALID_SIZE_POLICIES)),)
$(error SIZE_POLICY must be one of: $(VALID_SIZE_POLICIES); named profiles are strict, development, permissive)
endif

WORK_ROOT ?= $(CURDIR)/.work
export POSTMERKOS_WORK_ROOT := $(WORK_ROOT)
TOOLCHAIN_ROOT ?= $(shell POSTMERKOS_WORK_ROOT='$(WORK_ROOT)' ./scripts/toolchain-env.sh --print-root 2>/dev/null)
CROSS_COMPILE ?= $(TOOLCHAIN_ROOT)/bin/mipsel-linux-gnu-
JOBS ?= $(shell nproc 2>/dev/null || echo 1)
SOURCE_DATE_EPOCH ?= 1605204110
BUILD_TARGET ?= __all-local
BUILD_MODE ?= prompt
BUILD_CONTEXT ?= direct

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

BUILD_DIR := $(WORK_ROOT)/build/$(VARIANT)
OUT_DIR := $(WORK_ROOT)/out/$(VARIANT)
ARTIFACT_DIR := $(WORK_ROOT)/artifacts
LOG_DIR := $(WORK_ROOT)/logs
SUPPORT_DIR := $(WORK_ROOT)/support
ARTIFACT := $(ARTIFACT_DIR)/vcoreiii-linuxloader-$(VARIANT).bin
LOADER_ELF := $(BUILD_DIR)/loader.elf
LOADER_BIN := $(BUILD_DIR)/loader.bin
LOADER_MAP := $(BUILD_DIR)/loader.map
LOADER_SYM := $(BUILD_DIR)/loader.sym
WRAPPER_ELF := $(BUILD_DIR)/boot-region.elf
WRAPPER_OBJ := $(BUILD_DIR)/boot_wrapper.o
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
	$(if $(filter 1 y yes true,$(UART_RAMLOADER)),-DCONFIG_UART_RAMLOADER=1,) \
	-DCONFIG_UART_RAMLOADER_MAX_SIZE=$(UART_RAMLOADER_MAX_SIZE) \
	-DCONFIG_UART_RAMLOADER_RAM_START=$(UART_RAMLOADER_RAM_START) \
	-DCONFIG_UART_RAMLOADER_RAM_END=$(UART_RAMLOADER_RAM_END) \
	-DCONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS=$(UART_RAMLOADER_PROBE_TIMEOUT_MS) \
	-DCONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS=$(UART_RAMLOADER_INTERBYTE_TIMEOUT_MS) \
	-DCONFIG_UART_RAMLOADER_COUNT_HZ=$(UART_RAMLOADER_COUNT_HZ)

# The reset-time source was written for GCC 4.7's embedded MIPS PIC model:
# -fPIC with -mno-abicalls and a large GP data window. Current distro GCC
# releases reject or materially change that model, so this build does not use
# the host distribution's cross compiler. Release builds use the source-built,
# checksum-pinned GCC 4.7.3/binutils 2.23.2 toolchain installed by the helpers.
ARCH_FLAGS := -EL -mabi=32 -march=mips32r2 -msoft-float
FREESTANDING_FLAGS := \
	-ffreestanding -fno-builtin -fno-common \
	-fno-stack-protector -fomit-frame-pointer \
	-Os -pipe -nostdinc -I$(CURDIR)/include -I$(CURDIR)/src \
	-Wa,-mips32r2 -Wa,--trap

CFLAGS := $(ARCH_FLAGS) $(FREESTANDING_FLAGS) \
	-mno-abicalls -fPIC -G 65535 \
	-std=gnu89 -Wall -Wextra -Werror=implicit-function-declaration \
	-Wno-unused-function -Wno-unused-parameter -Wno-sign-compare

# Reset-time C runs before writable RAM or a stack exists.  The complete
# initialization call graph is forced inline in src/init*.c; these options
# stop GCC from emitting ABI save/restore traffic for s0-s7 in the resulting
# leaf entry point.  They are deliberately limited to the two pre-DDR objects.
PRE_DDR_CALL_USED_FLAGS := \
	-fcall-used-s0 -fcall-used-s1 -fcall-used-s2 -fcall-used-s3 \
	-fcall-used-s4 -fcall-used-s5 -fcall-used-s6 -fcall-used-s7
PRE_DDR_CFLAGS := $(CFLAGS) $(PRE_DDR_CALL_USED_FLAGS) -finline-functions -finline-limit=100000

ASFLAGS := $(ARCH_FLAGS) $(FREESTANDING_FLAGS) \
	-mno-abicalls -fno-pic -G 0 -D__ASSEMBLY__ -x assembler-with-cpp
LDFLAGS_LOADER := -EL -m elf32ltsmip -G 0 -static -n -nostdlib \
	-T src/loader.lds -Map $(LOADER_MAP)
LDFLAGS_WRAPPER := -EL -m elf32ltsmip -G 0 -static -n -nostdlib \
	-T src/wrapper.lds -Map $(BUILD_DIR)/boot-region.map

LOADER_OBJECTS := $(BUILD_DIR)/head.o $(BUILD_DIR)/init_luton26.o $(BUILD_DIR)/init_jaguar.o
ifneq ($(filter 1 y yes true,$(UART_RAMLOADER)),)
LOADER_OBJECTS += $(BUILD_DIR)/uart_ramloader.o
endif

.PHONY: all __all-local __loader-local raw image validate inspect variants reference-check check-tools \
	check-toolchain check-config clean distclean deps toolchain distrobox refresh-source test test-wrapper-fit payload recovery-payloads support-bundle work-layout help

# `make all` is the user-facing dispatcher. It prompts once on an interactive
# terminal and then invokes the internal target either natively or in Distrobox.
all:
	+@BUILD_MODE='$(BUILD_MODE)' MAKE_COMMAND='$(MAKE)' JOBS='$(JOBS)' \
	  ./scripts/build-dispatch.sh __all-local \
	  "VARIANT=$(VARIANT)" "CRC_POLICY=$(CRC_POLICY)" "SIZE_POLICY=$(SIZE_POLICY)" \
	  "FALLBACK_REGION_SIZE=$(FALLBACK_REGION_SIZE)" "PAYLOAD_SLOT_END=$(PAYLOAD_SLOT_END)" \
	  "LEGACY_PAYLOAD_LIMIT=$(LEGACY_PAYLOAD_LIMIT)" "HARD_PAYLOAD_LIMIT=$(HARD_PAYLOAD_LIMIT)" \
	  "UART_RAMLOADER=$(UART_RAMLOADER)" "UART_RAMLOADER_MAX_SIZE=$(UART_RAMLOADER_MAX_SIZE)" \
	  "UART_RAMLOADER_RAM_START=$(UART_RAMLOADER_RAM_START)" "UART_RAMLOADER_RAM_END=$(UART_RAMLOADER_RAM_END)" \
	  "UART_RAMLOADER_PROBE_TIMEOUT_MS=$(UART_RAMLOADER_PROBE_TIMEOUT_MS)" \
	  "UART_RAMLOADER_INTERBYTE_TIMEOUT_MS=$(UART_RAMLOADER_INTERBYTE_TIMEOUT_MS)" \
	  "UART_RAMLOADER_COUNT_HZ=$(UART_RAMLOADER_COUNT_HZ)" \
	  "SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH)" "WORK_ROOT=$(WORK_ROOT)"

__loader-local: check-tools check-toolchain check-config image validate

# A successful default build produces the boot region and both RAM recovery tools.
__all-local: __loader-local recovery-payloads

raw: check-tools check-toolchain check-config $(LOADER_BIN)

image: check-tools check-toolchain $(ARTIFACT) \
       $(OUT_DIR)/loader-$(VARIANT).elf \
       $(OUT_DIR)/loader-$(VARIANT).bin \
       $(OUT_DIR)/loader-$(VARIANT).map \
       $(OUT_DIR)/loader-$(VARIANT).sym \
       $(OUT_DIR)/loader-$(VARIANT).dis \
       $(OUT_DIR)/boot-region-$(VARIANT).elf \
       $(OUT_DIR)/boot-region-$(VARIANT).dis \
       $(ARTIFACT).sha256 \
       $(ARTIFACT).manifest.json \
       $(ARTIFACT).manifest.json.sha256

check-tools:
	@missing=0; \
	for tool in "$(CC)" "$(LD)" "$(OBJCOPY)" "$(OBJDUMP)" "$(NM)" "$(READELF)" python3 sha256sum stat; do \
	  command -v "$$tool" >/dev/null 2>&1 || { echo "missing required tool: $$tool" >&2; missing=1; }; \
	done; \
	test $$missing -eq 0

check-toolchain:
	@ALLOW_UNVERIFIED_TOOLCHAIN='$(ALLOW_UNVERIFIED_TOOLCHAIN)' ./scripts/check-toolchain.sh '$(CROSS_COMPILE)'

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
	  --uart-count-hz '$(UART_RAMLOADER_COUNT_HZ)'

$(BUILD_DIR) $(OUT_DIR) $(ARTIFACT_DIR) $(LOG_DIR) $(SUPPORT_DIR):
	@mkdir -p $@

$(BUILD_DIR)/head.o: src/head.S | $(BUILD_DIR)
	@echo "  AS      $@ ($(CRC_POLICY)/$(SIZE_POLICY))"
	@printf '%s\n' "$(CC) $(ASFLAGS) $(POLICY_CPPFLAGS) -c $< -o $@" > $@.cmd
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(ASFLAGS) $(POLICY_CPPFLAGS) -c $< -o $@
	@$(OBJDUMP) -drwC $@ > $@.dis
	@$(READELF) -rW $@ > $@.relocs

$(BUILD_DIR)/init_luton26.o: src/init_luton26.c src/init.h include/asm/mipsregs.h | $(BUILD_DIR)
	@echo "  CC      $@"
	@printf '%s\n' "$(CC) $(PRE_DDR_CFLAGS) -c $< -o $@" > $@.cmd
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(PRE_DDR_CFLAGS) -S $< -o $@.s
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(PRE_DDR_CFLAGS) -c $< -o $@
	@$(OBJDUMP) -drwC $@ > $@.dis
	@$(READELF) -rW $@ > $@.relocs

$(BUILD_DIR)/init_jaguar.o: src/init_jaguar.c src/init.h include/asm/mipsregs.h | $(BUILD_DIR)
	@echo "  CC      $@"
	@printf '%s\n' "$(CC) $(PRE_DDR_CFLAGS) -c $< -o $@" > $@.cmd
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(PRE_DDR_CFLAGS) -S $< -o $@.s
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(PRE_DDR_CFLAGS) -c $< -o $@
	@$(OBJDUMP) -drwC $@ > $@.dis
	@$(READELF) -rW $@ > $@.relocs

$(BUILD_DIR)/uart_ramloader.o: src/uart_ramloader.c | $(BUILD_DIR)
	@echo "  CC      $@"
	@printf '%s\n' "$(CC) $(CFLAGS) $(POLICY_CPPFLAGS) -c $< -o $@" > $@.cmd
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(CFLAGS) $(POLICY_CPPFLAGS) -S $< -o $@.s
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CC) $(CFLAGS) $(POLICY_CPPFLAGS) -c $< -o $@
	@$(OBJDUMP) -drwC $@ > $@.dis
	@$(READELF) -rW $@ > $@.relocs

$(LOADER_ELF): $(LOADER_OBJECTS) src/loader.lds scripts/validate_loader_codegen.py | $(LOG_DIR)
	@echo "  LD      $@"
	@set -o pipefail; tmp='$@.tmp'; rm -f "$$tmp"; \
	  $(LD) $(LDFLAGS_LOADER) -o "$$tmp" $(LOADER_OBJECTS); \
	  $(OBJDUMP) -drwC "$$tmp" > '$@.tmp.dis'; \
	  $(READELF) -aW "$$tmp" > '$@.tmp.readelf'; \
	  python3 scripts/validate_loader_codegen.py \
	    --elf "$$tmp" --objdump '$(OBJDUMP)' --readelf '$(READELF)' --nm '$(NM)' \
	    --pre-ddr-object '$(BUILD_DIR)/init_luton26.o' \
	    --pre-ddr-object '$(BUILD_DIR)/init_jaguar.o' 2>&1 | tee '$(LOG_DIR)/loader-codegen-validation-$(VARIANT).log'; \
	  rc=$${PIPESTATUS[0]}; \
	  if [[ $$rc -ne 0 ]]; then echo "Validation failed; share $(WORK_ROOT) or run 'make support-bundle'." >&2; exit $$rc; fi; \
	  mv "$$tmp" '$@'; mv '$@.tmp.dis' '$@.dis'; mv '$@.tmp.readelf' '$@.readelf'

$(LOADER_SYM): $(LOADER_ELF)
	@$(NM) -n $< > $@

$(LOADER_MAP): $(LOADER_ELF)
	@test -f $@

$(LOADER_BIN): $(LOADER_ELF)
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) -O binary $< $@
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

$(ARTIFACT).sha256: $(ARTIFACT)
	@sha256sum $< > $@

$(ARTIFACT).manifest.json: $(ARTIFACT) $(LOADER_BIN)
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
	  --compiler '$(CC)' --linker '$(LD)' \
	  --toolchain-id "$$(./scripts/toolchain-env.sh --print-id)" \
	  --loader $(LOADER_BIN) --image $(ARTIFACT) --output $@

$(ARTIFACT).manifest.json.sha256: $(ARTIFACT).manifest.json
	@sha256sum $< > $@

validate: check-config $(ARTIFACT) $(LOADER_BIN) $(LOADER_SYM)
	@python3 scripts/validate_image.py \
	  --variant '$(VARIANT)' --crc-policy '$(CRC_POLICY)' --size-policy '$(SIZE_POLICY)' \
	  --loader $(LOADER_BIN) --image $(ARTIFACT) --symbols $(LOADER_SYM)

inspect: $(ARTIFACT) $(LOADER_BIN) $(LOADER_SYM)
	@python3 scripts/inspect_image.py --loader $(LOADER_BIN) --image $(ARTIFACT) --symbols $(LOADER_SYM)

variants:
	@for variant in strict development permissive; do \
	  $(MAKE) --no-print-directory VARIANT=$$variant __loader-local || exit $$?; \
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

recovery-payloads: check-tools check-toolchain
	@$(MAKE) -C payloads/uart-firmware-recovery CROSS_COMPILE='$(CROSS_COMPILE)' \
	  BUILD_DIR='$(WORK_ROOT)/recovery/build' ARTIFACT_DIR='$(WORK_ROOT)/recovery/artifacts' all

work-layout:
	@echo "Work root:      $(WORK_ROOT)"
	@echo "Toolchain:     $(TOOLCHAIN_ROOT)"
	@echo "Objects:       $(BUILD_DIR)"
	@echo "Inspection:    $(OUT_DIR)"
	@echo "Artifacts:     $(ARTIFACT_DIR)"
	@echo "Recovery:      $(WORK_ROOT)/recovery"
	@echo "Logs:          $(LOG_DIR)"

support-bundle: | $(SUPPORT_DIR)
	@POSTMERKOS_WORK_ROOT='$(WORK_ROOT)' ./scripts/create-support-bundle.sh '$(SUPPORT_DIR)'

reference-check:
	@test -n "$(REFERENCE_IMAGE)" || { echo 'usage: make reference-check REFERENCE_IMAGE=/path/redboot-nocrc-sz.bin' >&2; exit 2; }
	@python3 scripts/analyze_reference.py "$(REFERENCE_IMAGE)"

refresh-source:
	@test -n "$(GPL_ARCHIVE)" || { echo 'usage: make refresh-source GPL_ARCHIVE=/path/MS42-GPL-sources-3-18-122-master.zip' >&2; exit 2; }
	@./scripts/refresh-from-gpl.sh "$(GPL_ARCHIVE)"

deps:
	@./scripts/install-deps.sh

toolchain:
	@./scripts/install-legacy-toolchain.sh

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
	  "UART_RAMLOADER_COUNT_HZ=$(UART_RAMLOADER_COUNT_HZ)" \
	  "SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH)" "WORK_ROOT=$(WORK_ROOT)"

test-wrapper-fit:
	@len=21080; $(WRAPPER_FIT_SHELL); test $$active -eq $$((0x40000 - 2 * len))

test:
	@python3 -m unittest discover -s tests -p 'test_*.py' -v
	@./scripts/structural-test-clang.sh
	@./scripts/structural-test-recovery-clang.sh

clean:
	@rm -rf '$(WORK_ROOT)/build' '$(WORK_ROOT)/out' '$(WORK_ROOT)/artifacts' '$(WORK_ROOT)/recovery' '$(WORK_ROOT)/logs' '$(WORK_ROOT)/support'

distclean:
	@rm -rf '$(WORK_ROOT)'

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
	  '' \
	  'Payload packaging:' \
	  '  make payload KERNEL=vmlinuz.bin LOAD_ADDRESS=0x81000000 ENTRY_POINT=0x81000000' \
	  '' \
	  'Other targets:' \
	  '  make variants                    Build all three named profiles' \
	  '  make all                         Ask whether to use Distrobox; build loader + recovery tools' \
	  '  make all BUILD_MODE=distrobox    Noninteractive Distrobox build' \
	  '  make all BUILD_MODE=native       Noninteractive native build' \
	  '  make distrobox                   Backward-compatible explicit Distrobox build' \
	  '  make inspect                     Print compiled loader/wrapper layout' \
	  '  make refresh-source GPL_ARCHIVE=/path/MS42-GPL-sources-3-18-122-master.zip' \
	  '  make recovery-payloads           Build Luton26 and Jaguar1 recovery payloads' \
	  '  make work-layout                Print source-local .work paths' \
	  '  make support-bundle             Package objects, disassemblies and logs' \
	  '  make test                        Run host tests and Clang structural builds' \
	  '' \
	  'Generated work tree:' \
	  '  .work/toolchains/               Pinned compiler and binutils' \
	  '  .work/build/<variant>/          Objects, assembly, relocations and ELF files' \
	  '  .work/out/<variant>/            Inspection copies and disassemblies' \
	  '  .work/artifacts/                Flashable boot region and manifests' \
	  '  .work/recovery/artifacts/       UART recovery payloads' \
	  '  .work/logs/                     Build and validation logs'
