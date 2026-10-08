OUTPUT_ARCH(riscv)
ENTRY(_entry)

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = DEFINED(USER_IMAGE) ? 0x1000 : 0x80000000;

  .text : ALIGN(DEFINED(USER_IMAGE) ? 1 : 4096) {
    KEEP(*(.text.entry))
    KEEP(*(.text.user_entry))
    *(.text .text.*)
  } :text

  .trampoline : ALIGN(DEFINED(USER_IMAGE) ? 1 : 4096) {
    KEEP(*(.trampoline))
  } :text
  ASSERT(DEFINED(USER_IMAGE) || (ADDR(.trampoline) & 4095) == 0, "trampoline is not page aligned")
  ASSERT(SIZEOF(.trampoline) <= 4096, "trampoline exceeds one page")
  . = DEFINED(USER_IMAGE) ? . : ALIGN(4096);

  .rodata : ALIGN(16) {
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
  } :text

  . = DEFINED(USER_IMAGE) ? . : ALIGN(4096);
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

  . = DEFINED(USER_IMAGE) ? . : ALIGN(4096);
  kernel_end = .;
  ALLOC_BEGIN = .;
  ALLOC_END = 0x88000000;

  ASSERT(ALLOC_BEGIN < ALLOC_END, "kernel image exceeds QEMU RAM")
  ASSERT(DEFINED(USER_IMAGE) ? main == 0x1000 : _entry == 0x80000000, "incorrect image entry")
  ASSERT(!DEFINED(USER_IMAGE) || SIZEOF(.text) > 0, "empty user text")
  ASSERT(!DEFINED(USER_IMAGE) || (SIZEOF(.data) == 0 && SIZEOF(.bss) == 0), "user image must have no writable static data")
  ASSERT(!DEFINED(USER_IMAGE) || . <= 0x2000, "user image exceeds one page")

  /DISCARD/ : {
    *(.eh_frame .eh_frame.*)
    *(.note .note.*)
    *(.comment)
  }
}
