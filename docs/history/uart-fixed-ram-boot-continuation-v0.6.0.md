# Fixed-RAM boot continuation (v0.6.0)

Hardware testing of v0.5.1 reached `PMOSRAM STAGE1 RETURN` but did not produce
normal kernel output. Because the fixed-RAM stage had already proven UART, timer,
stack, and flash-return execution, v0.6.0 removes the return entirely.

The relocatable flash shim now supplies stage 1 with the current SPIM payload
header and next fallback entry. Stage 1 validates and copies the kernel using the
same header and policy semantics, performs cache maintenance, and jumps directly
to the kernel. Failure paths print a reason and enter the next fallback region
with the original loader reason code in `k0`.
