from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock
import zlib

ROOT = Path(__file__).resolve().parents[1]
LOADER_SOURCE = (ROOT / "src/uart_ramloader.c").read_text(encoding="utf-8")
RECOVERY_SOURCE = (ROOT / "payloads/uart-firmware-recovery/recovery.c").read_text(encoding="utf-8")
HEAD_SOURCE = (ROOT / "src/head.S").read_text(encoding="utf-8")

spec = importlib.util.spec_from_file_location("uart_ramload_send", ROOT / "tools/uart_ramload_send.py")
sender = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(sender)


class UartRecoveryContractTests(unittest.TestCase):
    def test_loader_rejects_flags_and_bounds_entire_object(self) -> None:
        self.assertIn("header->version != RAMLOADER_PROTOCOL_VERSION", LOADER_SOURCE)
        self.assertIn("header->flags != 0u", LOADER_SOURCE)
        self.assertIn("end > CONFIG_UART_RAMLOADER_RAM_END", LOADER_SOURCE)
        self.assertIn("header->entry_addr >= end", LOADER_SOURCE)
        with self.assertRaisesRegex(sender.ProtocolError, "DRAM range"):
            sender.make_header(b"x" * 128, 0x87EFFFC0, 0x87EFFFC0, 64)

    def test_loader_receive_paths_are_bounded_and_abort_to_boot(self) -> None:
        self.assertIn("static int uart_recv_exact", LOADER_SOURCE)
        self.assertIn("struct pmos_timer", LOADER_SOURCE)
        self.assertIn("RAMLOADER_TRANSFER_TIMEOUT_MS", LOADER_SOURCE)
        self.assertNotIn("static u8 uart_getc(void)", LOADER_SOURCE)
        self.assertIn('abort_to_boot("HEADER-TIMEOUT")', LOADER_SOURCE)
        self.assertIn('abort_to_boot("FRAME-DATA-TIMEOUT")', LOADER_SOURCE)
        self.assertIn('abort_to_boot("TRANSFER-TIMEOUT")', LOADER_SOURCE)
        self.assertIn("CONFIG_UART_RAMLOADER_COUNT_HZ", LOADER_SOURCE)
        self.assertNotIn("timeout_ticks", LOADER_SOURCE)

    def test_loader_performs_cache_coherency_before_jump(self) -> None:
        self.assertIn('cache 0x15', LOADER_SOURCE)
        self.assertIn('cache 0x10', LOADER_SOURCE)
        self.assertIn("cache_prepare_for_execution(header.load_addr, header.total_size)", LOADER_SOURCE)
        self.assertLess(LOADER_SOURCE.index("cache_prepare_for_execution"), LOADER_SOURCE.rindex("entry();"))

    def test_detected_soc_is_passed_into_ramloader(self) -> None:
        self.assertIn("li\ts7, 2", HEAD_SOURCE)
        self.assertIn("li\ts7, 1", HEAD_SOURCE)
        self.assertIn("move\ta0, s7", HEAD_SOURCE)


    def test_flash_loader_only_copies_and_calls_fixed_ram_stage(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        stage_linker = (ROOT / "src/uart_stage1.lds").read_text(encoding="utf-8")
        entry_source = (ROOT / "src/uart_stage1_entry.S").read_text(encoding="utf-8")
        self.assertIn(".incbin UART_STAGE1_FILE", HEAD_SOURCE)
        self.assertIn("jr\tt9", HEAD_SOURCE)
        self.assertNotIn("jalr\tt9", HEAD_SOURCE)
        self.assertNotIn("bal\tuart_ramloader_probe_and_run", HEAD_SOURCE)
        self.assertNotIn("uart_ramloader.o", makefile.split("LOADER_OBJECTS :=", 1)[1].splitlines()[0])
        self.assertIn("ENTRY(uart_stage1_entry)", stage_linker)
        self.assertIn("UART_STAGE1_LOAD_ADDR", stage_linker)
        self.assertIn("uart_stage1_main", entry_source)
        self.assertIn("b uart_stage1_main", entry_source)
        self.assertIn("uart_stage1_jump_kernel", entry_source)
        self.assertNotIn("uart_stage1_jump_fallback", entry_source)
        self.assertNotIn("PMOSRAM STAGE1 RETURN", HEAD_SOURCE)
        self.assertIn("loader_kernel_continue", HEAD_SOURCE)
        self.assertIn("and\ta1, a1, t0", HEAD_SOURCE)
        self.assertIn("move\ta3, gp", HEAD_SOURCE)
        # GNU as 2.23.2 requires relocatable label addresses to be formed with
        # explicit HI16/LO16 relocations; `li reg, label` requires an absolute
        # assembly-time expression and fails before link.
        self.assertIn("lui\ta0, %hi(loader_uart_stage1_copy_text)", HEAD_SOURCE)
        self.assertIn("addiu\ta0, a0, %lo(loader_uart_stage1_copy_text)", HEAD_SOURCE)
        self.assertNotIn("loader_uart_stage1_return_text", HEAD_SOURCE)
        self.assertNotIn("li\ta0, loader_uart_stage1_copy_text", HEAD_SOURCE)
        self.assertNotIn("li\ta0, loader_uart_stage1_return_text", HEAD_SOURCE)


    def test_fixed_ram_stage_owns_flash_kernel_boot(self) -> None:
        stage = LOADER_SOURCE
        entry_source = (ROOT / "src/uart_stage1_entry.S").read_text(encoding="utf-8")
        for token in (
            "PMOSBOOT", "PASS", "WARN", "FAIL", "SKIP",
            "HEADER-ADDRESS", "MAGIC", "SIZE-ALIGN", "SIZE-HARD",
            "SIZE-LEGACY", "LOAD-SEGMENT", "LOAD-OVERFLOW",
            "ENTRY-RANGE", "COPY", "CRC", "CACHE", "EXEC",
            "boot_flash_kernel", "CONFIG_HARD_PAYLOAD_LIMIT",
            "CONFIG_LEGACY_PAYLOAD_LIMIT", "SPIM_LOADER_MAGIC",
            "STAGE1-OVERLAP", "STACK-OVERLAP",
        ):
            self.assertIn(token, stage)
        self.assertIn("uart_stage1_jump_kernel", entry_source)
        self.assertNotIn("uart_stage1_jump_fallback", entry_source)
        self.assertIn("uart_stage1_main", stage)
        self.assertIn("boot_flash_kernel(soc_family, payload_header_addr)", stage)

    def test_flash_failure_launches_embedded_platform_recovery(self) -> None:
        stage = LOADER_SOURCE
        blob_source = (ROOT / "src/uart_stage1_recovery_blobs.S").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        for token in (
            "launch_embedded_recovery", "recovery_luton26_blob_start",
            "recovery_jaguar1_blob_start", "RECOVERY-COPY", "RECOVERY-EXEC",
            'launch_embedded_recovery(soc_family, "MAGIC")',
            'launch_embedded_recovery(soc_family, "CRC")',
        ):
            self.assertIn(token, stage)
        self.assertIn(".incbin RECOVERY_LUTON26_FILE", blob_source)
        self.assertIn(".incbin RECOVERY_JAGUAR1_FILE", blob_source)
        self.assertIn("UART_STAGE1_RECOVERY_OBJ", makefile)
        self.assertNotIn("fallback_to_next_region", stage)
        self.assertNotIn("uart_stage1_jump_fallback", stage)

    def test_stage2_menu_requires_explicit_choice_and_times_out_to_boot(self) -> None:
        stage = LOADER_SOURCE
        for token in (
            "PMOSBOOT MENU-PROBE", "PMOSBOOT MENU-READY",
            "1=UART-RAMLOADER", "2=FW-RECOVERY",
            "CONFIG_UART_MENU_TIMEOUT_MS", "MENU-OPTION-1", "MENU-OPTION-2",
            "NO EXPLICIT 1/2 SELECTION; CONTINUING NORMAL BOOT",
        ):
            self.assertIn(token, stage)
        self.assertIn("choice == (u8)'1'", stage)
        self.assertIn("choice == (u8)'2'", stage)
        self.assertIn("boot_flash_kernel(soc_family, payload_header_addr)", stage)


    def test_flash_size_alignment_uses_historical_rounded_copy_policy(self) -> None:
        self.assertIn("copy_size = (header.size + 31u) & ~31u", LOADER_SOURCE)
        self.assertIn('report_pair("PMOSBOOT", "WARN", "SIZE-ALIGN"', LOADER_SOURCE)
        self.assertIn('report_pair("PMOSBOOT", "FAIL", "SIZE-ALIGN"', LOADER_SOURCE)
        self.assertIn("offset < copy_size", LOADER_SOURCE)
        self.assertIn("cache_prepare_for_execution(header.load_addr, copy_size)", LOADER_SOURCE)
        self.assertIn('report_pair("PMOSBOOT", "PASS", "COPY"', LOADER_SOURCE)

    def test_image_checks_emit_structured_pass_warn_fail_diagnostics(self) -> None:
        for token in (
            'report_pair("PMOSBOOT", "PASS", "MAGIC"',
            'report_pair("PMOSBOOT", "WARN", "CRC"',
            'report_pair("PMOSBOOT", "FAIL", "CRC"',
            'report_pair("PMOSRAM", "PASS", "HEADER-CRC"',
            'report_pair("PMOSRAM", "FAIL", "IMAGE-CRC"',
            'report_digest_pair("PMOSRAM", "PASS", "IMAGE-SHA256"',
            'report_digest_pair("PMOSRAM", "FAIL", "IMAGE-SHA256"',
        ):
            self.assertIn(token, LOADER_SOURCE)


    def test_recovery_flat_binary_has_explicit_byte_zero_entry(self) -> None:
        entry = (ROOT / "payloads/uart-firmware-recovery/entry.S").read_text(encoding="utf-8")
        linker = (ROOT / "payloads/uart-firmware-recovery/linker.ld").read_text(encoding="utf-8")
        makefile = (ROOT / "payloads/uart-firmware-recovery/Makefile").read_text(encoding="utf-8")
        self.assertIn('.section .text.start', entry)
        self.assertIn('lui     $sp, 0x813f', entry)
        self.assertIn('jal     recovery_main', entry)
        self.assertIn('KEEP(*(.text.start))', linker)
        self.assertIn('ASSERT(_start == 0x81000000', linker)
        self.assertIn('*(.MIPS.abiflags)', linker)
        self.assertIn('$(BUILD_DIR)/entry.o', makefile)
        self.assertIn('Entry point address:', makefile)
        self.assertNotIn('void _start(void)', RECOVERY_SOURCE)
        self.assertIn('void recovery_main(void)', RECOVERY_SOURCE)

    def test_recovery_json_lookup_is_scoped_to_direct_object_members(self) -> None:
        self.assertIn("direct_object_depth", RECOVERY_SOURCE)
        self.assertIn("object_depth != direct_object_depth || array_depth != 0u", RECOVERY_SOURCE)
        self.assertIn("manifest_lookup_contract", (ROOT / "payloads/uart-firmware-recovery/write_descriptor.py").read_text(encoding="utf-8"))
        # The artifact object intentionally contains a nested kernel SHA before its
        # own full-image SHA in sorted JSON. The recovery parser must not accept
        # that nested value when looking up artifact.sha256.
        self.assertIn('json_object_value(MANIFEST_BASE, header->manifest_size, "artifact"', RECOVERY_SOURCE)
        self.assertIn('json_string_value(artifact_object, artifact_length, "sha256"', RECOVERY_SOURCE)

    def test_recovery_enforces_manifest_flash_and_terminal_contracts(self) -> None:
        for token in (
            "PMOSRECOVERY2;SOC=", "PKG_MAGIC1", "PMOSPKG VERIFIED",
            "PMOSREC ERASE-CHALLENGE", "PMOSREC RESULT DRY-RUN-OK",
            "PMOSREC RESULT SUCCESS", "PMOSREC RESULT ERROR",
            "spi_verify_full", "spi_read_id", "spi_wait_ready", "FLASH-PROTECTED",
            "spi_check_completion", "FLASH-FLAG-", "accepted_jedec_ids",
            "PACKAGE_HEADER_TIMEOUT_MS", "OBJECT_TRANSFER_TIMEOUT_MS",
            "CONFIRM_TIMEOUT_MS", "struct pmos_timer", "PMOSREC COMMAND-READY 1",
            "PMOSPFT HEADER-ACK", "spi_scratch_preflight", "SPI-GENERAL",
            "FLASH-PREFLIGHT-OK", "PREFLIGHT-RESTORE-FAILED",
            "PREFLIGHT-BOOTLOADER-CHANGED", "BOOTLOADER-UNCHANGED",
            "image_sha256", "manifest_sha256", "target_family", "boot_chain",
            "uart_firmware", "flash_geometry", "accepted_models",
            "MANIFEST-RECOVERY-CAPABILITY", "MANIFEST-LOADER-DIGEST",
        ):
            self.assertIn(token, RECOVERY_SOURCE)
        self.assertNotIn("current-artifact-validated", RECOVERY_SOURCE)
        self.assertNotIn("historically-validated", RECOVERY_SOURCE)
        self.assertIn("recv_exact(prefix, sizeof(prefix), PACKAGE_HEADER_TIMEOUT_MS)", RECOVERY_SOURCE)
        self.assertIn("recv_exact(raw + 8u, PACKAGE_HEADER_BYTES - 8u, PACKAGE_HEADER_TIMEOUT_MS)", RECOVERY_SOURCE)
        self.assertNotIn("recv_exact(raw, PACKAGE_HEADER_BYTES, INTERBYTE_TIMEOUT_MS)", RECOVERY_SOURCE)

    def test_recovery_preflight_preserves_bootloader_and_restores_scratch(self) -> None:
        self.assertIn("request->scratch_address < LOADER_REGION_SIZE", RECOVERY_SOURCE)
        self.assertIn("request->scratch_size != device->erase_size", RECOVERY_SOURCE)
        self.assertIn("spi_read_block(request->scratch_address, backup", RECOVERY_SOURCE)
        self.assertIn("spi_program_pattern", RECOVERY_SOURCE)
        self.assertIn("spi_verify_pattern", RECOVERY_SOURCE)
        self.assertIn("spi_program_buffer", RECOVERY_SOURCE)
        self.assertIn("spi_verify_buffer", RECOVERY_SOURCE)
        self.assertIn("PMOSREC RESULT PREFLIGHT-OK", RECOVERY_SOURCE)


    def test_duplicate_frames_are_exact_and_wrap_safe(self) -> None:
        self.assertIn("expected_sequence != 0u && frame.sequence == expected_sequence - 1u", LOADER_SOURCE)
        self.assertIn("frame.length == previous_length", LOADER_SOURCE)
        self.assertIn("frame.crc32 == previous_crc", LOADER_SOURCE)
        self.assertNotIn("frame.sequence + 1u == expected_sequence", LOADER_SOURCE)
        self.assertIn("sequence != 0u && frame.sequence == sequence - 1u", RECOVERY_SOURCE)
        self.assertIn("frame.length == previous_length", RECOVERY_SOURCE)
        self.assertIn("frame.crc32 == previous_crc", RECOVERY_SOURCE)
        self.assertNotIn("frame.sequence + 1u == sequence", RECOVERY_SOURCE)

    def test_recovery_object_deadline_is_enforced(self) -> None:
        self.assertIn("timer_start(&transfer_timer)", RECOVERY_SOURCE)
        self.assertIn("timer_expired(&transfer_timer, OBJECT_TRANSFER_TIMEOUT_MS)", RECOVERY_SOURCE)
        self.assertIn("OBJECT-TRANSFER-TIMEOUT", RECOVERY_SOURCE)

    def test_standalone_sender_accepts_completion_as_final_ack(self) -> None:
        link = mock.Mock()
        link.wait_for.return_value = "PMOSRAM VERIFIED SHA256=test"
        response = sender.send_frame(
            link, b"frame", 7, retries=0, timeout=1.0,
            completion_prefix="PMOSRAM VERIFIED",
        )
        self.assertTrue(response.startswith("PMOSRAM VERIFIED"))
        link.wait_for.assert_called_once_with(
            ("PMOSRAM ACK 00000007", "PMOSRAM NACK 00000007", "PMOSRAM VERIFIED"),
            1.0,
        )

    def test_sender_enters_stage2_menu_option_one(self) -> None:
        link = mock.Mock()
        link.wait_for.side_effect = ["PMOSBOOT MENU-PROBE TIMEOUT_MS=00000bb8", "PMOSBOOT MENU-READY TIMEOUT_MS=00001388", "PMOSRAM READY 2 SOC=jaguar1"]
        sender.enter_boot_menu(link, 60.0)
        self.assertEqual(link.write_all.call_args_list, [mock.call(b" "), mock.call(b"1")])

    def test_sender_header_integrity_and_short_writes(self) -> None:
        payload = b"payload" * 100
        header = sender.make_header(payload, 0x81000000, 0x81000000, 128)
        fields = sender.HEADER.unpack(header)
        self.assertEqual(fields[0], sender.MAGIC)
        self.assertEqual(fields[1], 2)
        self.assertEqual(fields[2], 0)
        self.assertEqual(fields[7], zlib.crc32(payload) & 0xFFFFFFFF)
        self.assertEqual(fields[-1], zlib.crc32(header[:-4]) & 0xFFFFFFFF)

        link = sender.SerialLink(7)
        captured = bytearray()
        actions = [1, BlockingIOError(), 2, InterruptedError(), 2]
        def fake_write(_fd, view):
            action = actions.pop(0)
            if isinstance(action, BaseException):
                raise action
            captured.extend(bytes(view[:action]))
            return action
        with mock.patch.object(sender.select, "select", return_value=([], [7], [])), \
             mock.patch.object(sender.os, "write", side_effect=fake_write):
            link.write_all(b"abcde", timeout=1.0)
        self.assertEqual(captured, b"abcde")
        self.assertFalse(actions)


if __name__ == "__main__":
    unittest.main()
