from __future__ import annotations

import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
RECOVERY_SOURCE = (ROOT / "payloads/uart-firmware-recovery/recovery.c").read_text(encoding="utf-8")


def old_unscoped_lookup(text: str, key: str) -> str:
    token = json.dumps(key)
    start = text.index(token) + len(token)
    colon = text.index(":", start)
    value, _ = json.JSONDecoder().raw_decode(text[colon + 1 :].lstrip())
    return value


class ManifestDigestShadowingTests(unittest.TestCase):
    def test_sorted_artifact_contains_nested_digest_before_full_image_digest(self) -> None:
        kernel_digest = "11" * 32
        image_digest = "22" * 32
        artifact = {
            "bytes": 16 * 1024 * 1024,
            "kernel_payload": {"sha256": kernel_digest},
            "sha256": image_digest,
        }
        encoded = json.dumps(artifact, sort_keys=True)
        self.assertEqual(old_unscoped_lookup(encoded, "sha256"), kernel_digest)
        self.assertEqual(json.loads(encoded)["sha256"], image_digest)
        self.assertLess(encoded.index(kernel_digest), encoded.index(image_digest))

    def test_recovery_source_tracks_object_and_array_depth(self) -> None:
        self.assertIn("direct_object_depth", RECOVERY_SOURCE)
        self.assertIn("object_depth != direct_object_depth || array_depth != 0u", RECOVERY_SOURCE)
        self.assertIn("if (in_string)", RECOVERY_SOURCE)
        self.assertIn("escaped", RECOVERY_SOURCE)


if __name__ == "__main__":
    unittest.main()
