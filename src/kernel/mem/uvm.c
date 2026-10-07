#include "method.h"
#include "../arch/method.h"
#include "../lib/method.h"

static bool is_leaf(pte_t pte)
{
  return (pte & (PTE_R | PTE_W | PTE_X)) != 0;
}

static pte_t *user_pte(pgtbl_t pgtbl, uint64 va, int access)
{
  if (va >= TRAPFRAME)
    return NULL;
  pte_t *pte = vm_getpte(pgtbl, va, false);
  if (pte == NULL || (*pte & (PTE_V | PTE_U)) != (PTE_V | PTE_U) ||
      !is_leaf(*pte) || (*pte & (uint64)access) != (uint64)access ||
      ((*pte & PTE_W) != 0 && (*pte & PTE_R) == 0))
    return NULL;
  return pte;
}

static bool user_range(uint64 begin, uint64 len)
{
  return begin < VA_MAX && len <= VA_MAX - begin;
}

int uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
  if (len == 0)
    return 0;
  if (!user_range(src, len))
    return -1;
  while (len != 0) {
    pte_t *pte = user_pte(pgtbl, src, PTE_R);
    if (pte == NULL)
      return -1;
    uint32 n = PAGE_SIZE - (src & PAGE_MASK);
    if (n > len)
      n = len;
    memmove((void *)dst, (void *)(PTE_TO_PA(*pte) + (src & PAGE_MASK)), n);
    src += n;
    dst += n;
    len -= n;
  }
  return 0;
}

int uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
  if (len == 0)
    return 0;
  if (!user_range(dst, len))
    return -1;
  while (len != 0) {
    pte_t *pte = user_pte(pgtbl, dst, PTE_W);
    if (pte == NULL)
      return -1;
    uint32 n = PAGE_SIZE - (dst & PAGE_MASK);
    if (n > len)
      n = len;
    memmove((void *)(PTE_TO_PA(*pte) + (dst & PAGE_MASK)), (void *)src, n);
    src += n;
    dst += n;
    len -= n;
  }
  return 0;
}

int uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
  while (maxlen != 0) {
    pte_t *pte = user_pte(pgtbl, src, PTE_R);
    if (pte == NULL)
      return -1;
    uint32 n = PAGE_SIZE - (src & PAGE_MASK);
    if (n > maxlen)
      n = maxlen;
    const char *from = (const char *)(PTE_TO_PA(*pte) + (src & PAGE_MASK));
    for (uint32 i = 0; i < n; ++i) {
      char c = from[i];
      *(char *)dst++ = c;
      if (c == '\0')
        return 0;
    }
    src += n;
    maxlen -= n;
  }
  return -1;
}

/* Build new leaves off to the side. The published tree is never used to
 * allocate intermediate tables, so every failed attempt is self-contained. */
static int stage_pages(pgtbl_t staged, pgtbl_t target, pgtbl_t source,
                       uint64 begin, uint64 end)
{
  for (uint64 va = begin; va < end; va += PAGE_SIZE) {
    pte_t *occupied = vm_getpte(target, va, false);
    if (occupied != NULL && (*occupied & PTE_V) != 0)
      return -1;

    pte_t flags = PTE_V | PTE_R | PTE_W | PTE_U;
    pte_t *original = NULL;
    if (source != NULL) {
      original = user_pte(source, va, 0);
      if (original == NULL)
        return -1;
      flags = PTE_FLAGS(*original);
    }
    uint64 page = pmem_alloc(false);
    if (page == 0)
      return -1;
    if (original != NULL)
      memmove((void *)page, (void *)PTE_TO_PA(*original), PAGE_SIZE);
    int perm = flags & (PTE_R | PTE_W | PTE_X | PTE_U);
    if (vm_mappages(staged, va, page, PAGE_SIZE, perm) < 0) {
      pmem_free(page, false);
      return -1;
    }
    *vm_getpte(staged, va, false) = PA_TO_PTE(page) | flags;
  }
  return 0;
}

/* Every destination leaf was checked before this allocation-free commit.
 * Transfer missing subtrees wholesale; shared paths retain their old tables. */
static void publish_pages(pgtbl_t target, pgtbl_t staged, int level)
{
  for (int i = 0; i < 512; ++i) {
    pte_t entry = staged[i];
    if ((entry & PTE_V) == 0)
      continue;
    if ((target[i] & PTE_V) == 0) {
      target[i] = entry;
      staged[i] = 0;
    } else {
      if (level == 0 || is_leaf(entry) || is_leaf(target[i]))
        panic("uvm publish overlap");
      publish_pages((pgtbl_t)PTE_TO_PA(target[i]),
                    (pgtbl_t)PTE_TO_PA(entry), level - 1);
    }
  }
}

static int grow_pages(pgtbl_t pgtbl, uint64 begin, uint64 end)
{
  if (begin == end)
    return 0;
  pgtbl_t staged = (pgtbl_t)pmem_alloc(true);
  if (staged == NULL)
    return -1;
  if (stage_pages(staged, pgtbl, NULL, begin, end) < 0) {
    uvm_destroy_pgtbl(staged);
    return -1;
  }
  publish_pages(pgtbl, staged, 2);
  sfence_vma();
  vm_freewalk(staged);
  return 0;
}

uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{
  if (pgtbl == NULL || cur_heap_top < USER_HEAP_BASE ||
      cur_heap_top > MMAP_BEGIN || len > MMAP_BEGIN - cur_heap_top)
    return (uint64)-1;
  uint64 top = cur_heap_top + len;
  if (grow_pages(pgtbl, PGROUNDUP(cur_heap_top), PGROUNDUP(top)) < 0)
    return (uint64)-1;
  return top;
}

uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{
  if (pgtbl == NULL || cur_heap_top < USER_HEAP_BASE ||
      cur_heap_top > MMAP_BEGIN || len > cur_heap_top - USER_HEAP_BASE)
    return (uint64)-1;
  uint64 top = cur_heap_top - len;
  uint64 begin = PGROUNDUP(top);
  uint64 end = PGROUNDUP(cur_heap_top);
  for (uint64 va = begin; va < end; va += PAGE_SIZE)
    if (user_pte(pgtbl, va, PTE_R | PTE_W) == NULL)
      panic("uvm heap metadata mismatch");
  for (uint64 va = begin; va < end; va += PAGE_SIZE) {
    uint64 page = PTE_TO_PA(*vm_getpte(pgtbl, va, false));
    vm_unmappages(pgtbl, va, PAGE_SIZE, false);
    sfence_vma();
    pmem_free(page, check_inkernel(page));
  }
  return top;
}

int64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_ustack_npage,
                      uint64 fault_addr)
{
  if (pgtbl == NULL ||
      old_ustack_npage > (USER_STACK_TOP - USER_STACK_BOTTOM) / PAGE_SIZE)
    return -1;
  uint64 bottom = USER_STACK_TOP - old_ustack_npage * PAGE_SIZE;
  if (fault_addr < USER_STACK_BOTTOM || fault_addr >= bottom)
    return -1;
  uint64 begin = PGROUNDDOWN(fault_addr);
  if (grow_pages(pgtbl, begin, bottom) < 0)
    return -1;
  return (USER_STACK_TOP - begin) / PAGE_SIZE;
}

static void free_user_leaves(pgtbl_t pgtbl, int level, uint64 base)
{
  for (int i = 0; i < 512; ++i) {
    pte_t entry = pgtbl[i];
    if ((entry & PTE_V) == 0)
      continue;
    uint64 va = base + ((uint64)i << (12 + 9 * level));
    if (is_leaf(entry)) {
      if (level != 0 || va >= VA_MAX)
        panic("uvm invalid leaf");
      pgtbl[i] = 0;
      if (va == TRAMPOLINE || va == TRAPFRAME)
        continue;
      /* Ownership belongs to the address space, even when U was cleared. */
      uint64 page = PTE_TO_PA(entry);
      pmem_free(page, check_inkernel(page));
    } else {
      if (level == 0)
        panic("uvm invalid page table");
      free_user_leaves((pgtbl_t)PTE_TO_PA(entry), level - 1, va);
    }
  }
}

void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
  if (pgtbl == NULL)
    return;
  free_user_leaves(pgtbl, 2, 0);
  vm_freewalk(pgtbl);
}

static bool valid_layout(uint64 heap_top, uint64 ustack_npage,
                         mmap_region_t *mmap)
{
  if (heap_top < USER_HEAP_BASE || heap_top > MMAP_BEGIN ||
      ustack_npage > (USER_STACK_TOP - USER_STACK_BOTTOM) / PAGE_SIZE)
    return false;
  mmap_region_t *previous = NULL;
  uint32 count = 0;
  for (mmap_region_t *region = mmap; region != NULL; region = region->next) {
    if (++count > MMAP_REGION_COUNT || region->begin < MMAP_BEGIN ||
        region->end > MMAP_END || region->begin >= region->end ||
        ((region->begin | region->end) & PAGE_MASK) != 0 ||
        (region->perm & ~(PTE_R | PTE_W | PTE_X)) != 0 ||
        (region->perm & PTE_R) == 0)
      return false;
    if (previous != NULL &&
        (region->begin < previous->end ||
         (region->begin == previous->end && region->perm == previous->perm)))
      return false;
    previous = region;
  }
  return true;
}

int uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top,
                    uint64 ustack_npage, mmap_region_t *mmap)
{
  if (old == NULL || new == NULL || old == new ||
      !valid_layout(heap_top, ustack_npage, mmap))
    return -1;
  pgtbl_t staged = (pgtbl_t)pmem_alloc(true);
  if (staged == NULL)
    return -1;
  if (stage_pages(staged, new, old, USER_BASE, USER_HEAP_BASE) < 0 ||
      stage_pages(staged, new, old, USER_HEAP_BASE, PGROUNDUP(heap_top)) < 0 ||
      stage_pages(staged, new, old, USER_STACK_TOP - ustack_npage * PAGE_SIZE,
                  USER_STACK_TOP) < 0)
    goto fail;
  for (mmap_region_t *region = mmap; region != NULL; region = region->next)
    if (stage_pages(staged, new, old, region->begin, region->end) < 0)
      goto fail;
  publish_pages(new, staged, 2);
  sfence_vma();
  asm volatile("fence.i" ::: "memory");
  vm_freewalk(staged);
  return 0;

fail:
  uvm_destroy_pgtbl(staged);
  return -1;
}
