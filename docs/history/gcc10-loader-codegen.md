# GCC 10 incompatibility investigation

The distribution-provided Ubuntu 22.04 MIPS cross compiler rejected the
historical loader combination of `-mno-abicalls` and `-fPIC`. A later attempt to
represent the loader with GCC 10 as direct calls plus GP-relative small data was
also rejected after inspection showed:

- `R_MIPS_HI16`/`R_MIPS_LO16` absolute references for a Jaguar diagnostic
  string; and
- stack-pointer use in code that executes before DDR and the stack exist.

The code-generation validator correctly blocked that image. This investigation
established that compiler behavior is part of the loader's executable contract,
not merely a build convenience.

Version 0.4.0 superseded the GCC 10 path with a checksum-pinned source build of
GCC 4.7.3 and binutils 2.23.2. Modern GCC compatibility remains a possible
future source-refactoring project, but it is not part of the release path.
