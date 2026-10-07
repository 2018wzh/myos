#include "sys.h"

/* A third successful hello is the completion signal; failures remain silent. */
int main(void) __attribute__((section(".text.user_entry")));
int main(void)
{
  if (helloworld() != 0 || helloworld() != 0 || __syscall0(255) != -1)
    goto failed;
  volatile unsigned long stack[8];
  for (unsigned long i = 0; i < 8; ++i)
    stack[i] = 0x10203040UL + i;
  register unsigned long saved asm("s1") = 0x13579bdfUL;
  register unsigned long temp asm("t3") = 0x2468ace0UL;
  register unsigned long argument asm("a3") = 0x12345678UL;
  unsigned long sum = 0;
  for (unsigned long i = 0; i < 3000000UL; ++i) {
    asm volatile("" : "+r"(saved), "+r"(temp), "+r"(argument) : : "memory");
    sum += i;
  }
  if (sum != 4499998500000UL || saved != 0x13579bdfUL ||
      temp != 0x2468ace0UL || argument != 0x12345678UL)
    goto failed;
  for (unsigned long i = 0; i < 8; ++i)
    if (stack[i] != 0x10203040UL + i)
      goto failed;
  if (helloworld() != 0)
    goto failed;
  /* A fault must stop this user flow without panicking the kernel. */
  asm volatile("ld t0, 0(zero)" : : : "t0", "memory");
  helloworld();
failed:
  for (;;)
    asm volatile("nop");
}
