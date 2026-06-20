# VCore-III LinuxLoader v0.6.0 build review

- Exact GCC 4.7.3/binutils 2.23.2 development build passed.
- Fixed-RAM stage entry remains `0xa7f00000`; development stage size is
  `0x1550` bytes.
- Development loader size is `20312` bytes; boot region is exactly `262144` bytes.
- Strict, development, permissive, and strict-plus-UART exact builds passed.
- 33 source, validator, payload-format, and UART protocol tests passed.
- Stage validation requires UART and `PMOSBOOT` kernel markers plus explicit
  kernel and fallback jump helpers.
- Hardware validation remains required for flash-header interpretation, CRC,
  cache handoff, kernel execution, and uploaded recovery-payload operation.
