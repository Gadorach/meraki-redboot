# v0.7.0 build fix report

Implemented an explicit stage-2 boot menu and embedded both platform recovery
programs. A trigger byte only opens the menu; a second explicit `1` or `2` is
required within five seconds. Timeout/invalid input continues normal boot.
Fatal kernel-image checks launch the matching embedded recovery.

The fixed-stage ELF now separates executable code, normal data, and embedded
recovery bytes. The flash loader similarly places the enlarged stage blob after
its GP-relative section, avoiding GCC 4.7.3 small-data relocation overflow.

Exact development build: stage 39,952 bytes; loader 54,816 bytes; boot region
262,144 bytes.
