#include "sys.h"

static void require(int ok)
{
  if (!ok) {
    __syscall1(SYS_printf, (long)"TEST:FAIL user memory\n");
    for (;;) asm volatile("nop");
  }
}

static int jump_stack(void) __attribute__((noinline));
static int jump_stack(void)
{
  volatile unsigned char data[12288];
  data[0] = 11;
  data[4096] = 22;
  data[8192] = 33;
  data[12287] = 44;
  return data[0] == 11 && data[4096] == 22 &&
         data[8192] == 33 && data[12287] == 44;
}

int main(void) __attribute__((section(".text.user_entry")));
int main(void)
{
  long base = __syscall1(SYS_brk, 0);
  require(base > 0 && __syscall1(SYS_brk, base + 8192 + 13) == base + 8192 + 13);
  volatile unsigned char *heap = (void *)base;
  require(heap[0] == 0 && heap[8192 + 12] == 0);
  heap[0] = 91;
  heap[8192 + 12] = 92;
  require(heap[0] == 91 && heap[8192 + 12] == 92);
  require(__syscall1(SYS_brk, base + 17) == base + 17 && heap[0] == 91);
  require(__syscall2(SYS_copyin, base + 4096, 1) == -1);
  require(__syscall1(SYS_brk, base - 1) == -1 &&
          __syscall1(SYS_brk, 0) == base + 17);
  require(jump_stack());
  long mapped = __syscall2(SYS_mmap, 0, 8192);
  require(mapped != 0);
  volatile unsigned char *bytes = (void *)mapped;
  require(bytes[0] == 0 && bytes[8191] == 0);
  bytes[0] = 71;
  bytes[8191] = 72;
  require(bytes[0] == 71 && bytes[8191] == 72);
  require(__syscall2(SYS_mmap, mapped, 4096) == 0);
  require(__syscall2(SYS_mmap, 0, 0) == 0 &&
          __syscall2(SYS_mmap, 0, 4097) == 0 &&
          __syscall2(SYS_mmap, 0, 1UL << 32) == 0);
  require(__syscall2(SYS_munmap, mapped + 1, 4096) == -1);
  require(__syscall2(SYS_munmap, mapped, 8192) == 0 &&
          __syscall2(SYS_munmap, mapped, 4096) == -1);
  require(__syscall0(255) == -1);
  require(__syscall1(SYS_printf, (long)"TEST:PASS user memory\n") == 0);
  for (;;) asm volatile("nop");
}
