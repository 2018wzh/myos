#include "method.h"
#include "../arch/method.h"
#include "../lib/method.h"
#include "../lock/method.h"
#include "../mem/method.h"
#include "../trap/method.h"
#include "initcode.h"

extern char trampoline[];
cpu_t cpus[MAX_HARTS];
proc_t proc_list[NPROC];
proc_t *proczero;
static spinlock_t pid_lock;
static spinlock_t wait_lock;
static int next_pid = 1;

_Static_assert(sizeof(context_t) == 112, "context must match swtch.S");
_Static_assert(sizeof(initcode) > 0, "initcode must not be empty");
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
  proc_t *p = mycpu()->proc;
  pop_off();
  return p;
}

void proc_init(void)
{
  spinlock_init(&pid_lock, "pid");
  spinlock_init(&wait_lock, "wait");
  next_pid = 1;
  proczero = NULL;
  for (uint32 i = 0; i < MAX_HARTS; ++i) {
    cpus[i].proc = NULL;
    memset(&cpus[i].scheduler, 0, sizeof(cpus[i].scheduler));
  }
  for (uint32 i = 0; i < NPROC; ++i) {
    proc_t *p = &proc_list[i];
    memset(p, 0, sizeof(*p));
    spinlock_init(&p->lock, "process");
    p->state = PROC_UNUSED;
    p->kstack = KSTACK(i);
  }
}

pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
  if (trapframe == 0 || (trapframe & PAGE_MASK) != 0)
    panic("invalid trapframe page");
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

void proc_free(proc_t *p)
{
  if (!spinlock_holding(&p->lock))
    panic("proc_free without process lock");
  uvm_destroy_pgtbl(p->pgtbl);
  p->pgtbl = NULL;
  if (p->trapframe != NULL)
    pmem_free((uint64)p->trapframe, true);
  p->trapframe = NULL;
  while (p->mmap != NULL) {
    mmap_region_t *region = p->mmap;
    p->mmap = region->next;
    region->next = NULL;
    mmap_region_free(region);
  }
  p->pid = 0;
  p->parent = NULL;
  p->exit_code = 0;
  p->sleep_space = NULL;
  p->heap_top = 0;
  p->ustack_npage = 0;
  p->name = NULL;
  memset(&p->context, 0, sizeof(p->context));
  p->state = PROC_UNUSED;
}

proc_t *proc_alloc(void)
{
  for (uint32 i = 0; i < NPROC; ++i) {
    proc_t *p = &proc_list[i];
    spinlock_acquire(&p->lock);
    if (p->state != PROC_UNUSED) {
      spinlock_release(&p->lock);
      continue;
    }
    p->state = PROC_EMBRYO;
    spinlock_acquire(&pid_lock);
    p->pid = next_pid++;
    spinlock_release(&pid_lock);
    p->trapframe = (user_trapframe_t *)pmem_alloc(true);
    if (p->trapframe == NULL)
      goto fail;
    p->pgtbl = proc_pgtbl_init((uint64)p->trapframe);
    if (p->pgtbl == NULL)
      goto fail;
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)proc_return;
    p->context.sp = p->kstack + PAGE_SIZE;
    return p;
fail:
    proc_free(p);
    spinlock_release(&p->lock);
    return NULL;
  }
  return NULL;
}

void proc_make_first(void)
{
  if (initcode_len != sizeof(initcode))
    panic("invalid initcode length");
  proc_t *p = proc_alloc();
  if (p == NULL)
    return;
  uint64 code = pmem_alloc(false);
  if (code == 0)
    goto fail;
  if (vm_mappages(p->pgtbl, USER_BASE, code, PAGE_SIZE,
                  PTE_R | PTE_X | PTE_U) < 0) {
    pmem_free(code, false);
    goto fail;
  }
  memmove((void *)code, initcode, initcode_len);
  uint64 stack = pmem_alloc(false);
  if (stack == 0)
    goto fail;
  if (vm_mappages(p->pgtbl, USER_STACK, stack, PAGE_SIZE,
                  PTE_R | PTE_W | PTE_U) < 0) {
    pmem_free(stack, false);
    goto fail;
  }
  p->trapframe->epc = USER_BASE;
  p->trapframe->sp = USER_STACK_TOP;
  p->heap_top = USER_HEAP_BASE;
  p->ustack_npage = 1;
  p->name = "init";
  proczero = p;
  p->state = PROC_RUNNABLE;
  spinlock_release(&p->lock);
  return;
fail:
  proc_free(p);
  spinlock_release(&p->lock);
}

void proc_return(void)
{
  proc_t *p = myproc();
  if (p == NULL || p->state != PROC_RUNNING || !spinlock_holding(&p->lock))
    panic("invalid process context handoff");
  spinlock_release(&p->lock);
  trap_user_return();
}

int proc_fork(void)
{
  proc_t *parent = myproc();
  if (parent == NULL)
    panic("fork without process");
  proc_t *child = proc_alloc();
  if (child == NULL)
    return -1;
  if (uvm_copy_pgtbl(parent->pgtbl, child->pgtbl, parent->heap_top,
                     parent->ustack_npage, parent->mmap) < 0)
    goto fail;
  mmap_region_t **tail = &child->mmap;
  for (mmap_region_t *region = parent->mmap; region != NULL;
       region = region->next) {
    mmap_region_t *copy = mmap_region_alloc();
    if (copy == NULL)
      goto fail;
    copy->begin = region->begin;
    copy->end = region->end;
    copy->perm = region->perm;
    *tail = copy;
    tail = &copy->next;
  }
  *child->trapframe = *parent->trapframe;
  child->trapframe->a0 = 0;
  child->heap_top = parent->heap_top;
  child->ustack_npage = parent->ustack_npage;
  child->name = parent->name;
  int pid = child->pid;
  /* EMBRYO remains unschedulable while we establish the lock order. */
  spinlock_release(&child->lock);
  spinlock_acquire(&wait_lock);
  spinlock_acquire(&child->lock);
  child->parent = parent;
  child->state = PROC_RUNNABLE;
  spinlock_release(&child->lock);
  spinlock_release(&wait_lock);
  return pid;
fail:
  proc_free(child);
  spinlock_release(&child->lock);
  return -1;
}

void proc_sched(void)
{
  proc_t *p = myproc();
  if (p == NULL || !spinlock_holding(&p->lock) || mycpu()->noff != 1 ||
      intr_get() || p->state == PROC_RUNNING)
    panic("invalid proc_sched context");
  bool intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->scheduler);
  mycpu()->intena = intena;
}

void proc_yield(void)
{
  proc_t *p = myproc();
  if (p == NULL)
    return;
  spinlock_acquire(&p->lock);
  p->state = PROC_RUNNABLE;
  proc_sched();
  spinlock_release(&p->lock);
}

void proc_scheduler(void)
{
  cpu_t *cpu = mycpu();
  cpu->proc = NULL;
  for (;;) {
    intr_on();
    for (uint32 i = 0; i < NPROC; ++i) {
      proc_t *p = &proc_list[i];
      spinlock_acquire(&p->lock);
      if (p->state == PROC_RUNNABLE) {
        p->state = PROC_RUNNING;
        cpu->proc = p;
        swtch(&cpu->scheduler, &p->context);
        cpu->proc = NULL;
      }
      spinlock_release(&p->lock);
    }
  }
}

void proc_sleep(void *sleep_space, spinlock_t *lock)
{
  proc_t *p = myproc();
  if (p == NULL || lock == NULL || !spinlock_holding(lock))
    panic("invalid proc_sleep context");
  if (lock != &p->lock) {
    spinlock_acquire(&p->lock);
    spinlock_release(lock);
  }
  p->sleep_space = sleep_space;
  p->state = PROC_SLEEPING;
  proc_sched();
  p->sleep_space = NULL;
  if (lock != &p->lock) {
    spinlock_release(&p->lock);
    spinlock_acquire(lock);
  }
}

void proc_wakeup(void *sleep_space)
{
  proc_t *current = myproc();
  for (uint32 i = 0; i < NPROC; ++i) {
    proc_t *p = &proc_list[i];
    if (p == current)
      continue;
    spinlock_acquire(&p->lock);
    if (p->state == PROC_SLEEPING && p->sleep_space == sleep_space)
      p->state = PROC_RUNNABLE;
    spinlock_release(&p->lock);
  }
}

void proc_try_wakeup(proc_t *p)
{
  if (!spinlock_holding(&p->lock))
    panic("proc_try_wakeup without process lock");
  if (p->state == PROC_SLEEPING)
    p->state = PROC_RUNNABLE;
}

void proc_reparent(proc_t *parent)
{
  if (!spinlock_holding(&wait_lock))
    panic("reparent without wait lock");
  if (parent == proczero)
    return;
  for (uint32 i = 0; i < NPROC; ++i) {
    proc_t *child = &proc_list[i];
    if (child->parent == parent) {
      child->parent = proczero;
      if (proczero != NULL)
        proc_wakeup(proczero);
    }
  }
}

void proc_exit(int exit_code)
{
  proc_t *p = myproc();
  if (p == NULL)
    panic("exit without process");
  spinlock_acquire(&wait_lock);
  proc_reparent(p);
  if (p->parent != NULL)
    proc_wakeup(p->parent);
  spinlock_acquire(&p->lock);
  p->exit_code = exit_code;
  p->state = PROC_ZOMBIE;
  spinlock_release(&wait_lock);
  proc_sched();
  panic("zombie resumed");
}

int proc_wait(uint64 user_addr)
{
  proc_t *parent = myproc();
  if (parent == NULL)
    panic("wait without process");
  spinlock_acquire(&wait_lock);
  for (;;) {
    bool have_children = false;
    for (uint32 i = 0; i < NPROC; ++i) {
      proc_t *child = &proc_list[i];
      if (child->parent != parent || child == proczero)
        continue;
      spinlock_acquire(&child->lock);
      have_children = true;
      if (child->state == PROC_ZOMBIE) {
        int pid = child->pid;
        if (user_addr != 0 &&
            uvm_copyout(parent->pgtbl, user_addr, (uint64)&child->exit_code,
                        sizeof(child->exit_code)) < 0) {
          spinlock_release(&child->lock);
          spinlock_release(&wait_lock);
          return -1;
        }
        proc_free(child);
        spinlock_release(&child->lock);
        spinlock_release(&wait_lock);
        return pid;
      }
      spinlock_release(&child->lock);
    }
    if (!have_children) {
      spinlock_release(&wait_lock);
      return -1;
    }
    proc_sleep(parent, &wait_lock);
  }
}
