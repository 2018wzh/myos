#include "method.h"
#include "../arch/type.h"
#include "../arch/method.h"
#include "../lib/method.h"
#include "../lock/method.h"
#include "../proc/method.h"

#ifndef TIMER_INTERVAL
#define TIMER_INTERVAL 1000000UL
#endif

_Static_assert(TIMER_INTERVAL > 0, "TIMER_INTERVAL must be positive");

#define CLINT_MTIMECMP(hart) (0x02004000UL + 8UL * (hart))
#define CLINT_MTIME 0x0200bff8UL

typedef struct timer_scratch {
  uint64 slots[5];
} __attribute__((aligned(16))) timer_scratch_t;

static timer_scratch_t timer_scratch[MAX_HARTS];
static spinlock_t ticks_lock;
static uint64 sys_ticks;

void timer_init(void)
{
  uint64 hart;
  hart = r_mhartid();
  if (hart >= MAX_HARTS)
    for (;;)
      asm volatile("wfi");

  volatile uint64 *mtimecmp = (volatile uint64 *)CLINT_MTIMECMP(hart);
  volatile uint64 *mtime = (volatile uint64 *)CLINT_MTIME;
  *mtimecmp = *mtime + TIMER_INTERVAL;

  timer_scratch[hart].slots[3] = (uint64)mtimecmp;
  timer_scratch[hart].slots[4] = TIMER_INTERVAL;
  w_mscratch((uint64)timer_scratch[hart].slots);
  w_mtvec((uint64)timer_vector);
  w_mie(r_mie() | MIE_MTIE);
}

void timer_create(void)
{
  spinlock_init(&ticks_lock, "ticks");
  sys_ticks = 0;
}

void timer_update(void)
{
  spinlock_acquire(&ticks_lock);
  sys_ticks++;
  proc_wakeup(&sys_ticks);
  spinlock_release(&ticks_lock);
}

uint64 timer_get_ticks(void)
{
  spinlock_acquire(&ticks_lock);
  uint64 ticks = sys_ticks;
  spinlock_release(&ticks_lock);
  return ticks;
}

void timer_wait(uint64 ntick)
{
  spinlock_acquire(&ticks_lock);
  uint64 start = sys_ticks;
  while (sys_ticks - start < ntick)
    proc_sleep(&sys_ticks, &ticks_lock);
  spinlock_release(&ticks_lock);
}
