#include "sys.h"

int main(void) __attribute__((section(".text.user_entry"), noreturn));

int main(void)
{
  long first = helloworld();
  long second = helloworld();
  if (first != 0 || second != 0)
    __builtin_trap();
  for (;;)
    asm volatile("" ::: "memory");
}
