#include "sys.h"

enum { PAGE_BYTES = 4096 };

int main(void) __attribute__((section(".text.user_entry"), noreturn));

static int demo_memory(void) __attribute__((noinline));

static int demo_memory(void)
{
  volatile unsigned char stack[3 * PAGE_BYTES];
  long base = brk(0);
  if (base < 0 || brk((unsigned long)base + 2 * PAGE_BYTES) != base + 2 * PAGE_BYTES)
    return -1;
  unsigned long area = mmap((void *)0, 2 * PAGE_BYTES);
  if (area == 0) {
    (void)brk((unsigned long)base);
    return -1;
  }

  volatile int *heap = (int *)((unsigned long)base + PAGE_BYTES);
  volatile int *mapped = (int *)(area + PAGE_BYTES);
  *heap = 31;
  *mapped = 73;
  stack[0] = 11;
  stack[sizeof(stack) - 1] = 22;
  int child = fork();
  if (child == 0) {
    if (*heap != 31 || *mapped != 73 || stack[0] != 11 ||
        stack[sizeof(stack) - 1] != 22)
      exit(-1);
    *heap = 32;
    *mapped = 74;
    stack[0] = 12;
    stack[sizeof(stack) - 1] = 23;
    if (sleep(30) != 0 || *heap != 32 || *mapped != 74 ||
        stack[0] != 12 || stack[sizeof(stack) - 1] != 23 ||
        user_printf("child resumed\n") < 0)
      exit(-1);
    exit(1234);
  }

  int result = -1;
  if (child > 0) {
    *heap = 41;
    *mapped = 83;
    stack[0] = 21;
    stack[sizeof(stack) - 1] = 32;
    int progress = 0;
    for (int i = 0; i < 3; ++i) {
      if (sleep(5) < 0 || user_printf("parent progress\n") < 0)
        progress = -1;
    }
    int code = 0;
    int invalid = wait((int *)1);
    int reaped = wait(&code);
    if (progress == 0 && invalid == -1 && reaped == child && code == 1234 &&
        *heap == 41 && *mapped == 83 && stack[0] == 21 &&
        stack[sizeof(stack) - 1] == 32 &&
        user_printf("wait exit\n") == 0 && print_int(code) == 0)
      result = 0;
  }
  if (munmap((void *)area, 2 * PAGE_BYTES) < 0 ||
      brk((unsigned long)base) != base)
    result = -1;
  if (result < 0)
    return -1;

  /* Reuse a freed process slot and discard its exit status deliberately. */
  child = fork();
  if (child == 0)
    exit(0);
  return child > 0 && wait((int *)0) == child ? 0 : -1;
}

static int demo_tree(void)
{
  if (user_printf("level-1\n") < 0)
    return -1;
  int first = fork();
  if (first < 0)
    return -1;
  if (user_printf("level-2\n") < 0) {
    if (first == 0)
      exit(-1);
    (void)wait((int *)0);
    return -1;
  }
  int second = fork();
  if (second < 0) {
    if (first == 0)
      exit(-1);
    (void)wait((int *)0);
    return -1;
  }
  int result = user_printf("level-3\n") < 0 ? -1 : 0;
  if (second == 0)
    exit(result);

  int children = first == 0 ? 1 : 2;
  for (int i = 0; i < children; ++i) {
    int code = 0;
    if (wait(&code) < 0 || code != 0)
      result = -1;
  }
  if (first == 0)
    exit(result);
  return result;
}

int main(void)
{
  int pid = getpid();
  if (pid != 1 || user_printf("init pid\n") < 0 || print_int(pid) < 0 ||
      syscall(0) != -1 || wait((int *)0) != -1 || sleep(0) != 0 ||
      demo_memory() < 0 || demo_tree() < 0 || wait((int *)0) != -1)
    (void)user_printf("lab6 failed\n");
  else
    (void)user_printf("lab6 complete\n");

  /* Stay available to adopt and reap children orphaned by later scenarios. */
  for (;;) {
    while (wait((int *)0) > 0)
      ;
    (void)sleep(1);
  }
}
