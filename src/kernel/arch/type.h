#ifndef KERNEL_ARCH_TYPE_H
#define KERNEL_ARCH_TYPE_H

#include "../lib/type.h"

#ifndef HART_COUNT
#define HART_COUNT 2
#endif

#if HART_COUNT != 1 && HART_COUNT != 2
#error HART_COUNT must be 1 or 2
#endif

#define NCPU HART_COUNT
#define MAX_HARTS NCPU

#define MSTATUS_MIE       (1UL << 3)
#define MSTATUS_MPP_MASK  (3UL << 11)
#define MSTATUS_MPP_S     (1UL << 11)
#define MSTATUS_FS_MASK   (3UL << 13)
#define MSTATUS_FS_INITIAL (1UL << 13)

#define MIE_MTIE (1UL << 7)

#define SSTATUS_SIE (1UL << 1)
#define SSTATUS_SPIE (1UL << 5)
#define SSTATUS_SPP (1UL << 8)
#define SIE_SSIE     (1UL << 1)
#define SIE_STIE     (1UL << 5)
#define SIE_SEIE     (1UL << 9)
#define SIP_SSIP     (1UL << 1)

#define SCAUSE_INTERRUPT (1UL << 63)
#define SCAUSE_S_SOFTWARE 1UL
#define SCAUSE_S_TIMER    5UL
#define SCAUSE_S_EXTERNAL 9UL
#define SCAUSE_U_ECALL 8UL

#endif
