# Supplied boot-region reference analysis

The two GPL-adjacent images were analyzed only to recover the outer 256 KiB
packaging contract. They are never copied into generated output.

## Whole-image hashes

```text
redboot-nocrc.bin
  SHA-256 43105c9ab54071d64068cba533674685a4c9c3a533f928fe36aa16423d98e937

redboot-nocrc-sz.bin
  SHA-256 adaac5a2ae1695b04a94719ebe3e97844d7682c1b79b355f486a87b05315a477
```

Both are exactly `0x40000` bytes and share the same wrapper/descriptor layout:

```text
active descriptor   0x444
active body         0x39018, length 0x381c, end 0x3c834
fallback descriptor 0x41c
fallback body       0x3c834, length 0x37cc, end 0x40000
```

The fallback body is identical between the two files. The active bodies differ
at only three stored bytes, all within the single 32-bit instruction at active
loader offset `0x00cc`: the `nocrc` image contains `bltz t0,...` (`0x05000230`
in little-endian decoded form), while `nocrc-sz` contains `0x00000000` (`nop`).
This directly corroborates the adjacent README's description of the size-check
hex edit.

The CRC-reject instruction in both active bodies is already zeroed. The source
project avoids relying on its historical absolute offset by adding the symbol
`loader_crc_reject_branch` and validating the instruction at that symbol after
each new link.

## Wrapper instruction words

The following words are identical in both supplied files and are reproduced by
`src/boot_wrapper.S`:

```text
0x400  0x3c1bffff
0x404  0x377bfc00
0x408  0x037ff824
0x40c  0x10000000
0x414  0x10000007
0x434  0x10000003
0x43c  0x10000007
```

Run the included analyzer against either original file:

```bash
make reference-check REFERENCE_IMAGE=/path/redboot-nocrc-sz.bin
```
