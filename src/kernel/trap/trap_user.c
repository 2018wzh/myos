#include "method.h"
#include "../arch/method.h"
#include "../lib/method.h"
#include "../lock/method.h"
#include "../mem/method.h"
#include "../proc/method.h"
#include "../syscall/method.h"

extern char trampoline[];
extern char user_vector[];
extern char user_return[];

static void stop_user(proc_t *proc, uint64 cause, uint64 epc, uint64 fault)
  __attribute__((noreturn));

static void stop_user(proc_t *proc, uint64 cause, uint64 epc, uint64 fault)
{
  printf("%s stopped: scause=%x sepc=%x stval=%x\n",
         proc->name, cause, epc, fault);
  /* No scheduler or exit reclamation yet; this kernel stack is still in use. */
  spinlock_acquire(&proc->lock);
  proc->state = PROC_UNUSED;
  mycpu()->proc = NULL;
  spinlock_release(&proc->lock);
  intr_on();
  for (;;)
    asm volatile("wfi");
}

void trap_user_handler(void)
{
  uint64 epc = r_sepc();
  w_stvec((uint64)kernel_vector);
  if ((r_sstatus() & SSTATUS_SPP) != 0 || intr_get())
    panic("invalid user trap context");
  proc_t *proc = myproc();
  if (proc == NULL || proc->state != PROC_RUNNING || proc->trapframe == NULL ||
      proc->pgtbl == NULL || proc->kstack == 0)
    panic("user trap without a running process");
  proc->trapframe->epc = epc;

  uint64 cause = r_scause();
  if (cause == SCAUSE_U_ECALL) {
    proc->trapframe->epc += 4;
    syscall();
  } else if (cause == SCAUSE_LOAD_PAGE_FAULT || cause == SCAUSE_STORE_PAGE_FAULT) {
    uint64 fault = r_stval();
    int64 pages = uvm_ustack_grow(proc->pgtbl, proc->ustack_npage, fault);
    if (pages < 0)
      stop_user(proc, cause, epc, fault);
    proc->ustack_npage = (uint64)pages;
  } else if (interrupt_info() == 0) {
    stop_user(proc, cause, epc, r_stval());
  }
  trap_user_return();
}

void trap_user_return(void)
{
  intr_off();
  proc_t *proc = myproc();
  if (proc == NULL || proc->trapframe == NULL || proc->pgtbl == NULL ||
      proc->state != PROC_RUNNING || proc->kstack == 0)
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
