#include "lib/method.h"
#include "lock/method.h"
#include "proc/method.h"
#include "trap/method.h"
#include "arch/method.h"
#include "mem/method.h"

static sleeplock_t shared;
static volatile int inside;
static bool contended;
static bool different_pid;
static bool reported;
static int first_pid;
static uint32 calls;
static bool kernel_preempted;
static uint64 held_pages[32768];
static mmap_region_t *held_nodes[MMAP_REGION_COUNT];
static proc_t *held_processes[NPROC];
static bool fork_failures_checked;

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason)
{
  printf("TEST:FAIL %s\n", reason);
  intr_off();
  for (;;) asm volatile("wfi");
}

void kernel_test(void)
{
  sleeplock_init(&shared, "functional contention");
}

uint64 kernel_test_sleep(uint32 duration)
{
  uint64 begin = timer_get_ticks();
  timer_wait(duration);
  if (timer_get_ticks() - begin < duration)
    fail("sleep duration");
  return 0;
}

void kernel_test_yield(void)
{
  if (myproc() != NULL && (r_sstatus() & SSTATUS_SPP) != 0)
    __atomic_store_n(&kernel_preempted, true, __ATOMIC_RELAXED);
}

uint64 kernel_test_fork(void)
{
  if (!fork_failures_checked) {
    fork_failures_checked = true;
    uint64 kernel_before = kern_region.allocable;
    uint64 user_before = user_region.allocable;
    uint32 count = 0;
    while (count < 32768 && (held_pages[count] = pmem_alloc(true)) != 0)
      ++count;
    if (count == 32768 || proc_fork() != -1 || kern_region.allocable != 0 ||
        user_region.allocable != user_before)
      fail("fork kernel exhaustion");
    while (count != 0)
      pmem_free(held_pages[--count], true);
    if (kern_region.allocable != kernel_before)
      fail("fork page recovery");

    count = 0;
    while (count < MMAP_REGION_COUNT &&
           (held_nodes[count] = mmap_region_alloc()) != NULL)
      ++count;
    if (myproc()->mmap == NULL || proc_fork() != -1 ||
        kern_region.allocable != kernel_before || user_region.allocable != user_before)
      fail("fork mmap exhaustion");
    while (count != 0)
      mmap_region_free(held_nodes[--count]);

    count = 0;
    while (count < NPROC && (held_processes[count] = proc_alloc()) != NULL) {
      spinlock_release(&held_processes[count]->lock);
      ++count;
    }
    if (count != NPROC - 1 || proc_fork() != -1)
      fail("fork slot exhaustion");
    while (count != 0) {
      proc_t *process = held_processes[--count];
      spinlock_acquire(&process->lock);
      proc_free(process);
      spinlock_release(&process->lock);
    }
    if (kern_region.allocable != kernel_before || user_region.allocable != user_before)
      fail("fork resource recovery");
    printf("TEST:FORK-FAILURE-PASS\n");
  }
  return (uint64)proc_fork();
}

/* Temporarily substitutes SYS_getpid without changing its public ABI. */
uint64 kernel_test_getpid(void)
{
  proc_t *process = myproc();
  if (process == NULL)
    fail("getpid without current process");
  int pid = process->pid;
  /* A timer must be able to preempt a running system call, before it sleeps. */
  uint64 start = timer_get_ticks();
  while (timer_get_ticks() - start < 2)
    asm volatile("nop");
  spinlock_acquire(&shared.lock);
  if (shared.locked && shared.pid != pid)
    contended = true;
  spinlock_release(&shared.lock);
  sleeplock_acquire(&shared);
  if (!sleeplock_holding(&shared) || __sync_add_and_fetch(&inside, 1) != 1)
    fail("sleep lock exclusivity");
  if (first_pid == 0)
    first_pid = pid;
  else if (pid != first_pid)
    different_pid = true;
  ++calls;
  /* Other runnable processes contend while the owner actually sleeps. */
  timer_wait(2);
  if (!sleeplock_holding(&shared) || __sync_sub_and_fetch(&inside, 1) != 0)
    fail("sleep lock ownership after sleep");
  spinlock_acquire(&shared.lock);
  bool observed = contended;
  spinlock_release(&shared.lock);
  if (!reported && calls >= 3 && different_pid && observed &&
      __atomic_load_n(&kernel_preempted, __ATOMIC_RELAXED)) {
    reported = true;
    printf("TEST:SLEEPLOCK-PASS\n");
  }
  sleeplock_release(&shared);
  if (sleeplock_holding(&shared))
    fail("sleep lock ownership after release");
  return (uint64)pid;
}
