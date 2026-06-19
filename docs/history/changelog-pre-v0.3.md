# Changelog

## 0.2.0 — 2026-06-19

- Replaced historical final-branch NOP bypasses with source-level CRC and size
  policy paths.
- Added CRC `strict`, `warn`, and true `off` modes; `off` omits CRC calculation
  and the lookup table.
- Added size `legacy-strict`, `legacy-warn`, and `hard-only` modes.
- Added a mandatory hard flash-boundary check in every build.
- Separated payload-slot end from fallback-loader stride so an earlier rootfs boundary can be enforced.
- Corrected the original maximum-size off-by-32 condition by including the
  payload header in region-capacity calculations (`0x3bffe0` safe default).
- Added serial warning output for warning policies.
- Added zero-length and 32-byte-alignment checks.
- Added a kernel payload packer/verifier that pads, creates the header, and
  writes loader-compatible IEEE CRC-32.
- Added policy/geometry metadata to build manifests and expanded structural
  validation.

## 0.1.0 — 2026-06-18

- Initial standalone GPL-source extraction, MIPS build scripts, and source-owned
  256 KiB VCore-III boot wrapper.
