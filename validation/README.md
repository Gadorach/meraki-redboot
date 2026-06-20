# Validation evidence

These files are secondary regression evidence generated from the v0.7.0 source
with the supplied GCC 4.7.3/binutils 2.23.2 reference toolchain. They verify:

- stackless, data-free, call-free, GOT-free pre-DDR stages;
- fixed stage-1 entry at `0xa7f00000`;
- non-executable embedded Jaguar1 and Luton26 recovery images;
- byte-for-byte recovery payload identity and SHA-256 recording;
- menu, persistent flash-failure recovery, and exact-length kernel-copy markers;
- complete 256 KiB dual-copy wrapper fit for every policy profile.

The authoritative release compiler remains Ubuntu 22.04 GCC 10.3/binutils 2.38
through Distrobox. The embedded recovery menu and destructive recovery paths
remain hardware-experimental until serial and flash acceptance testing is
completed.
