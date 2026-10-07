#include "method.h"
#include "../arch/method.h"
#include "../proc/method.h"

extern void panic(const char *s) __attribute__((noreturn));

uint64 hart_id(void) { return r_tp(); }

static bool interrupts_enabled(void)
{
  return intr_get();
}

void push_off(void)
{
  bool old = interrupts_enabled();

  intr_off();
  cpu_t *cpu = mycpu();
  if (cpu->noff == (uint32)~0U)
    panic("push_off depth overflow");
  if (cpu->noff == 0)
    cpu->intena = old;
  cpu->noff++;
}

void pop_off(void)
{
  cpu_t *cpu = mycpu();
  if (interrupts_enabled() || cpu->noff == 0)
    panic("pop_off");
  cpu->noff--;
  if (cpu->noff == 0 && cpu->intena)
    intr_on();
}

void spinlock_init(spinlock_t *lk, const char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->owner = ~0UL;
}

bool spinlock_holding(spinlock_t *lk)
{
  return __atomic_load_n(&lk->locked, __ATOMIC_RELAXED) != 0 &&
         __atomic_load_n(&lk->owner, __ATOMIC_RELAXED) == hart_id();
}

void spinlock_acquire(spinlock_t *lk)
{
  push_off();
  if (spinlock_holding(lk))
    panic("acquire");

  while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;
  __sync_synchronize();
  __atomic_store_n(&lk->owner, hart_id(), __ATOMIC_RELAXED);
}

void spinlock_release(spinlock_t *lk)
{
  if (!spinlock_holding(lk))
    panic("release");

  __atomic_store_n(&lk->owner, ~0UL, __ATOMIC_RELAXED);
  __sync_synchronize();
  __sync_lock_release(&lk->locked);
  pop_off();
}
