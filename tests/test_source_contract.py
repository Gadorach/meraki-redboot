from __future__ import annotations

import json
import re
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def loader_crc_reference(data: bytes) -> int:
    """Mirror head.S: init 0xffffffff, reflected polynomial, final complement."""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return (~crc) & 0xFFFFFFFF


class SourceContractTests(unittest.TestCase):
    def test_policy_driven_source_paths_exist(self) -> None:
        head = (ROOT / "src/head.S").read_text()
        for token in (
            "CONFIG_CRC_POLICY_STRICT",
            "CONFIG_CRC_POLICY_WARN",
            "CONFIG_CRC_POLICY_OFF",
            "CONFIG_SIZE_POLICY_LEGACY_STRICT",
            "CONFIG_SIZE_POLICY_LEGACY_WARN",
            "CONFIG_SIZE_POLICY_HARD_ONLY",
            "CONFIG_PAYLOAD_SLOT_END",
            "loader_hard_size_reject_branch",
            "loader_crc_policy_off",
        ):
            self.assertIn(token, head)
        self.assertNotIn("CONFIG_BOOT_ROM_LOADER_NO_CRC", head)
        self.assertNotIn("CONFIG_BOOT_ROM_LOADER_NO_SIZE_CHECK", head)

    def test_crc_off_is_copy_only_and_table_is_conditional(self) -> None:
        head = (ROOT / "src/head.S").read_text()
        self.assertIn("CRC-off copy-only loop", head)
        self.assertRegex(
            head,
            r"#if !defined\(CONFIG_CRC_POLICY_OFF\)\s*\ncrctab:",
        )
        self.assertIn("CRC off: skip the header and copy only", head)

    def test_mandatory_hard_boundary_is_separate_from_legacy_policy(self) -> None:
        head = (ROOT / "src/head.S").read_text()
        self.assertIn("HARD_PAYLOAD_LIMIT", head)
        self.assertIn("LEGACY_PAYLOAD_LIMIT", head)
        self.assertLess(
            head.index("loader_hard_size_reject_branch"),
            head.index("loader_size_policy_legacy_strict"),
        )

    def test_warning_uart_is_source_owned(self) -> None:
        head = (ROOT / "src/head.S").read_text()
        self.assertIn("loader_uart_puts", head)
        self.assertIn("payload CRC mismatch", head)
        self.assertIn("payload exceeds legacy size threshold", head)

    def test_wrapper_embeds_only_current_build(self) -> None:
        wrapper = (ROOT / "src/boot_wrapper.S").read_text()
        self.assertEqual(wrapper.count(".incbin LOADER_FILE"), 2)
        self.assertNotIn("redboot-nocrc.bin", wrapper)
        self.assertNotIn("redboot-nocrc-sz.bin", wrapper)

    def test_vtss_include_closure_is_present(self) -> None:
        include_dir = ROOT / "include/vtss"
        for path in include_dir.glob("*.h"):
            for name in re.findall(r'^#include\s+"([^"]+)"', path.read_text(), re.M):
                self.assertTrue((include_dir / name).is_file(), f"{path.name} needs {name}")

    def test_default_profile_is_development(self) -> None:
        makefile = (ROOT / "Makefile").read_text()
        self.assertIn("VARIANT ?= development", makefile)
        self.assertIn("CRC_POLICY_development := warn", makefile)
        self.assertIn("SIZE_POLICY_development := legacy-warn", makefile)
        self.assertIn("CRC_POLICY_permissive := off", makefile)
        self.assertIn("SIZE_POLICY_permissive := hard-only", makefile)

    def test_gnu_loader_uses_data_free_runtime_relative_stages(self) -> None:
        makefile = (ROOT / "Makefile").read_text()
        linker = (ROOT / "src/loader.lds").read_text()
        head = (ROOT / "src/head.S").read_text()
        validator = (ROOT / "scripts/validate_loader_codegen.py").read_text()
        normalizer = (ROOT / "scripts/normalize_mips_local_jumps.py").read_text()
        self.assertIn("-mno-abicalls -fno-pic -fno-pie -G 0", makefile)
        self.assertIn("PRE_DDR_CALL_USED_FLAGS", makefile)
        self.assertIn("-fcall-used-s7", makefile)
        self.assertNotIn("-fcall-used-fp", makefile)
        self.assertIn("normalize_mips_local_jumps.py", makefile)
        self.assertIn("loader_stage_call", head)
        self.assertIn("addu\tt9, t9, gp", head)
        self.assertNotIn("loader_pic_call", head)
        self.assertIn(".forbidden_loader_data", linker)
        self.assertIn("relocatable flash loader C must not allocate data, literals, or a GOT", linker)
        self.assertIn("pre-DDR C must be leaf-only", validator)
        self.assertIn("data-free, call-free, stackless, and GOT-free", validator)
        self.assertIn("LOCAL_JUMP_RE", normalizer)
        self.assertIn("REQUIRED_GCC_MAJOR ?= 10", makefile)

    def test_cp0_tlb_primitives_are_statement_macros(self) -> None:
        mipsregs = (ROOT / "include/asm/mipsregs.h").read_text()
        self.assertIn("#define mtc0_tlbw_hazard() do", mipsregs)
        self.assertIn("#define tlb_write_indexed() do", mipsregs)
        self.assertIn('"ehb"', mipsregs)
        self.assertIn('"tlbwi\\n\\t"', mipsregs)
        self.assertNotRegex(mipsregs, r"static inline void\s+(?:mtc0_tlbw_hazard|tlb_write_indexed)")

    def test_gcc10_pre_ddr_helpers_are_force_inlined_and_data_free(self) -> None:
        init_h = (ROOT / "src/init.h").read_text()
        jaguar = (ROOT / "src/init_jaguar.c").read_text()
        luton = (ROOT / "src/init_luton26.c").read_text()
        combined = "\n".join((init_h, jaguar, luton))

        self.assertIn("#define LOADER_ALWAYS_INLINE", combined)
        self.assertIn("#define LOADER_STAGE_ENTRY", combined)
        self.assertNotIn("__func__", combined)
        self.assertNotIn('uart_puts("', combined)
        self.assertNotIn("} __attribute__((always_inline))", combined)
        self.assertNotRegex(combined, r"(?m)^\s*static inline\s")
        self.assertNotRegex(combined, r"(?m)^\s*(?:volatile\s+)?register\s")
        self.assertIn("movz   %[speed]", init_h)
        self.assertNotIn("switch (divider", init_h)

        for helper in (
            "clock_speed_khz", "init_uart", "uart_puts", "init_memctl",
            "wait_memctl", "lookfor_and_incr", "train_bytelane",
            "test_memory", "init_memory_controller_config_stage",
            "wait_memory_controller_stage", "train_memory_stage",
            "set_tlb_entry", "create_tlb", "init_tlb", "init_pll",
            "init_spi", "init_cache_prepare_stage", "init_icache_stage",
            "init_dcache_stage", "init_cache_enable_stage", "read_mii",
            "write_mii",
        ):
            self.assertRegex(init_h, rf"LOADER_ALWAYS_INLINE\s+[\w\s\*]+\n{helper}\s*\(", helper)

        self.assertIn("DEFINE_LOADER_INIT_STAGES(init_jaguar)", jaguar)
        self.assertIn("DEFINE_LOADER_INIT_STAGES(init_luton26)", luton)

    def test_early_initialization_order_is_preserved(self) -> None:
        head = (ROOT / "src/head.S").read_text()
        stages = (
            "stage_probe", "stage_console", "stage_pll", "stage_spi",
            "stage_memctl_config", "stage_memctl_wait", "stage_memtrain",
            "stage_irq", "stage_cache_prepare", "stage_icache",
            "stage_dcache", "stage_cache_enable", "stage_pi",
            "stage_board", "stage_finish",
        )
        for prefix in ("init_jaguar", "init_luton26"):
            cursor = 0
            for stage in stages:
                token = f"loader_stage_call {prefix}_{stage}"
                position = head.find(token, cursor)
                self.assertGreaterEqual(position, 0, token)
                cursor = position + len(token)

        for label in (
            "loader_init_pll_text", "loader_init_spi_text",
            "loader_init_memctl_text", "loader_wait_memctl_text",
            "loader_training_dram_text", "loader_init_irq_text",
            "loader_init_dram_uncached_text", "loader_init_icache_text",
            "loader_init_dcache_text", "loader_enable_caches_text",
            "loader_init_pi_text", "loader_init_board_text",
            "loader_low_level_done_text",
        ):
            self.assertIn(label, head)

    def test_platform_critical_initialization_values_are_preserved(self) -> None:
        jaguar = (ROOT / "src/init_jaguar.c").read_text()
        luton = (ROOT / "src/init_luton26.c").read_text()

        for token in (
            "VTSS_BIT(12) | VTSS_BIT(20)",
            "VTSS_F_ICPU_CFG_PI_MST_PI_MST_CFG_CLK_DIV(0x1f)",
            "VTSS_F_ICPU_CFG_CPU_SYSTEM_CTRL_GENERAL_CTRL_IF_MASTER_PI_ENA",
            "VTSS_ICPU_CFG_PI_MST_PI_MST_CTRL(3) |= 0x00C200B3",
            "write_mii(0, 0, 0, data)",
            "VTSS_DEVCPU_GCB_SIO_CTRL_SIO_CLOCK(1) = 0x14",
        ):
            self.assertIn(token, jaguar)

        for token in (
            "VTSS_BIT(31) | VTSS_BIT(30)",
            "get_chip_id() != 0x7425",
            "write_mii(1, 12, 0, data)",
            "VTSS_DEVCPU_GCB_SIO_CTRL_SIO_CLOCK = 0x14",
            "i == 10 && get_chip_id() == 0x7425",
        ):
            self.assertIn(token, luton)

    def test_uart_loader_is_permanent_fixed_ram_boot_continuation(self) -> None:
        source = (ROOT / "src/uart_ramloader.c").read_text()
        head = (ROOT / "src/head.S").read_text()
        linker = (ROOT / "src/uart_stage1.lds").read_text()
        makefile = (ROOT / "Makefile").read_text()
        self.assertIn("UART_STAGE1_ENTRY void uart_stage1_entry", source)
        self.assertIn('section(".text.entry")', source)
        self.assertIn("boot_flash_kernel(loader_base, soc_family)", source)
        self.assertIn("persistent_recovery_menu(soc_family", source)
        self.assertIn("uart_stage1_blob_start", head)
        self.assertIn(".incbin UART_STAGE1_FILE", head)
        self.assertIn("uart_stage1_copy_loop", head)
        self.assertIn("cache\t0x15", head)
        self.assertIn("cache\t0x10", head)
        self.assertIn("move\ta1, gp", head)
        self.assertIn("loader_uart_stage1_handoff", head)
        self.assertIn("jr\tt9", head)
        self.assertIn("PMOSRAM STAGE1 COPY", head)
        self.assertIn("lbu\tt5, 0(t0)", head)
        self.assertIn("sb\tt5, 0(t3)", head)
        self.assertIn("ranges_overlap_physical", source)
        self.assertNotIn("PMOSRAM STAGE1 RETURN", head)
        self.assertIn("ENTRY(uart_stage1_entry)", linker)
        self.assertIn("ASSERT(uart_stage1_entry == UART_STAGE1_ADDRESS", linker)
        self.assertIn(".embedded_recovery", linker)
        self.assertIn("UART_STAGE1_OBJ :=", makefile)
        self.assertIn("UART_RECOVERY_BLOBS_OBJ", makefile)
        self.assertIn("LDFLAGS_UART_STAGE1", makefile)
        self.assertNotIn("LOADER_OBJECTS += $(BUILD_DIR)/uart_ramloader.o", makefile)

    def test_uart_smoke_payload_is_fixed_address_and_non_destructive(self) -> None:
        source = (ROOT / "payloads/uart-smoke-test/smoke.c").read_text()
        linker = (ROOT / "payloads/uart-smoke-test/linker.ld").read_text()
        makefile = (ROOT / "payloads/uart-smoke-test/Makefile").read_text()
        top_makefile = (ROOT / "Makefile").read_text()
        readme = (ROOT / "payloads/uart-smoke-test/README.md").read_text()
        self.assertIn('section(".text.start")', source)
        self.assertIn('PMOSRAM SMOKE OK', source)
        self.assertNotRegex(source, r"(?i)erase|page.program|write.enable")
        self.assertIn("ASSERT(_start == 0x81000000", linker)
        self.assertIn("validate_fixed_payload.py", makefile)
        self.assertIn("uart-smoke-test:", top_makefile)
        self.assertIn("local-uart-smoke-test:", top_makefile)
        self.assertIn("PMOSRAM RETURNED", readme)
        self.assertIn("PMOSRAM FLASH-BOOT", readme)

    def test_recovery_payload_entry_matches_default_upload_address(self) -> None:
        source = (ROOT / "payloads/uart-firmware-recovery/recovery.c").read_text()
        linker = (ROOT / "payloads/uart-firmware-recovery/linker.ld").read_text()
        makefile = (ROOT / "payloads/uart-firmware-recovery/Makefile").read_text()
        descriptor = (ROOT / "payloads/uart-firmware-recovery/write_descriptor.py").read_text()
        self.assertIn('section(".text.start")', source)
        self.assertIn("KEEP(*(.text.start))", linker)
        self.assertIn("ASSERT(_start == 0x81000000", linker)
        self.assertIn("validate_fixed_payload.py", makefile)
        self.assertIn('"entry_address": args.entry_address', descriptor)
        self.assertIn('"load_address": args.load_address', descriptor)

    def test_make_entry_points_and_codegen_report_are_declared(self) -> None:
        makefile = (ROOT / "Makefile").read_text()
        distrobox = (ROOT / "scripts/distrobox-build.sh").read_text()
        self.assertRegex(makefile, r"(?m)^local-all: check-tools check-config image validate$")
        self.assertRegex(
            makefile,
            r"(?m)^\$\(CODEGEN_REPORT\): \$\(LOADER_ELF\)$",
        )
        self.assertIn("GNU MIPS toolchain not found on host; building through Distrobox.", makefile)
        self.assertIn("if (( missing )); then", makefile)
        self.assertIn("target=local-all", distrobox)
        self.assertRegex(makefile, r"(?m)^local-recovery-payloads: check-tools$")
        self.assertIn("target=local-recovery-payloads", distrobox)

    def test_development_profile_enables_uart_recovery(self) -> None:
        makefile = (ROOT / "Makefile").read_text()
        self.assertIn("UART_RAMLOADER_development := 1", makefile)
        self.assertIn("UART_RAMLOADER_strict := 0", makefile)
        self.assertIn("UART_RAMLOADER_permissive := 0", makefile)
        checker = (ROOT / "scripts/check_config.py").read_text()
        self.assertIn("UART_STAGE1_ADDRESS ?= 0xa7f00000", makefile)
        self.assertIn("UART_STAGE1_MAX_SIZE ?= 0x00100000", makefile)
        self.assertIn("UART_MENU_TIMEOUT_MS ?= 5000", makefile)
        self.assertIn("expected_uncached_alias", checker)
        self.assertIn("uncached KSEG1 alias of uart-ram-end", checker)

    def test_local_jump_normalizer_preserves_returns(self) -> None:
        import importlib.util
        module_path = ROOT / "scripts/normalize_mips_local_jumps.py"
        spec = importlib.util.spec_from_file_location("jump_normalizer", module_path)
        self.assertIsNotNone(spec)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        source = "\tj\t$L12\n\tj\t$BB3_4\n\tj\t.Ltmp7\n\tj\t$31\n"
        normalized, count = module.normalize(source)
        self.assertEqual(count, 3)
        self.assertIn("\tb\t$L12", normalized)
        self.assertIn("\tb\t$BB3_4", normalized)
        self.assertIn("\tb\t.Ltmp7", normalized)
        self.assertIn("\tj\t$31", normalized)

    def test_payload_packer_generates_loader_compatible_crc(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            td_path = Path(td)
            kernel = td_path / "kernel.bin"
            image = td_path / "payload.bin"
            metadata = td_path / "payload.json"
            kernel.write_bytes(bytes(range(100)))
            subprocess.run(
                [
                    "python3",
                    str(ROOT / "tools/mkvcoreiii_payload.py"),
                    "pack",
                    "--input",
                    str(kernel),
                    "--output",
                    str(image),
                    "--load-address",
                    "0x81000000",
                    "--entry-point",
                    "0x81000100",
                    "--metadata",
                    str(metadata),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            data = image.read_bytes()
            words = list(struct.unpack_from("<8I", data))
            stored_crc = words[4]
            words[4] = 0
            crc_input = struct.pack("<8I", *words) + data[32:]
            calculated = zlib.crc32(crc_input) & 0xFFFFFFFF
            self.assertEqual(stored_crc, calculated)
            self.assertEqual(stored_crc, loader_crc_reference(crc_input))
            self.assertEqual(words[2] % 32, 0)
            self.assertEqual(len(data), 32 + words[2])
            self.assertEqual(json.loads(metadata.read_text())["padding"], 28)

            verified = subprocess.run(
                [
                    "python3",
                    str(ROOT / "tools/mkvcoreiii_payload.py"),
                    "verify",
                    str(image),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(verified.returncode, 0, verified.stderr)
            self.assertIn("payload header and CRC are valid", verified.stdout)


if __name__ == "__main__":
    unittest.main()
