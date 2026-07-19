# PMOSLIVE buildfix3

This revision fixes the top-level `make clean` target so it removes the complete
PMOSLIVE work tree at `.work/liveboot` along with the existing build, recovery,
artifact, log, and support outputs.

In buildfix2, `.work/liveboot/.built` and its artifacts survived `make clean`.
A subsequent build could therefore validate a stale PMOSLIVE binary before the
updated source was recompiled. Deleting the complete `.work` directory proved
that the buildfix2 PMOSLIVE source itself compiled and validated successfully
with the pinned GCC 4.7.3 toolchain.

A regression test now creates stale files in every generated work subtree, runs
`make clean` with an isolated `WORK_ROOT`, verifies that `.work/liveboot` and all
other generated outputs are removed, and verifies that the cached toolchain
subtree is preserved.
