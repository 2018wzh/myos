#include "method.h"
#include "../lib/method.h"
#include "../mem/method.h"
#include "../proc/method.h"
#include "../trap/method.h"

static proc_t *current_proc(void)
{
  proc_t *proc = myproc();
  if (proc == NULL || proc->trapframe == NULL || proc->pgtbl == NULL)
    panic("invalid syscall process context");
  return proc;
}

uint64 sys_print_str(void)
{
  char buffer[256];
  if (arg_str(0, buffer, sizeof(buffer)) < 0)
    return (uint64)-1;
  printf("%s", buffer);
  return 0;
}

uint64 sys_brk(void)
{
  proc_t *proc = current_proc();
  uint64 requested = arg_raw(0);
  uint64 current = proc->heap_top;
  if (requested == 0 || requested == current)
    return current;
  if (requested < USER_HEAP_BASE || requested > MMAP_BEGIN)
    return (uint64)-1;

  uint64 change = requested > current ? requested - current : current - requested;
  if (change > 0xffffffffUL)
    return (uint64)-1;
  uint64 top = requested > current
    ? uvm_heap_grow(proc->pgtbl, current, (uint32)change)
    : uvm_heap_ungrow(proc->pgtbl, current, (uint32)change);
  if (top != (uint64)-1)
    proc->heap_top = top;
  return top;
}

uint64 sys_mmap(void)
{
  uint64 len = arg_raw(1);
  if (len == 0 || len > MMAP_END - MMAP_BEGIN || (len & PAGE_MASK) != 0)
    return 0;
  return uvm_mmap(arg_raw(0), (uint32)(len / PAGE_SIZE), PTE_R | PTE_W);
}

uint64 sys_munmap(void)
{
  uint64 len = arg_raw(1);
  if (len == 0 || len > MMAP_END - MMAP_BEGIN || (len & PAGE_MASK) != 0)
    return (uint64)-1;
  return (uint64)uvm_munmap(arg_raw(0), (uint32)(len / PAGE_SIZE));
}

uint64 sys_print_int(void)
{
  uint32 value;
  arg_uint32(0, &value);
  printf("%d\n", (int)value);
  return 0;
}

uint64 sys_getpid(void)
{
  return current_proc()->pid;
}

uint64 sys_fork(void)
{
  return (uint64)proc_fork();
}

uint64 sys_wait(void)
{
  return (uint64)proc_wait(arg_raw(0));
}

uint64 sys_exit(void)
{
  uint32 code;
  arg_uint32(0, &code);
  proc_exit((int)code);
}

uint64 sys_sleep(void)
{
  uint32 ntick;
  arg_uint32(0, &ntick);
  timer_wait(ntick);
  return 0;
}
