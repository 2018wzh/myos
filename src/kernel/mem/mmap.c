#include "method.h"
#include "../arch/method.h"
#include "../lib/method.h"
#include "../lock/method.h"
#include "../proc/method.h"

static spinlock_t region_lock;
static mmap_region_t regions[MMAP_REGION_COUNT];
static bool allocated[MMAP_REGION_COUNT];
static proc_t *owners[MMAP_REGION_COUNT];
static mmap_region_t *free_regions;
static bool initialized;

void mmap_init(void)
{
  if (initialized)
    panic("mmap pool initialized twice");
  spinlock_init(&region_lock, "mmap regions");
  for (uint32 i = MMAP_REGION_COUNT; i != 0;) {
    --i;
    regions[i].index = i;
    regions[i].next = free_regions;
    free_regions = &regions[i];
  }
  initialized = true;
}

mmap_region_t *mmap_region_alloc(void)
{
  if (!initialized)
    panic("mmap pool not initialized");
  spinlock_acquire(&region_lock);
  mmap_region_t *region = free_regions;
  if (region != NULL) {
    free_regions = region->next;
    allocated[region->index] = true;
    owners[region->index] = NULL;
    region->begin = 0;
    region->end = 0;
    region->perm = 0;
    region->next = NULL;
  }
  spinlock_release(&region_lock);
  return region;
}

static bool contains_region(proc_t *proc, mmap_region_t *region)
{
  if (proc == NULL)
    return false;
  uint32 count = 0;
  for (mmap_region_t *p = proc->mmap; p != NULL; p = p->next) {
    if (++count > MMAP_REGION_COUNT)
      panic("mmap cyclic region list");
    if (p == region)
      return true;
  }
  return false;
}

void mmap_region_free(mmap_region_t *region)
{
  uint64 address = (uint64)region;
  uint64 base = (uint64)regions;
  if (!initialized || address < base || address - base >= sizeof(regions) ||
      (address - base) % sizeof(*region) != 0)
    panic("mmap invalid region free");
  uint32 index = (address - base) / sizeof(*region);
  proc_t *current = myproc();
  spinlock_acquire(&region_lock);
  if (region->index != index || !allocated[index])
    panic("mmap double or invalid region free");
  if (contains_region(owners[index], region) ||
      (current != owners[index] && contains_region(current, region)))
    panic("mmap freeing linked region");
  allocated[index] = false;
  owners[index] = NULL;
  region->begin = 0;
  region->end = 0;
  region->perm = 0;
  region->next = free_regions;
  free_regions = region;
  spinlock_release(&region_lock);
}

static void set_owner(mmap_region_t *region, proc_t *proc)
{
  spinlock_acquire(&region_lock);
  owners[region->index] = proc;
  spinlock_release(&region_lock);
}

uint64 uvm_mmap_find(mmap_region_t *head_mmap, uint64 len,
                     mmap_region_t **p_last_mmap, mmap_region_t **p_tmp_mmap)
{
  if (len == 0 || (len & PAGE_MASK) != 0 || len > MMAP_END - MMAP_BEGIN)
    return 0;
  uint64 begin = MMAP_BEGIN;
  mmap_region_t *previous = NULL;
  mmap_region_t *next = head_mmap;
  while (next != NULL) {
    if (len <= next->begin - begin)
      break;
    begin = next->end;
    previous = next;
    next = next->next;
  }
  if (len > MMAP_END - begin)
    return 0;
  if (p_last_mmap != NULL)
    *p_last_mmap = previous;
  if (p_tmp_mmap != NULL)
    *p_tmp_mmap = next;
  return begin;
}

static bool valid_size(uint32 npages)
{
  return npages != 0 && npages <= (MMAP_END - MMAP_BEGIN) / PAGE_SIZE;
}

static bool valid_range(uint64 begin, uint64 len)
{
  return begin >= MMAP_BEGIN && begin < MMAP_END &&
         (begin & PAGE_MASK) == 0 && len <= MMAP_END - begin;
}

/* Staged leaves are known not to overlap the destination. Installing them
 * needs no allocation, including where an existing branch is shared. */
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
      if (level == 0 || !PTE_CHECK(entry) || !PTE_CHECK(target[i]))
        panic("mmap publish overlap");
      publish_pages((pgtbl_t)PTE_TO_PA(target[i]),
                    (pgtbl_t)PTE_TO_PA(entry), level - 1);
    }
  }
}

uint64 uvm_mmap(uint64 begin, uint32 npages, int perm)
{
  proc_t *proc = myproc();
  if (proc == NULL || proc->pgtbl == NULL || !valid_size(npages) ||
      (perm & ~(PTE_R | PTE_W | PTE_X)) != 0 || (perm & PTE_R) == 0)
    return 0;
  uint64 len = (uint64)npages * PAGE_SIZE;
  mmap_region_t *previous = NULL;
  mmap_region_t *next = proc->mmap;
  if (begin == 0) {
    begin = uvm_mmap_find(proc->mmap, len, &previous, &next);
    if (begin == 0)
      return 0;
  } else {
    if (!valid_range(begin, len))
      return 0;
    while (next != NULL && next->begin < begin) {
      previous = next;
      next = next->next;
    }
  }
  uint64 end = begin + len;
  if ((previous != NULL && previous->end > begin) ||
      (next != NULL && end > next->begin))
    return 0;
  for (uint64 va = begin; va < end; va += PAGE_SIZE) {
    pte_t *pte = vm_getpte(proc->pgtbl, va, false);
    if (pte != NULL && (*pte & PTE_V) != 0)
      return 0;
  }

  bool join_left = previous != NULL && previous->end == begin &&
                   previous->perm == perm;
  bool join_right = next != NULL && next->begin == end && next->perm == perm;
  mmap_region_t *region = NULL;
  if (!join_left && !join_right) {
    region = mmap_region_alloc();
    if (region == NULL)
      return 0;
  }
  pgtbl_t staged = (pgtbl_t)pmem_alloc(true);
  if (staged == NULL)
    goto fail;
  for (uint64 va = begin; va < end; va += PAGE_SIZE) {
    uint64 page = pmem_alloc(false);
    if (page == 0)
      goto fail;
    if (vm_mappages(staged, va, page, PAGE_SIZE, perm | PTE_U) < 0) {
      pmem_free(page, false);
      goto fail;
    }
  }
  publish_pages(proc->pgtbl, staged, 2);
  sfence_vma();
  vm_freewalk(staged);

  if (join_left) {
    previous->end = end;
    if (join_right) {
      previous->end = next->end;
      previous->next = next->next;
      next->next = NULL;
      mmap_region_free(next);
    }
  } else if (join_right) {
    next->begin = begin;
  } else {
    region->begin = begin;
    region->end = end;
    region->perm = perm;
    region->next = next;
    set_owner(region, proc);
    if (previous != NULL)
      previous->next = region;
    else
      proc->mmap = region;
  }
  return begin;

fail:
  uvm_destroy_pgtbl(staged);
  if (region != NULL)
    mmap_region_free(region);
  return 0;
}

int uvm_munmap(uint64 begin, uint32 npages)
{
  proc_t *proc = myproc();
  if (proc == NULL || proc->pgtbl == NULL || !valid_size(npages))
    return -1;
  uint64 len = (uint64)npages * PAGE_SIZE;
  if (!valid_range(begin, len))
    return -1;
  uint64 end = begin + len;
  mmap_region_t *previous = NULL;
  mmap_region_t *first = proc->mmap;
  while (first != NULL && first->end <= begin) {
    previous = first;
    first = first->next;
  }

  /* Validate the entire interval before acquiring a split node or unmapping
   * anything. In particular, adjacent regions may have different permissions. */
  mmap_region_t *region = first;
  uint64 cursor = begin;
  while (cursor < end) {
    if (region == NULL || region->begin > cursor || region->end <= cursor)
      return -1;
    uint64 stop = region->end < end ? region->end : end;
    for (; cursor < stop; cursor += PAGE_SIZE) {
      pte_t *pte = vm_getpte(proc->pgtbl, cursor, false);
      if (pte == NULL ||
          PTE_PERMS(*pte) != ((uint64)region->perm | PTE_V | PTE_U))
        return -1;
    }
    region = region->next;
  }

  mmap_region_t *split = NULL;
  if (first->begin < begin && end < first->end) {
    split = mmap_region_alloc();
    if (split == NULL)
      return -1;
    split->begin = end;
    split->end = first->end;
    split->perm = first->perm;
    split->next = first->next;
  }
  for (uint64 va = begin; va < end; va += PAGE_SIZE) {
    uint64 page = PTE_TO_PA(*vm_getpte(proc->pgtbl, va, false));
    vm_unmappages(proc->pgtbl, va, PAGE_SIZE, false);
    sfence_vma();
    pmem_free(page, check_inkernel(page));
  }

  if (split != NULL) {
    set_owner(split, proc);
    first->end = begin;
    first->next = split;
    return 0;
  }
  region = first;
  if (region->begin < begin) {
    region->end = begin;
    previous = region;
    region = region->next;
  }
  while (region != NULL && region->begin < end) {
    if (region->end > end) {
      region->begin = end;
      break;
    }
    mmap_region_t *next = region->next;
    if (previous != NULL)
      previous->next = next;
    else
      proc->mmap = next;
    region->next = NULL;
    mmap_region_free(region);
    region = next;
  }
  return 0;
}
