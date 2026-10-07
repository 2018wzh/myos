#ifndef KERNEL_MEM_TYPE_H
#define KERNEL_MEM_TYPE_H

#include "../lib/type.h"
#include "../lock/type.h"

#define PAGE_SIZE 4096UL
#define PGSIZE PAGE_SIZE
#define PAGE_MASK (PAGE_SIZE - 1)
#define PGROUNDUP(x) (((uint64)(x) + PAGE_MASK) & ~PAGE_MASK)
#define PGROUNDDOWN(x) ((uint64)(x) & ~PAGE_MASK)

#define KERNEL_BASE 0x80000000UL
#define PHYSTOP 0x88000000UL
#define UART0 0x10000000UL
#define CLINT 0x02000000UL
#define CLINT_SIZE 0x00010000UL
#define PLIC 0x0c000000UL
#define PLIC_SIZE 0x04000000UL

#define VA_MAX (1UL << 38)
#define TRAMPOLINE (VA_MAX - PAGE_SIZE)
#define TRAPFRAME (TRAMPOLINE - PAGE_SIZE)
#define KSTACK(procid) (TRAMPOLINE - (((uint64)(procid) + 1UL) * 2UL * PGSIZE))
#define USER_BASE PAGE_SIZE
#define USER_HEAP_BASE (USER_BASE + PAGE_SIZE)
#define MMAP_END (TRAPFRAME - 16UL * 256UL * PAGE_SIZE)
#define MMAP_BEGIN (MMAP_END - 64UL * 256UL * PAGE_SIZE)
#define USER_STACK_BOTTOM MMAP_END
#define USER_STACK_TOP TRAPFRAME
#define USER_STACK (USER_STACK_TOP - PAGE_SIZE)
#define MMAP_REGION_COUNT 256U
#define NPROC 64U

#define SATP_SV39 (8UL << 60)
#define MAKE_SATP(pgtbl) (SATP_SV39 | ((uint64)(pgtbl) >> 12))
#define PTE_V (1UL << 0)
#define PTE_R (1UL << 1)
#define PTE_W (1UL << 2)
#define PTE_X (1UL << 3)
#define PTE_U (1UL << 4)
#define PTE_FLAGS(pte) ((pte) & 0x3ffUL)
#define PTE_PERMS(pte) ((pte) & (PTE_V | PTE_R | PTE_W | PTE_X | PTE_U))
#define PA_TO_PTE(pa) ((((uint64)(pa)) >> 12) << 10)
#define PTE_TO_PA(pte) (((pte) >> 10) << 12)
#define VA_TO_VPN(level, va) ((((uint64)(va)) >> (12 + 9 * (level))) & 0x1ff)

// 检查一个PTE是否属于pgtbl
#define PTE_CHECK(pte) (((pte) & (PTE_R | PTE_W | PTE_X)) == 0)

typedef uint64 pte_t;
typedef pte_t *pgtbl_t;

typedef struct mmap_region
{
  uint64 begin;
  uint64 end;
  int perm;
  uint32 index;
  struct mmap_region *next;
} mmap_region_t;

typedef struct free_page
{
  struct free_page *next;
} free_page_t;

typedef struct pmem_region
{
  spinlock_t lock;
  free_page_t *freelist;
  uint64 begin;
  uint64 end;
  uint64 allocable;
  const char *name;
} pmem_region_t;

#endif
