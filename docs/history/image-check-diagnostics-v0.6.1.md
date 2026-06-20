# v0.6.1 image-check diagnostics and historical size rounding

Hardware showed that v0.6.0 rejected a previously bootable kernel because its
declared SPIM size was not divisible by 32. The original assembly loop copied
32-byte groups until its signed remaining count became non-positive, effectively
rounding the transfer upward. v0.6.1 makes that compatibility behavior explicit
for non-strict profiles and keeps it visible through `WARN-SIZE-ALIGN`.

All flash-kernel and UART executable validation decisions now produce structured
serial records with status and values. This makes policy decisions distinguishable
from hard safety failures and gives enough information to reproduce CRC, size,
and address problems offline.
