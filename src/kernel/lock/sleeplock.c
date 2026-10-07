#include "method.h"
#include "../lib/method.h"
#include "../proc/method.h"

void sleeplock_init(sleeplock_t *lk, const char *name)
{
  spinlock_init(&lk->lock, "sleeplock");
  lk->locked = false;
  lk->pid = 0;
  lk->name = name;
}

bool sleeplock_holding(sleeplock_t *lk)
{
  spinlock_acquire(&lk->lock);
  proc_t *p = myproc();
  bool holding = p != NULL && lk->locked && lk->pid == p->pid;
  spinlock_release(&lk->lock);
  return holding;
}

void sleeplock_acquire(sleeplock_t *lk)
{
  proc_t *p = myproc();
  if (p == NULL)
    panic("sleeplock without process");
  spinlock_acquire(&lk->lock);
  if (lk->locked && lk->pid == p->pid)
    panic("recursive sleeplock");
  while (lk->locked)
    proc_sleep(lk, &lk->lock);
  lk->locked = true;
  lk->pid = p->pid;
  spinlock_release(&lk->lock);
}

void sleeplock_release(sleeplock_t *lk)
{
  spinlock_acquire(&lk->lock);
  proc_t *p = myproc();
  if (p == NULL || !lk->locked || lk->pid != p->pid)
    panic("sleeplock released by non-owner");
  lk->locked = false;
  lk->pid = 0;
  proc_wakeup(lk);
  spinlock_release(&lk->lock);
}
