#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "payloads/uart-firmware-recovery/recovery.c").read_text()
UBOOT = (ROOT / "tests/reference-mscc-bb-spi-contract.txt").read_text()

required = [
    "#define SPI_CS0_MASK         0x01u",
    "#define SPI_CS_NONE          0x00u",
    "(SPI_CS0_MASK << SPI_SW_CS_OE_SHIFT)",
    "(SPI_CS0_MASK << SPI_SW_CS_SHIFT)",
    "value &= ~SPI_SW_CS_MASK;",
    "value &= ~SPI_SW_SCK_OE;",
    "spi_write(0u);",
    "PMOSREC SPI-CS-CONTRACT ACTIVE-MASK",
    "PMOSRECOVERY3",
    "PREFLIGHT=4",
]
for token in required:
    assert token in SOURCE, token

for forbidden in ["SPI_CS_ALL_HIGH", "SPI_CS0_LOW", "#define SPI_CS0_MASK         0x0eu", "#define SPI_CS_NONE          0x0fu"]:
    assert forbidden not in SOURCE, forbidden

# Model the field semantics used by the upstream MSCC bitbang driver.
CS_SHIFT = 5
CS_OE_SHIFT = 1
CS0 = 1
active = (CS0 << CS_SHIFT) | (CS0 << CS_OE_SHIFT)
deselected = active & ~(0xF << CS_SHIFT)
assert ((active >> CS_SHIFT) & 0xF) == 1
assert ((active >> CS_OE_SHIFT) & 0xF) == 1
assert ((deselected >> CS_SHIFT) & 0xF) == 0

assert "CS(BIT(cs))" in UBOOT
assert "value &= ~ICPU_SW_MODE_SW_SPI_CS_M" in UBOOT
print("PASS: MSCC SW_MODE chip-select uses active-mask semantics")
