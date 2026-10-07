.section .text.entry
.globl _entry
_entry:
        csrr a0, mhartid
        li   t0, HART_COUNT
        bgeu a0, t0, 2f
        li   t0, 4096
        addi t1, a0, 1
        mul  t0, t0, t1
        la   sp, boot_stacks
        add  sp, sp, t0
        call start
1:
        wfi
        j    1b
2:
        wfi
        j    2b

.section .bss.stack
.align 12
.globl boot_stacks
boot_stacks:
        .space 4096 * HART_COUNT
