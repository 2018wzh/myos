#include "sys.h"

enum { PAGE_BYTES = 4096 };

int main(void) __attribute__((section(".text.user_entry"), noreturn));

static int demo_heap(void)
{
  long base = brk(0);
  if (base < 0 || brk((unsigned long)base + 2 * PAGE_BYTES) != base + 2 * PAGE_BYTES)
    return -1;

  /* These five integers straddle two heap pages. */
  int *values = (int *)((unsigned long)base + PAGE_BYTES - 2 * sizeof(int));
  int result = 0;
  if (copyout(values) < 0 || copyin(values, 5) < 0)
    result = -1;
  if (brk((unsigned long)base + 37) != base + 37)
    result = -1;
  if (brk((unsigned long)base) != base)
    result = -1;
  return result;
}

static int demo_stack(void) __attribute__((noinline));

static int demo_stack(void)
{
  volatile unsigned char bytes[3 * PAGE_BYTES];
  /* A distant first access must also allocate all intervening stack pages. */
  bytes[0] = 11;
  bytes[PAGE_BYTES] = 22;
  bytes[sizeof(bytes) - 1] = 33;
  return bytes[0] == 11 && bytes[PAGE_BYTES] == 22 &&
         bytes[sizeof(bytes) - 1] == 33 ? 0 : -1;
}

static int demo_mmap(void)
{
  unsigned long base = mmap((void *)0, 2 * PAGE_BYTES);
  if (base == 0)
    return -1;
  int *values = (int *)(base + PAGE_BYTES - 2 * sizeof(int));
  int result = 0;
  if (values[0] != 0 || values[4] != 0 ||
      copyout(values) < 0 || copyin(values, 5) < 0)
    result = -1;
  if (munmap((void *)base, 2 * PAGE_BYTES) < 0)
    result = -1;
  return result;
}

int main(void)
{
  int values[5];
  if (helloworld() != 0 || copyout(values) < 0 || copyin(values, 5) < 0 ||
      copyinstr("lab5 user copies\n") < 0)
    goto failed;
  for (int i = 0; i < 5; ++i)
    if (values[i] != i + 1)
      goto failed;
  if (demo_heap() < 0 || demo_stack() < 0 || demo_mmap() < 0 ||
      user_printf("lab5 complete\n") < 0)
    goto failed;
  goto done;

failed:
  (void)user_printf("lab5 failed\n");
done:
  for (;;)
    asm volatile("" ::: "memory");
}
