#include "method.h"
#include "../lib/method.h"
#include "../mem/method.h"
#include "../proc/method.h"
#include "../../user/syscall_num.h"

static user_trapframe_t *current_trapframe(void)
{
  proc_t *proc = myproc();
  if (proc == NULL || proc->trapframe == NULL)
    panic("syscall without a trapframe");
  return proc->trapframe;
}

static const syscall_fn_t syscall_table[] = {
  [SYS_brk] = sys_brk,
  [SYS_mmap] = sys_mmap,
  [SYS_munmap] = sys_munmap,
  [SYS_print_str] = sys_print_str,
  [SYS_print_int] = sys_print_int,
  [SYS_getpid] = sys_getpid,
  [SYS_fork] = sys_fork,
  [SYS_wait] = sys_wait,
  [SYS_exit] = sys_exit,
  [SYS_sleep] = sys_sleep,
};

void syscall(void)
{
  user_trapframe_t *frame = current_trapframe();
  uint64 number = frame->a7;
  if (number >= sizeof(syscall_table) / sizeof(syscall_table[0]) ||
      syscall_table[number] == NULL) {
    frame->a0 = (uint64)-1;
    return;
  }
  frame->a0 = syscall_table[number]();
}

uint64 arg_raw(int n)
{
  user_trapframe_t *frame = current_trapframe();
  switch (n) {
  case 0: return frame->a0;
  case 1: return frame->a1;
  case 2: return frame->a2;
  case 3: return frame->a3;
  case 4: return frame->a4;
  case 5: return frame->a5;
  default: panic("invalid syscall argument index");
  }
}

void arg_uint32(int n, uint32 *ip)
{
  if (ip == NULL)
    panic("null syscall argument output");
  *ip = (uint32)arg_raw(n);
}

void arg_uint64(int n, uint64 *ip)
{
  if (ip == NULL)
    panic("null syscall argument output");
  *ip = arg_raw(n);
}

int arg_str(int n, char *buf, int maxlen)
{
  uint64 source = arg_raw(n);
  if (buf == NULL)
    panic("null syscall string buffer");
  if (maxlen <= 0)
    return -1;
  proc_t *proc = myproc();
  if (proc->pgtbl == NULL)
    panic("syscall without a page table");
  return uvm_copyin_str(proc->pgtbl, (uint64)buf, source, (uint32)maxlen);
}
