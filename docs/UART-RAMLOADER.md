# UART RAM loader and PMOSREC

The fixed-RAM stage exposes a stable 115200-baud `PMOSRAM2` executable loader.
It validates header geometry, load and entry ranges, frame CRC-32, complete
object CRC-32 and SHA-256 before invalidating caches and entering the uploaded
program.

The normal recovery executable is PMOSREC v3. Keeping the permanent loader
simple and fixed-rate allows adaptive UART, sparse transport, LZ4 and flash
logic to evolve in a replaceable RAM payload.

## Menu paths

1. `UART-RAMLOADER` uploads a current PMOSREC executable.
2. `FW-RECOVERY` launches the PMOSREC executable embedded in the installed
   loader.

Menu option 1 is preferred during development and whenever the installed
loader's embedded recovery contract is older than the host tooling.

## PMOSREC v3 handoff

After `PMOSRAM VERIFIED` and `PMOSRAM EXEC`, the host requires:

```text
PMOSREC READY 3
PMOSREC DESCRIPTOR PMOSRECOVERY3;...;PROTO=3;PREFLIGHT=4;...;END
PMOSREC UART-CAP ...
PMOSREC FLASH-PREFLIGHT-OK ...
PMOSREC COMMAND-READY 3
```

Only then does adaptive rate qualification begin. A failed PMOSREC experiment
is recovered by power cycling and returning to the unchanged 115200-baud RAM
loader.

The success path uses the family soft-chip reset and then arms the ICPU watchdog if execution unexpectedly continues.
