#ifndef KERNEL_PROC_TYPE_H
#define KERNEL_PROC_TYPE_H

#include "../lib/type.h"
#include "../arch/type.h"
#include "../lock/type.h"
#include "../mem/type.h"

typedef struct context {
  uint64 ra;
  uint64 sp;
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
} context_t;

typedef struct user_trapframe {
  uint64 kernel_satp;
  uint64 kernel_sp;
  uint64 kernel_trap;
  uint64 epc;
  uint64 kernel_hartid;
  uint64 ra;
  uint64 sp;
  uint64 gp;
  uint64 tp;
  uint64 t0;
  uint64 t1;
  uint64 t2;
  uint64 s0;
  uint64 s1;
  uint64 a0;
  uint64 a1;
  uint64 a2;
  uint64 a3;
  uint64 a4;
  uint64 a5;
  uint64 a6;
  uint64 a7;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
  uint64 t3;
  uint64 t4;
  uint64 t5;
  uint64 t6;
} user_trapframe_t;

typedef enum proc_state {
  PROC_UNUSED,
  PROC_EMBRYO,
  PROC_SLEEPING,
  PROC_RUNNABLE,
  PROC_RUNNING,
  PROC_ZOMBIE,
} proc_state_t;

typedef struct proc {
  spinlock_t lock;
  proc_state_t state;
  int pid;
  struct proc *parent;
  int exit_code;
  void *sleep_space;
  uint64 kstack;
  pgtbl_t pgtbl;
  user_trapframe_t *trapframe;
  uint64 heap_top;
  uint64 ustack_npage;
  mmap_region_t *mmap;
  context_t context;
  const char *name;
} proc_t;

typedef struct cpu {
  proc_t *proc;
  context_t scheduler;
  uint32 noff;
  bool intena;
} cpu_t;

_Static_assert(sizeof(user_trapframe_t) == 288,
               "user trapframe must match trampoline.S");
_Static_assert(__builtin_offsetof(user_trapframe_t, kernel_satp) == 0,
               "user trapframe kernel_satp offset mismatch");
_Static_assert(__builtin_offsetof(user_trapframe_t, kernel_trap) == 16,
               "user trapframe kernel_trap offset mismatch");
_Static_assert(__builtin_offsetof(user_trapframe_t, epc) == 24,
               "user trapframe epc offset mismatch");
_Static_assert(__builtin_offsetof(user_trapframe_t, a0) == 112,
               "user trapframe a0 offset mismatch");
_Static_assert(__builtin_offsetof(user_trapframe_t, t6) == 280,
               "user trapframe t6 offset mismatch");

#endif
