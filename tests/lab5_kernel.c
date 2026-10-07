#include "lib/method.h"
#include "mem/method.h"
#include "proc/method.h"
#include "lock/method.h"
#include "arch/method.h"

#define CHECK(test) do { if (!(test)) fail(__LINE__); } while (0)

static void fail(int line) __attribute__((noreturn));
static void fail(int line)
{
  printf("TEST:FAIL kernel memory line %d\n", line);
  intr_off();
  for (;;) asm volatile("wfi");
}

static pgtbl_t blank(void)
{
  pgtbl_t table = (pgtbl_t)pmem_alloc(true);
  CHECK(table != NULL);
  return table;
}

static uint64 map_user(pgtbl_t table, uint64 va, int perm, uint8 tag)
{
  uint64 physical = pmem_alloc(false);
  CHECK(physical != 0);
  memset((void *)physical, tag, PAGE_SIZE);
  CHECK(vm_mappages(table, va, physical, PAGE_SIZE, perm | PTE_U) == 0);
  return physical;
}

static void copies(void)
{
  uint64 kernel_free = kern_region.allocable, user_free = user_region.allocable;
  pgtbl_t table = blank();
  map_user(table, USER_BASE, PTE_R | PTE_W, 0);
  map_user(table, USER_BASE + PAGE_SIZE, PTE_R | PTE_W, 0);
  char text[] = "abcde", out[16];
  uint64 cross = USER_BASE + PAGE_SIZE - 3;
  CHECK(uvm_copyout(table, cross, (uint64)text, 6) == 0);
  CHECK(uvm_copyin(table, (uint64)out, cross, 6) == 0);
  CHECK(strncmp(out, text, 6) == 0);
  CHECK(uvm_copyin_str(table, (uint64)out, cross, 6) == 0);
  CHECK(strncmp(out, text, 6) == 0);
  CHECK(uvm_copyin_str(table, (uint64)out, cross, 5) == -1);
  CHECK(uvm_copyin_str(table, (uint64)out, cross, 0) == -1);
  CHECK(uvm_copyin(table, (uint64)out, USER_BASE + 2 * PAGE_SIZE, 1) == -1);
  CHECK(uvm_copyout(table, USER_BASE + 2 * PAGE_SIZE, (uint64)text, 1) == -1);
  CHECK(uvm_copyin(table, (uint64)out, VA_MAX - 2, 6) == -1);
  CHECK(uvm_copyout(table, ~0UL - 2, (uint64)text, 6) == -1);
  CHECK(uvm_copyin(table, 0, ~0UL, 0) == 0);
  CHECK(uvm_copyout(table, ~0UL, 0, 0) == 0);

  pte_t *pte = vm_getpte(table, USER_BASE + PAGE_SIZE, false);
  CHECK(pte != NULL);
  pte_t saved = *pte;
  *pte = saved & ~PTE_U;
  CHECK(uvm_copyin(table, (uint64)out, USER_BASE + PAGE_SIZE, 1) == -1);
  CHECK(uvm_copyout(table, USER_BASE + PAGE_SIZE, (uint64)text, 1) == -1);
  CHECK(uvm_copyin_str(table, (uint64)out, cross, 6) == -1);
  *pte = saved & ~PTE_W;
  CHECK(uvm_copyin(table, (uint64)out, USER_BASE + PAGE_SIZE, 1) == 0);
  CHECK(uvm_copyout(table, USER_BASE + PAGE_SIZE, (uint64)text, 1) == -1);
  *pte = (saved & ~(PTE_R | PTE_W)) | PTE_X;
  CHECK(uvm_copyin(table, (uint64)out, USER_BASE + PAGE_SIZE, 1) == -1);
  CHECK(uvm_copyout(table, USER_BASE + PAGE_SIZE, (uint64)text, 1) == -1);
  *pte = saved;
  uvm_destroy_pgtbl(table);
  CHECK(kern_region.allocable == kernel_free && user_region.allocable == user_free);
}

static void cloning(void)
{
  uint64 kernel_free = kern_region.allocable, user_free = user_region.allocable;
  pgtbl_t original = blank(), copy = blank();
  uint64 addresses[4] = {USER_BASE, USER_HEAP_BASE, USER_STACK, MMAP_BEGIN};
  int permissions[4] = {PTE_R | PTE_X, PTE_R | PTE_W,
                        PTE_R | PTE_W, PTE_R | PTE_W};
  for (int i = 0; i < 4; ++i)
    map_user(original, addresses[i], permissions[i], (uint8)(31 + i));
  mmap_region_t region = {0};
  region.begin = MMAP_BEGIN;
  region.end = MMAP_BEGIN + PAGE_SIZE;
  region.perm = PTE_R | PTE_W;
  CHECK(uvm_copy_pgtbl(original, copy, USER_HEAP_BASE + PAGE_SIZE, 1, &region) == 0);
  for (int i = 0; i < 4; ++i) {
    pte_t *a = vm_getpte(original, addresses[i], false);
    pte_t *b = vm_getpte(copy, addresses[i], false);
    CHECK(a != NULL && b != NULL);
    CHECK(PTE_PERMS(*a) == PTE_PERMS(*b));
    CHECK(PTE_TO_PA(*a) != PTE_TO_PA(*b));
    uint8 *source = (void *)PTE_TO_PA(*a), *destination = (void *)PTE_TO_PA(*b);
    for (uint64 offset = 0; offset < PAGE_SIZE; ++offset)
      CHECK(destination[offset] == source[offset]);
    destination[0] = 99;
    CHECK(source[0] == (uint8)(31 + i));
  }
  uvm_destroy_pgtbl(copy);
  uvm_destroy_pgtbl(original);
  CHECK(kern_region.allocable == kernel_free && user_region.allocable == user_free);
}

static mmap_region_t *nodes[MMAP_REGION_COUNT];

static void mapped_regions(void)
{
  uint64 kernel_free = kern_region.allocable, user_free = user_region.allocable;
  for (uint32 i = 0; i < MMAP_REGION_COUNT; ++i) {
    nodes[i] = mmap_region_alloc();
    CHECK(nodes[i] != NULL);
    CHECK(nodes[i]->begin == 0 && nodes[i]->end == 0 && nodes[i]->next == NULL);
    for (uint32 j = 0; j < i; ++j)
      CHECK(nodes[j] != nodes[i] && nodes[j]->index != nodes[i]->index);
  }
  CHECK(mmap_region_alloc() == NULL);
  for (uint32 i = 0; i < MMAP_REGION_COUNT; ++i)
    mmap_region_free(nodes[i]);

  proc_t temporary;
  memset(&temporary, 0, sizeof temporary);
  temporary.pgtbl = blank();
  temporary.heap_top = USER_HEAP_BASE;
  temporary.ustack_npage = 1;
  cpu_t *cpu = mycpu();
  proc_t *previous = cpu->proc;
  CHECK(previous == NULL);
  cpu->proc = &temporary;
  uint64 begin = uvm_mmap(0, 3, PTE_R | PTE_W);
  CHECK(begin == MMAP_BEGIN);
  CHECK(uvm_mmap(begin + 3 * PAGE_SIZE, 1, PTE_R | PTE_W) == begin + 3 * PAGE_SIZE);
  CHECK(temporary.mmap != NULL && temporary.mmap->begin == begin &&
        temporary.mmap->end == begin + 4 * PAGE_SIZE && temporary.mmap->next == NULL);
  CHECK(uvm_mmap(begin, 1, PTE_R) == 0);
  CHECK(uvm_mmap(0, 0, PTE_R) == 0);
  CHECK(uvm_mmap(0, ~0U, PTE_R) == 0);
  CHECK(uvm_mmap(begin + 1, 1, PTE_R) == 0);
  CHECK(uvm_mmap(0, 1, PTE_W) == 0);
  CHECK(uvm_munmap(begin + PAGE_SIZE, 1) == 0);
  CHECK(temporary.mmap->end == begin + PAGE_SIZE &&
        temporary.mmap->next != NULL &&
        temporary.mmap->next->begin == begin + 2 * PAGE_SIZE);
  CHECK(uvm_munmap(begin, 3) == -1);
  CHECK(uvm_mmap(begin + PAGE_SIZE, 1, PTE_R | PTE_W) == begin + PAGE_SIZE);
  CHECK(temporary.mmap->end == begin + 4 * PAGE_SIZE && temporary.mmap->next == NULL);

  uint32 count = 0;
  while (count < MMAP_REGION_COUNT && (nodes[count] = mmap_region_alloc()) != NULL)
    ++count;
  CHECK(count == MMAP_REGION_COUNT - 1);
  CHECK(uvm_munmap(begin + PAGE_SIZE, 1) == -1);
  CHECK(temporary.mmap->end == begin + 4 * PAGE_SIZE && temporary.mmap->next == NULL);
  pte_t *middle = vm_getpte(temporary.pgtbl, begin + PAGE_SIZE, false);
  CHECK(middle != NULL && (*middle & PTE_V) != 0);
  for (uint32 i = 0; i < count; ++i)
    mmap_region_free(nodes[i]);
  CHECK(uvm_munmap(begin, 4) == 0 && temporary.mmap == NULL);
  CHECK(uvm_munmap(begin, 1) == -1);
  cpu->proc = previous;
  uvm_destroy_pgtbl(temporary.pgtbl);
  CHECK(kern_region.allocable == kernel_free && user_region.allocable == user_free);
}

static void allocation_failure(void)
{
  uint64 kernel_free = kern_region.allocable, user_free = user_region.allocable;
  pgtbl_t table = blank();
  uint64 physical = map_user(table, USER_BASE, PTE_R | PTE_W, 73);
  pte_t before = *vm_getpte(table, USER_BASE, false);
  uint64 chain = 0, page;
  while ((page = pmem_alloc(true)) != 0) {
    *(uint64 *)page = chain;
    chain = page;
  }
  CHECK(chain != 0 && kern_region.allocable == 0);
  page = chain;
  chain = *(uint64 *)page;
  pmem_free(page, true);
  CHECK(kern_region.allocable == 1);
  /* A new VPN2 branch needs two table pages: the second allocation must fail. */
  CHECK(vm_mappages(table, 1UL << 30, physical, PAGE_SIZE, PTE_R | PTE_W) == -1);
  CHECK(kern_region.allocable == 1 && table[1] == 0);
  CHECK(*vm_getpte(table, USER_BASE, false) == before);
  CHECK(*(uint8 *)physical == 73);
  while (chain != 0) {
    page = chain;
    chain = *(uint64 *)page;
    pmem_free(page, true);
  }
  uvm_destroy_pgtbl(table);
  CHECK(kern_region.allocable == kernel_free && user_region.allocable == user_free);
}

void kernel_test(void)
{
  /* Before readiness publication, this hart is the only allocator client. */
  push_off();
  copies();
  cloning();
  mapped_regions();
  allocation_failure();
  pop_off();
  printf("TEST:KERNEL-PASS memory\n");
}
