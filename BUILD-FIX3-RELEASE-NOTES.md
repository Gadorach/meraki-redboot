# PMOSLIVE buildfix3 release notes

## Confirmed production-build result

After deleting the complete `.work` directory, the operator successfully built
meraki-redboot with the pinned GCC 4.7.3 / binutils 2.23.2 toolchain. The
production build generated the PMOSLIVE payload, embedded recovery payloads,
the shared UART stage and complete 256 KiB development boot region.

This confirms the buildfix2 transport-only source correction works with the
production compiler. The immediate prior failure was caused by stale generated
state rather than source or linker failure.

## buildfix3 change

The top-level `make clean` target now removes `.work/liveboot` as well as the
existing generated build, artifact, recovery, log, and support directories.
It deliberately preserves `.work/toolchains`, so the verified legacy compiler
does not need to be imported or rebuilt after an ordinary clean.

A regression test constructs stale files in all generated subtrees, invokes
`make clean` with an isolated `WORK_ROOT`, confirms each generated subtree is
removed, and confirms the toolchain cache is preserved.
