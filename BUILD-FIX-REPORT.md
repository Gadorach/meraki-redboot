# VCore-III LinuxLoader v0.6.0 boot-continuation fix

## Hardware finding

The v0.5.1 fixed-RAM UART stage executed and returned successfully enough for
the flash loader to print `PMOSRAM STAGE1 RETURN`, but the following legacy
flash-resident kernel path did not produce kernel output. This proved the UART
stage copy, entry, timer, and return transfer, but not the final kernel handoff.

## Correction

Stage 1 no longer returns to relocatable flash code. The flash shim passes the
payload-header and fallback-region addresses and transfers control with `jr`.
The fixed-RAM stage then performs the original equivalent operations itself:

- SPIM magic, size, alignment, hard-boundary, and load-address validation;
- legacy size strict/warn/hard-only policy;
- CRC strict/warn/off policy over the zeroed-CRC header and padded payload;
- flash-to-RAM copy;
- D-cache writeback/invalidate and I-cache invalidation;
- zero-argument kernel jump;
- fallback-region jump with the original `k0` reason values on failure.

Explicit `PMOSBOOT` markers now identify each handoff stage and error reason.
