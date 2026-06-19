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
