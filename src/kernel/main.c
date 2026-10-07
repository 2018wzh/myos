#include "lib/method.h"
#include "lock/method.h"
#include "mem/method.h"
#include "proc/method.h"
#include "trap/method.h"

static uint32 kernel_ready;

void init(void)
{
  print_init();
  pmem_init();
  kvm_init();
  mmap_init();
  proc_init();
}

int main(void)
{
  uint64 id = hart_id();

  if (id == 0)
  {
    init();
    trap_kernel_init();
    proc_make_first();
    __atomic_store_n(&kernel_ready, 1, __ATOMIC_RELEASE);
  }
  else
  {
    while (__atomic_load_n(&kernel_ready, __ATOMIC_ACQUIRE) == 0)
      ;
  }
  kvm_inithart();
  trap_kernel_inithart();
  proc_scheduler();
}
