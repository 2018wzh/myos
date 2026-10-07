#include "method.h"
#include "../arch/type.h"
#include "../arch/method.h"
#include "../lib/method.h"

void trap_kernel_init(void)
{
  w_stvec((uint64)kernel_vector);
  timer_create();
  plic_init();
  uart_enable_rx_interrupt();
}

void trap_kernel_inithart(void)
{
  w_stvec((uint64)kernel_vector);
  plic_inithart();
  w_sie(r_sie() | SIE_SSIE | SIE_SEIE);
  intr_on();
}

void timer_interrupt_handler(void)
{
  timer_update();
}

void external_interrupt_handler(void)
{
  int irq = plic_claim();

  if (irq == UART0_IRQ)
  {
    uart_intr();
  }
  else if (irq != 0)
  {
    printf("unexpected PLIC irq %d\n", irq);
  }

  if (irq != 0)
    plic_complete(irq);
}

int interrupt_info(void)
{
  uint64 scause = r_scause();

  if ((scause & SCAUSE_INTERRUPT) != 0)
  {
    uint64 code = scause & ~SCAUSE_INTERRUPT;
    if (code == SCAUSE_S_SOFTWARE)
    {
      w_sip(r_sip() & ~SIP_SSIP);
      timer_interrupt_handler();
      return 1;
    }
    if (code == SCAUSE_S_EXTERNAL)
    {
      external_interrupt_handler();
      return 2;
    }
  }
  return 0;
}

void trap_kernel_handler(void)
{
  if ((r_sstatus() & SSTATUS_SPP) == 0 || intr_get())
    panic("invalid kernel trap context");
  if (interrupt_info() != 0) {
    if (intr_get())
      panic("kernel trap enabled interrupts");
    return;
  }

  printf("unexpected kernel trap: scause=%x sepc=%x stval=%x\n",
         r_scause(), r_sepc(), r_stval());
  panic("unexpected kernel trap");
}
