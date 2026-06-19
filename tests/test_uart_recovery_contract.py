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
        self.assertIn("header->version != RAMLOADER_PROTOCOL_VERSION || header->flags != 0u", LOADER_SOURCE)
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

    def test_recovery_enforces_manifest_flash_and_terminal_contracts(self) -> None:
        for token in (
            "PMOSRECOVERY2;SOC=", "PKG_MAGIC1", "PMOSPKG VERIFIED",
            "PMOSREC ERASE-CHALLENGE", "PMOSREC RESULT DRY-RUN-OK",
            "PMOSREC RESULT SUCCESS", "PMOSREC RESULT ERROR",
            "spi_verify_full", "spi_read_id", "spi_wait_ready", "FLASH-PROTECTED",
            "spi_check_completion", "FLASH-FLAG-", "accepted_jedec_ids",
            "OBJECT_TRANSFER_TIMEOUT_MS", "CONFIRM_TIMEOUT_MS", "struct pmos_timer",
            "image_sha256", "manifest_sha256", "target_family", "boot_chain",
            "uart_firmware", "flash_geometry", "accepted_models",
            "MANIFEST-RECOVERY-CAPABILITY", "MANIFEST-LOADER-DIGEST",
        ):
            self.assertIn(token, RECOVERY_SOURCE)
        self.assertNotIn("current-artifact-validated", RECOVERY_SOURCE)
        self.assertNotIn("historically-validated", RECOVERY_SOURCE)


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
