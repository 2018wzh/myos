.section .text
.align 4
.globl kernel_vector
kernel_vector:
        addi sp, sp, -256
        sd ra,   0(sp)
        sd gp,   8(sp)
        sd tp,  16(sp)
        sd t0,  24(sp)
        sd t1,  32(sp)
        sd t2,  40(sp)
        sd s0,  48(sp)
        sd s1,  56(sp)
        sd a0,  64(sp)
        sd a1,  72(sp)
        sd a2,  80(sp)
        sd a3,  88(sp)
        sd a4,  96(sp)
        sd a5, 104(sp)
        sd a6, 112(sp)
        sd a7, 120(sp)
        sd s2, 128(sp)
        sd s3, 136(sp)
        sd s4, 144(sp)
        sd s5, 152(sp)
        sd s6, 160(sp)
        sd s7, 168(sp)
        sd s8, 176(sp)
        sd s9, 184(sp)
        sd s10, 192(sp)
        sd s11, 200(sp)
        sd t3, 208(sp)
        sd t4, 216(sp)
        sd t5, 224(sp)
        sd t6, 232(sp)
        csrr t0, sepc
        sd t0, 240(sp)
        csrr t0, sstatus
        sd t0, 248(sp)

        call trap_kernel_handler

        ld t0, 248(sp)
        csrw sstatus, t0
        ld t0, 240(sp)
        csrw sepc, t0
        ld ra,   0(sp)
        ld gp,   8(sp)
        # tp identifies the current hart, which may differ after scheduling.
        ld t0,  24(sp)
        ld t1,  32(sp)
        ld t2,  40(sp)
        ld s0,  48(sp)
        ld s1,  56(sp)
        ld a0,  64(sp)
        ld a1,  72(sp)
        ld a2,  80(sp)
        ld a3,  88(sp)
        ld a4,  96(sp)
        ld a5, 104(sp)
        ld a6, 112(sp)
        ld a7, 120(sp)
        ld s2, 128(sp)
        ld s3, 136(sp)
        ld s4, 144(sp)
        ld s5, 152(sp)
        ld s6, 160(sp)
        ld s7, 168(sp)
        ld s8, 176(sp)
        ld s9, 184(sp)
        ld s10, 192(sp)
        ld s11, 200(sp)
        ld t3, 208(sp)
        ld t4, 216(sp)
        ld t5, 224(sp)
        ld t6, 232(sp)
        addi sp, sp, 256
        sret

.align 4
.globl timer_vector
timer_vector:
        csrrw a0, mscratch, a0
        sd a1, 0(a0)
        sd a2, 8(a0)
        sd a3, 16(a0)

        ld a1, 24(a0)
        ld a2, 32(a0)
        ld a3, 0(a1)
        add a3, a3, a2
        sd a3, 0(a1)

        li a1, 2
        csrs sip, a1

        ld a3, 16(a0)
        ld a2, 8(a0)
        ld a1, 0(a0)
        csrrw a0, mscratch, a0
        mret
