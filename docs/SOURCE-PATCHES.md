# Source policy implementation

LinuxLoader behavior is selected at compile time. The generated image is never
modified by post-link binary editing.

## CRC policy

- `strict`: calculate payload CRC and enter the normal fallback path on a
  mismatch.
- `warn`: calculate CRC, print a serial warning on mismatch, and continue.
- `off`: compile a copy-only loop and omit the expected-CRC load, CRC table,
  header CRC work, payload CRC work, and final comparison.

## Size policy

Every profile requires a non-zero, 32-byte-aligned payload and enforces
`HARD_PAYLOAD_LIMIT`. The independent legacy threshold supports:

- `legacy-strict`: reject above `LEGACY_PAYLOAD_LIMIT`;
- `legacy-warn`: print a warning and continue only when the hard boundary is
  still satisfied;
- `hard-only`: omit the legacy-threshold comparison.

The configuration checker rejects inconsistent geometry before compilation.
The hard limit cannot cross the configured payload-slot boundary, and the
payload-slot boundary cannot cross the fallback-region stride.

## Profiles

| Profile | CRC | Legacy size threshold | Hard boundary |
|---|---|---|---|
| `strict` | reject | reject | reject |
| `development` | warn | warn | reject |
| `permissive` | omitted | omitted | reject |

Use `make test` to validate source contracts, payload packing, the actual wrapper
recipe, UART protocol contracts, and both recovery-payload target variants.

The design-development record is kept in
[`history/source-policy-development.md`](history/source-policy-development.md).
