#include "method.h"
#include "../arch/method.h"
#include "../lib/method.h"
#include "../lock/method.h"
#include "../mem/method.h"
#include "../proc/method.h"
#include "../../user/syscall_num.h"

extern char trampoline[];
extern char user_vector[];
extern char user_return[];

void trap_user_handler(void)
{
  uint64 epc = r_sepc();
  w_stvec((uint64)kernel_vector);
  if ((r_sstatus() & SSTATUS_SPP) != 0 || intr_get())
    panic("invalid user trap context");
  proc_t *proc = myproc();
  if (proc == NULL || proc->state != PROC_RUNNING)
    panic("user trap without a running process");
  proc->trapframe->epc = epc;

  uint64 cause = r_scause();
  if (cause == SCAUSE_U_ECALL) {
    proc->trapframe->epc += 4;
    if (proc->trapframe->a7 == SYS_helloworld) {
      printf("%s hello world\n", proc->name);
      proc->trapframe->a0 = 0;
    } else {
      proc->trapframe->a0 = (uint64)-1;
    }
  } else if (interrupt_info() == 0) {
    printf("%s stopped: scause=%x sepc=%x stval=%x\n",
           proc->name, cause, epc, r_stval());
    /* There is no restart or scheduler in this stage. Keep the resources. */
    spinlock_acquire(&proc->lock);
    proc->state = PROC_UNUSED;
    mycpu()->proc = NULL;
    spinlock_release(&proc->lock);
    intr_on();
    for (;;)
      asm volatile("wfi");
  }
  trap_user_return();
}

void trap_user_return(void)
{
  intr_off();
  proc_t *proc = myproc();
  if (proc == NULL || proc->trapframe == NULL || proc->pgtbl == NULL ||
      proc->state != PROC_RUNNING)
    panic("invalid user return context");

  uint64 vector = TRAMPOLINE + (uint64)user_vector - (uint64)trampoline;
  w_stvec(vector);
  proc->trapframe->kernel_satp = r_satp();
  proc->trapframe->kernel_sp = proc->kstack + PAGE_SIZE;
  proc->trapframe->kernel_trap = (uint64)trap_user_handler;
  proc->trapframe->kernel_hartid = hart_id();

  uint64 status = r_sstatus();
  status &= ~SSTATUS_SPP;
  status |= SSTATUS_SPIE;
  w_sstatus(status);
  w_sepc(proc->trapframe->epc);
  uint64 entry = TRAMPOLINE + (uint64)user_return - (uint64)trampoline;
  ((void (*)(uint64, uint64))entry)(TRAPFRAME, MAKE_SATP(proc->pgtbl));
  __builtin_unreachable();
}
