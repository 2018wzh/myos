#include "method.h"
#include "../arch/method.h"

extern void panic(const char *s) __attribute__((noreturn));

static uint32 interrupt_depth[MAX_HARTS];
static bool interrupt_was_enabled[MAX_HARTS];

uint64 hart_id(void) { return r_tp(); }

static bool interrupts_enabled(void)
{
  return intr_get();
}

void push_off(void)
{
  uint64 id = hart_id();
  bool old = interrupts_enabled();

  intr_off();
  if (id >= MAX_HARTS)
    panic("hart id exceeds MAX_HARTS");
  if (interrupt_depth[id] == (uint32)~0U)
    panic("push_off depth overflow");
  if (interrupt_depth[id] == 0)
    interrupt_was_enabled[id] = old;
  interrupt_depth[id]++;
}

void pop_off(void)
{
  uint64 id = hart_id();

  if (id >= MAX_HARTS || interrupts_enabled() || interrupt_depth[id] == 0)
    panic("pop_off");
  interrupt_depth[id]--;
  if (interrupt_depth[id] == 0 && interrupt_was_enabled[id])
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
