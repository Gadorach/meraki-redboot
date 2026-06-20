# Abandoned GCC 10 custom-GP experiment

An intermediate development attempt replaced the loader's historical
`-mno-abicalls -fPIC -G 65535` code generation with non-PIC GP-relative small
data so Ubuntu 22.04's GCC 10 cross compiler would accept the build.

That approach added `_gp = 0`, forced selected saved registers to be
call-clobbered, and introduced relocation checks intended for the translated
model. It compiled further than the original flags but GCC 10 still emitted an
absolute string reference and stack-using pre-DDR code. Allowing those results
would have produced an unsafe loader.

The experiment was therefore removed from the active build in version 0.4.0.
The active release path instead source-builds GCC 4.7.3 and binutils 2.23.2 and
uses the source's original embedded MIPS PIC model. The useful safety checks—no
pre-DDR stack use, no unresolved/dynamic linkage, and no final relocations—were
retained independently of the abandoned translation.
