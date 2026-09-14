/* The binary is loaded RAW at the address below and its entry is called at +4,
   right after the signature - so the link MUST happen at that address. The code
   is position dependent: it references its own constants by absolute address
   (`lui v1,0x61; lwc1 $f1,OFFSET(v1)`), so linking elsewhere makes it read
   garbage.

   This address must match PAYLOAD in ../mc3_inject.py. Change one, change the
   other. */
ENTRY(payload_main)
SECTIONS
{
  . = 0x0061D000;
  .magic : { KEEP(*(.magic)) }        /* word 0: the signature the loader checks */

  /* The loader jumps to BASE+4, so the entry has to land exactly there. Without
     SUBALIGN the linker honours the 8-byte alignment of functions and leaves a
     word of padding - which happens to become a `nop` (zero is nop on MIPS) and
     would work, but by accident. SUBALIGN(4) makes the layout explicit. */
  . = 0x0061D004;
  .text  : SUBALIGN(4) {
    *(.text.start)                    /* payload_main, immediately after */
    *(.text .text.*)
  }
  .mc3hooks : { PROVIDE(__mc3hooks_start = .); KEEP(*(.mc3hooks)) PROVIDE(__mc3hooks_end = .); }
  .rodata : { *(.rodata .rodata.*) }
  .data   : { *(.data .data.*) }
  .bss    : { *(.bss .bss.*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.pdr) *(.reginfo) *(.MIPS.abiflags) *(.gnu.attributes) }
}
