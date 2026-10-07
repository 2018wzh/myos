OUTPUT_ARCH(riscv)
ENTRY(_entry)

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = 0x80000000;

  .text : ALIGN(4096) {
    KEEP(*(.text.entry))
    *(.text .text.*)
  } :text

  .trampoline : ALIGN(4096) {
    KEEP(*(.trampoline))
  } :text
  ASSERT((trampoline & 4095) == 0, "trampoline is not page aligned")
  ASSERT(SIZEOF(.trampoline) <= 4096, "trampoline exceeds one page")
  . = ALIGN(4096);

  .rodata : ALIGN(16) {
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
  } :text

  . = ALIGN(4096);
  KERNEL_DATA = .;

  .data : ALIGN(16) {
    *(.data .data.*)
    *(.sdata .sdata.*)
  } :data

  .bss : ALIGN(16) {
    *(.bss.stack)
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
  } :data

  . = ALIGN(4096);
  kernel_end = .;
  ALLOC_BEGIN = .;
  ALLOC_END = 0x88000000;

  ASSERT(ALLOC_BEGIN < ALLOC_END, "kernel image exceeds QEMU RAM")
  ASSERT(_entry == 0x80000000, "kernel entry must be at KERNEL_BASE")
}
