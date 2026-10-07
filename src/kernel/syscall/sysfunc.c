#include "method.h"
#include "../lib/method.h"
#include "../mem/method.h"
#include "../proc/method.h"

static proc_t *current_proc(void)
{
  proc_t *proc = myproc();
  if (proc == NULL || proc->trapframe == NULL || proc->pgtbl == NULL)
    panic("invalid syscall process context");
  return proc;
}

uint64 sys_helloworld(void)
{
  printf("%s hello world\n", current_proc()->name);
  return 0;
}

uint64 sys_copyin(void)
{
  proc_t *proc = current_proc();
  uint64 count = arg_raw(1);
  int values[64];
  if (count == 0 || count > 64)
    return (uint64)-1;
  if (uvm_copyin(proc->pgtbl, (uint64)values, arg_raw(0),
                 (uint32)count * sizeof(values[0])) < 0)
    return (uint64)-1;
  for (uint32 i = 0; i < (uint32)count; ++i)
    printf("%d%c", values[i], i + 1 == count ? '\n' : ' ');
  return 0;
}

uint64 sys_copyout(void)
{
  proc_t *proc = current_proc();
  const int values[] = {1, 2, 3, 4, 5};
  return (uint64)uvm_copyout(proc->pgtbl, arg_raw(0), (uint64)values,
                             sizeof(values));
}

static uint64 print_user_string(void)
{
  char buffer[256];
  if (arg_str(0, buffer, sizeof(buffer)) < 0)
    return (uint64)-1;
  printf("%s", buffer);
  return 0;
}

uint64 sys_copyinstr(void)
{
  return print_user_string();
}

uint64 sys_printf(void)
{
  return print_user_string();
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
  if (len == 0 || len > 0xffffffffUL || (len & PAGE_MASK) != 0)
    return 0;
  return uvm_mmap(arg_raw(0), (uint32)(len / PAGE_SIZE), PTE_R | PTE_W);
}

uint64 sys_munmap(void)
{
  uint64 len = arg_raw(1);
  if (len == 0 || len > 0xffffffffUL || (len & PAGE_MASK) != 0)
    return (uint64)-1;
  return (uint64)uvm_munmap(arg_raw(0), (uint32)(len / PAGE_SIZE));
}
