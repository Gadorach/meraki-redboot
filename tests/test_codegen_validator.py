from __future__ import annotations

import importlib.util
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "validate_loader_codegen", ROOT / "scripts" / "validate_loader_codegen.py"
)
assert SPEC and SPEC.loader
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)


class PreDdrCodegenValidatorTests(unittest.TestCase):
    def _objdump_script(self, output: str) -> tuple[tempfile.TemporaryDirectory[str], Path]:
        td = tempfile.TemporaryDirectory()
        script = Path(td.name) / "objdump"
        script.write_text(
            "#!/usr/bin/env python3\n"
            f"print({output!r})\n"
        )
        script.chmod(0o755)
        return td, script

    def test_accepts_stackless_leaf(self) -> None:
        td, objdump = self._objdump_script(
            textwrap.dedent(
                """
                00000000 <init_system_luton26>:
                   0: 3c027000        lui     v0,0x7000
                   4: 03e00008        jr      ra
                   8: 00000000        nop
                """
            )
        )
        self.addCleanup(td.cleanup)
        VALIDATOR.validate_pre_ddr_object(str(objdump), Path("probe.o"))

    def test_rejects_stack_reference(self) -> None:
        td, objdump = self._objdump_script(
            "   0: 27bdffc8        addiu   sp,sp,-56\n"
        )
        self.addCleanup(td.cleanup)
        with self.assertRaisesRegex(ValueError, "stack pointer"):
            VALIDATOR.validate_pre_ddr_object(str(objdump), Path("probe.o"))

    def test_rejects_nested_call(self) -> None:
        td, objdump = self._objdump_script(
            "   0: 0c000000        jal     0 <helper>\n"
        )
        self.addCleanup(td.cleanup)
        with self.assertRaisesRegex(ValueError, "call/link"):
            VALIDATOR.validate_pre_ddr_object(str(objdump), Path("probe.o"))

    def test_rejects_absolute_jump(self) -> None:
        td, objdump = self._objdump_script(
            "   0: 08000010        j       40 <helper>\n"
        )
        self.addCleanup(td.cleanup)
        with self.assertRaisesRegex(ValueError, "absolute J"):
            VALIDATOR.validate_pre_ddr_object(str(objdump), Path("probe.o"))


if __name__ == "__main__":
    unittest.main()
