/* Linker script for the RELOCATABLE build (.mod).

   Base 0: the module carries its relocations, so where it was linked does not
   matter - and linking at the real cave address would drag in ~52 KB of page
   alignment padding ahead of the code, which would then be copied and stored
   for nothing.

   The layout still matches the loaded one: signature at +0, entry at +4. */
ENTRY(payload_main)
SECTIONS
{
  . = 0x0;
  .magic : { KEEP(*(.magic)) }
  . = 0x4;
  .text  : SUBALIGN(4) {
    *(.text.start)
    *(.text .text.*)
  }
  /* The hook table, kept as one contiguous run so the build tool can hand the
     loader an offset and a count. KEEP because nothing references it. */
  .mc3hooks : { PROVIDE(__mc3hooks_start = .); KEEP(*(.mc3hooks)) PROVIDE(__mc3hooks_end = .); }
  .rodata : { *(.rodata .rodata.*) }
  .data   : { *(.data .data.*) }
  .bss    : { *(.bss .bss.*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.pdr) *(.reginfo) *(.MIPS.abiflags) *(.gnu.attributes) }
}
