# Boot-region reference contract

The current build creates the complete 256 KiB boot region from source and
validates its wrapper, descriptors, active slot, fallback slot, checksums, and
UART recovery capability on every build. Generated output never copies a
reference boot region.

The reconstruction evidence, instruction comparison, and supplied-image hashes
are retained in [project history](history/boot-region-reference-analysis.md).

Run an optional comparison against a locally supplied reference image with:

```bash
make reference-check REFERENCE_IMAGE=/path/to/reference-loader.bin
```
