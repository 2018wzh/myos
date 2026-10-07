OUTPUT_ARCH(riscv)
ENTRY(main)

PHDRS
{
  code PT_LOAD FLAGS(5);
}

SECTIONS
{
  . = 0x1000;
  .text : {
    KEEP(*(.text.user_entry))
    *(.text .text.*)
  } :code
  .rodata : ALIGN(16) {
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
  } :code
  .data : ALIGN(16) {
    *(.data .data.*)
    *(.sdata .sdata.*)
  } :code
  .bss : ALIGN(16) {
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
  } :code
  ASSERT(main == 0x1000, "user main must be at USER_BASE")
  ASSERT(. <= 0x2000, "initcode exceeds one page")
}
