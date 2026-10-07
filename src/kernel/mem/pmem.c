#include "method.h"
#include "../lib/method.h"
#include "../lock/method.h"

extern char ALLOC_BEGIN[];
extern char ALLOC_END[];

pmem_region_t kern_region;
pmem_region_t user_region;

static pmem_region_t *select_region(bool in_kernel)
{
  return in_kernel ? &kern_region : &user_region;
}

static bool page_in_region(const pmem_region_t *region, uint64 page)
{
  return (page & PAGE_MASK) == 0 && page >= region->begin && page < region->end;
}

static void region_init(pmem_region_t *region, const char *name,
                        uint64 begin, uint64 end)
{
  region->name = name;
  region->begin = begin;
  region->end = end;
  region->freelist = NULL;
  region->allocable = 0;
  spinlock_init(&region->lock, name);

  spinlock_acquire(&region->lock);
  for (uint64 page = begin; page < end; page += PAGE_SIZE) {
    memset((void *)page, 1, PAGE_SIZE);
    free_page_t *node = (free_page_t *)page;
    node->next = region->freelist;
    region->freelist = node;
    region->allocable++;
  }
  spinlock_release(&region->lock);
}

bool check_inkernel(uint64 p)
{
  return p >= kern_region.begin && p < kern_region.end;
}

void pmem_init(void)
{
  uint64 begin = PGROUNDUP((uint64)ALLOC_BEGIN);
  uint64 end = PGROUNDDOWN((uint64)ALLOC_END);
  assert(begin < end, "no allocable physical memory");

  uint64 pages = (end - begin) / PAGE_SIZE;
  uint64 split = begin + (pages / 2) * PAGE_SIZE;
  assert(split > begin && split < end, "cannot split physical memory");

  region_init(&kern_region, "kernel pages", begin, split);
  region_init(&user_region, "user pages", split, end);
}

uint64 pmem_alloc(bool in_kernel)
{
  pmem_region_t *region = select_region(in_kernel);
  spinlock_acquire(&region->lock);
  free_page_t *page = region->freelist;
  if (page != NULL) {
    region->freelist = page->next;
    region->allocable--;
  }
  spinlock_release(&region->lock);

  if (page == NULL)
    return 0;
  memset(page, 0, PAGE_SIZE);
  return (uint64)page;
}

void pmem_free(uint64 page, bool in_kernel)
{
  pmem_region_t *region = select_region(in_kernel);
  if (!page_in_region(region, page))
    panic("invalid physical page free");

  free_page_t *node = (free_page_t *)page;

  spinlock_acquire(&region->lock);
  for (free_page_t *p = region->freelist; p != NULL; p = p->next)
    if (p == node)
      panic("double physical page free");
  memset((void *)page, 1, PAGE_SIZE);
  node->next = region->freelist;
  region->freelist = node;
  region->allocable++;
  spinlock_release(&region->lock);
}
