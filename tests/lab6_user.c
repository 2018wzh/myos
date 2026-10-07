#include "sys.h"
#ifndef TEST_CHILDREN
#define TEST_CHILDREN 4
#endif

static void require(int ok)
{
  if (!ok) {
    __syscall1(SYS_print_str, (long)"TEST:FAIL processes\n");
    for (;;) __syscall1(SYS_sleep, 1);
  }
}

static void finish(int status) __attribute__((noreturn));
static void finish(int status)
{
  __syscall1(SYS_exit, status);
  for (;;) asm volatile("nop");
}

int main(void) __attribute__((section(".text.user_entry")));
int main(void)
{
  long parent = __syscall0(SYS_getpid);
  long base = __syscall1(SYS_brk, 0);
  require(parent == 1 && base > 0);
  require(__syscall1(SYS_brk, base + 4096) == base + 4096);
  long mapped = __syscall2(SYS_mmap, 0, 4096);
  require(mapped != 0);
  volatile int *heap = (void *)base, *region = (void *)mapped;
  volatile int stack = 55;
  *heap = 33;
  *region = 44;
  long first = __syscall0(SYS_fork);
  require(first >= 0);
  if (first == 0) {
    require(__syscall0(SYS_getpid) != parent &&
            *heap == 33 && *region == 44 && stack == 55);
    *heap = 333;
    *region = 444;
    stack = 555;
    require(__syscall1(SYS_sleep, 30) == 0);
    __syscall1(SYS_print_str, (long)"TEST:CHILD\n");
    finish(1234);
  }
  __syscall1(SYS_print_str, (long)"TEST:BLOCK-BEGIN\n");
  require(__syscall0(SYS_getpid) == parent);
  require(__syscall1(SYS_sleep, 40) == 0);
  __syscall1(SYS_print_str, (long)"TEST:BLOCK-END\n");
  require(*heap == 33 && *region == 44 && stack == 55);
  require(__syscall1(SYS_wait, 1) == -1);
  int status = 0;
  require(__syscall1(SYS_wait, (long)&status) == first && status == 1234);
  require(__syscall1(SYS_wait, 0) == -1);
  long children[TEST_CHILDREN];
  for (int i = 0; i < TEST_CHILDREN; ++i) {
    long child = __syscall0(SYS_fork);
    require(child >= 0);
    if (child == 0) {
      for (int tick = 0; tick < 8; ++tick) {
        require(__syscall0(SYS_getpid) != parent);
        require(__syscall1(SYS_sleep, 1) == 0);
      }
      finish(200 + i);
    }
    children[i] = child;
  }
  unsigned seen = 0;
  for (int n = 0; n < TEST_CHILDREN; ++n) {
    long pid = __syscall1(SYS_wait, (long)&status);
    require(__syscall0(SYS_getpid) == parent);
    int match = -1;
    for (int i = 0; i < TEST_CHILDREN; ++i)
      if (children[i] == pid) match = i;
    require(match >= 0 && !(seen & (1U << match)) && status == 200 + match);
    seen |= 1U << match;
  }
  require(__syscall1(SYS_wait, 0) == -1);
  long child = __syscall0(SYS_fork);
  require(child >= 0);
  if (child == 0) finish(9);
  require(__syscall1(SYS_wait, 0) == child &&
          __syscall1(SYS_wait, 0) == -1);
  child = __syscall0(SYS_fork);
  require(child >= 0);
  if (child == 0) {
    *(volatile int *)0 = 1;
    finish(99);
  }
  require(__syscall1(SYS_wait, (long)&status) == child && status == -1);
  require(__syscall2(SYS_munmap, mapped, 4096) == 0 &&
          __syscall1(SYS_brk, base) == base &&
          __syscall1(SYS_sleep, 0) == 0);
  require(__syscall0(255) == -1);
  __syscall1(SYS_print_str, (long)"TEST:PASS processes\n");
  for (;;) __syscall1(SYS_sleep, 1);
}
