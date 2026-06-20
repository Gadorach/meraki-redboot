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

    def test_release_build_uses_pinned_gcc_473_legacy_pic(self) -> None:
        makefile = (ROOT / "Makefile").read_text()
        config = (ROOT / "scripts/toolchain-config.sh").read_text()
        installer = (ROOT / "scripts/install-legacy-toolchain.sh").read_text()
        validator = (ROOT / "scripts/validate_loader_codegen.py").read_text()
        self.assertIn("-mno-abicalls -fPIC -G 65535", makefile)
        self.assertNotIn("-mgpopt", makefile)
        self.assertIn("PRE_DDR_CALL_USED_FLAGS", makefile)
        for register in ("s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7"):
            self.assertIn(f"-fcall-used-{register}", makefile)
        self.assertIn("LOADER_ALWAYS_INLINE", (ROOT / "src/init.h").read_text())
        self.assertIn("-finline-limit=100000", makefile)
        self.assertIn("gcc-4.7.3.tar.bz2", config)
        self.assertIn("5671a2dd3b6ac0d23f305cb11a796aeb", config)
        self.assertIn("binutils-2.23.2.tar.bz2", config)
        self.assertIn("fe914e56fed7a9ec2eb45274b1f2e14b", config)
        self.assertIn("all-gcc", installer)
        toolchain_check = (ROOT / "scripts/check-toolchain.sh").read_text()
        self.assertIn("-fcall-used-s7", toolchain_check)
        self.assertIn("-finline-limit=100000", toolchain_check)
        self.assertIn("without-headers", installer)
        self.assertIn("pre-DDR initialization references the stack pointer", validator)
        self.assertIn("final linked loader retains relocations", validator)

    def test_pre_ddr_initializer_is_one_stackless_leaf_contract(self) -> None:
        makefile = (ROOT / "Makefile").read_text()
        common = (ROOT / "src/init.h").read_text()
        validator = (ROOT / "scripts/validate_loader_codegen.py").read_text()
        self.assertIn("PRE_DDR_CFLAGS", makefile)
        self.assertIn("LOADER_ALWAYS_INLINE", common)
        self.assertIn("LOADER_INIT_SYSTEM_BODY", common)
        self.assertNotIn("init_system(void)", common)
        for source_name, entry in (
            ("init_luton26.c", "init_system_luton26"),
            ("init_jaguar.c", "init_system_jaguar1"),
        ):
            source = (ROOT / "src" / source_name).read_text()
            self.assertLess(source.index("init_board(void)"), source.index(entry))
            self.assertIn("LOADER_INIT_SYSTEM_BODY();", source)
        self.assertIn("call/link instructions", validator)
        self.assertIn('"jal", "jalr", "bal", "bgezal", "bltzal"', validator)


    def test_pre_ddr_cp0_helpers_are_mandatory_inline(self) -> None:
        header = (ROOT / "include/asm/mipsregs.h").read_text()
        self.assertIn(
            "static inline __attribute__((always_inline)) void mtc0_tlbw_hazard(void)",
            header,
        )
        self.assertIn(
            "static inline __attribute__((always_inline)) void tlb_write_indexed(void)",
            header,
        )
        self.assertNotRegex(header, r"static inline void (?:mtc0_tlbw_hazard|tlb_write_indexed)")

    def test_generated_outputs_and_toolchain_are_source_local(self) -> None:
        makefile = (ROOT / "Makefile").read_text()
        config = (ROOT / "scripts/toolchain-config.sh").read_text()
        dispatcher = (ROOT / "scripts/build-dispatch.sh").read_text()
        self.assertIn("WORK_ROOT ?= $(CURDIR)/.work", makefile)
        self.assertIn("$(WORK_ROOT)/build/$(VARIANT)", makefile)
        self.assertIn("$(WORK_ROOT)/artifacts", makefile)
        self.assertIn("postmerkos_work_root", config)
        self.assertIn("printf '%s/.work", config)
        self.assertIn("support-bundle", makefile)
        self.assertIn("build-native.log", dispatcher)
        support = (ROOT / "scripts/create-support-bundle.sh").read_text()
        self.assertIn("validate_uart_stage1.py", support)
        self.assertIn("uart_stage1.lds", support)
        self.assertIn("Importing verified legacy toolchain", (ROOT / "scripts/install-legacy-toolchain.sh").read_text())

    def test_toolchain_bootstrap_disables_recursive_info_generation(self) -> None:
        deps = (ROOT / "scripts/install-deps.sh").read_text()
        installer = (ROOT / "scripts/install-legacy-toolchain.sh").read_text()
        for token in ("bison", "gawk", "makeinfo", "m4", "texinfo"):
            self.assertIn(token, deps)
        self.assertIn("required=(bash make flex bison gawk makeinfo m4", installer)
        self.assertIn("make -j\"$jobs\" MAKEINFO=true all-binutils all-gas all-ld", installer)
        self.assertIn("make MAKEINFO=true install-binutils install-gas install-ld", installer)
        self.assertIn("make -j\"$jobs\" MAKEINFO=true all-gcc", installer)
        self.assertIn("make MAKEINFO=true install-gcc", installer)

    def test_make_all_dispatches_to_optional_distrobox(self) -> None:
        makefile = (ROOT / "Makefile").read_text()
        dispatcher = (ROOT / "scripts/build-dispatch.sh").read_text()
        self.assertIn("./scripts/build-dispatch.sh __all-local", makefile)
        self.assertIn("Use Distrobox for compilation? [Y/n]", dispatcher)
        self.assertIn("BUILD_MODE", dispatcher)
        self.assertIn("__all-local: __loader-local recovery-payloads", makefile)

    def test_current_docs_describe_source_built_gcc473(self) -> None:
        readme = (ROOT / "README.md").read_text()
        building = (ROOT / "docs/BUILDING.md").read_text()
        build_script = (ROOT / "build.sh").read_text()
        for text in (readme, building, build_script):
            self.assertIn("GCC 4.7.3", text)
            self.assertNotIn("pinned AOSP", text)
        self.assertIn("make all", readme)
        self.assertIn("BUILD_MODE=distrobox", building)
        self.assertTrue((ROOT / "docs/TOOLCHAIN.md").is_file())

    def test_development_profile_enables_uart_recovery(self) -> None:
        makefile = (ROOT / "Makefile").read_text()
        self.assertIn("UART_RAMLOADER_development := 1", makefile)
        self.assertIn("UART_RAMLOADER_strict := 0", makefile)
        self.assertIn("UART_RAMLOADER_permissive := 0", makefile)


    def test_uart_engine_is_fixed_ram_stage_not_flash_c_code(self) -> None:
        makefile = (ROOT / "Makefile").read_text()
        head = (ROOT / "src/head.S").read_text()
        linker = (ROOT / "src/uart_stage1.lds").read_text()
        validator = (ROOT / "scripts/validate_uart_stage1.py").read_text()
        manifest = (ROOT / "scripts/write_manifest.py").read_text()

        self.assertIn("UART_RAMLOADER_STAGE1_ADDR ?= 0xa7f00000", makefile)
        self.assertIn("STAGE1_CFLAGS", makefile)
        self.assertIn("UART_STAGE1_ELF", makefile)
        self.assertNotRegex(makefile, r"LOADER_OBJECTS\s*[:+]?=.*uart_ramloader\.o")
        self.assertIn(".incbin UART_STAGE1_FILE", head)
        self.assertIn("UART_STAGE1_LOAD_ADDR", head)
        self.assertIn("jr\tt9", head)
        self.assertIn("PMOSRAM STAGE1 COPY", head)
        self.assertNotIn("PMOSRAM STAGE1 RETURN", head)
        self.assertIn("loader_kernel_continue", head)
        self.assertIn("defined(CONFIG_UART_RAMLOADER)", head)
        self.assertIn("UART_STAGE1_LOAD_ADDR", linker)
        self.assertIn(".uart_stage1", linker)
        self.assertIn("fixed uncached RAM entry", validator)
        self.assertIn("direct jump/call targets outside", validator)
        image_validator = (ROOT / "scripts/validate_image.py").read_text()
        self.assertIn('uart_stage_present = "uart_stage1_blob_start" in syms', image_validator)
        self.assertIn("writer_expected = warnings_expected or uart_stage_present", image_validator)
        self.assertIn('"postmerkos.vcoreiii-linuxloader-build.v6"', manifest)

    def test_uart_stage1_reserves_uncached_alias_above_upload_window(self) -> None:
        makefile = (ROOT / "Makefile").read_text()
        config = (ROOT / "scripts/check_config.py").read_text()
        self.assertIn("UART_RAMLOADER_RAM_END ?= 0x87f00000", makefile)
        self.assertIn("UART_RAMLOADER_STAGE1_ADDR ?= 0xa7f00000", makefile)
        self.assertIn("expected_alias = args.uart_ram_end + 0x20000000", config)
        self.assertIn("UART stage1 must begin at the uncached alias", config)

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
