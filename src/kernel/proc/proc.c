#include "method.h"
#include "../arch/method.h"
#include "../lib/method.h"
#include "../lock/method.h"
#include "../mem/method.h"
#include "../trap/method.h"
#include "initcode.h"

extern char trampoline[];

cpu_t cpus[MAX_HARTS];
proc_t proczero;

_Static_assert(sizeof(context_t) == 112, "context must match swtch.S");
_Static_assert(sizeof(initcode) <= PAGE_SIZE, "initcode exceeds one page");

cpu_t *mycpu(void)
{
  uint64 id = hart_id();
  if (id >= MAX_HARTS)
    panic("invalid CPU id");
  return &cpus[id];
}

proc_t *myproc(void)
{
  push_off();
  proc_t *proc = mycpu()->proc;
  pop_off();
  return proc;
}

void proc_init(void)
{
  memset(cpus, 0, sizeof(cpus));
  memset(&proczero, 0, sizeof(proczero));
  spinlock_init(&proczero.lock, "proczero");
  proczero.name = "proczero";
  proczero.state = PROC_UNUSED;
  proczero.kstack = KSTACK(0);
}

pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
  pgtbl_t root = (pgtbl_t)pmem_alloc(true);
  if (root == NULL)
    return NULL;
  if (vm_mappages(root, TRAMPOLINE, (uint64)trampoline,
                  PAGE_SIZE, PTE_R | PTE_X) < 0 ||
      vm_mappages(root, TRAPFRAME, trapframe,
                  PAGE_SIZE, PTE_R | PTE_W) < 0) {
    vm_freewalk(root);
    return NULL;
  }
  return root;
}

void proc_make_first(void)
{
  proc_t *proc = &proczero;
  uint64 frame = 0;
  uint64 code = 0;
  uint64 stack = 0;
  pgtbl_t root = NULL;

  spinlock_acquire(&proc->lock);
  if (hart_id() != 0 || proc->state != PROC_UNUSED || mycpu()->proc != NULL)
    panic("invalid first process startup");
  frame = pmem_alloc(true);
  if (frame == 0)
    goto fail;
  root = proc_pgtbl_init(frame);
  if (root == NULL)
    goto fail;
  code = pmem_alloc(false);
  if (code == 0)
    goto fail;
  stack = pmem_alloc(false);
  if (stack == 0)
    goto fail;
  if (vm_mappages(root, USER_BASE, code,
                  PAGE_SIZE, PTE_R | PTE_X | PTE_U) < 0 ||
      vm_mappages(root, USER_STACK, stack,
                  PAGE_SIZE, PTE_R | PTE_W | PTE_U) < 0)
    goto fail;

  memmove((void *)code, initcode, initcode_len);
  asm volatile("fence.i" ::: "memory");
  proc->pgtbl = root;
  proc->trapframe = (user_trapframe_t *)frame;
  proc->trapframe->epc = USER_BASE;
  proc->trapframe->sp = USER_STACK_TOP;
  proc->context.ra = (uint64)proc_return;
  proc->context.sp = proc->kstack + PAGE_SIZE;
  proc->state = PROC_RUNNING;
  cpu_t *cpu = mycpu();
  cpu->proc = proc;
  swtch(&cpu->context, &proc->context);
  panic("first process returned to boot context");

fail:
  vm_freewalk(root);
  if (stack != 0)
    pmem_free(stack, false);
  if (code != 0)
    pmem_free(code, false);
  if (frame != 0)
    pmem_free(frame, true);
  proc->state = PROC_UNUSED;
  spinlock_release(&proc->lock);
  printf("cannot allocate first process\n");
  intr_on();
  for (;;)
    asm volatile("wfi");
}

void proc_return(void)
{
  proc_t *proc = myproc();
  if (proc == NULL || proc->state != PROC_RUNNING ||
      !spinlock_holding(&proc->lock))
    panic("invalid process context handoff");
  spinlock_release(&proc->lock);
  trap_user_return();
}
