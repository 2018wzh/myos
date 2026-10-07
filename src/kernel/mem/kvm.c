#include "method.h"
#include "../lib/method.h"
#include "../arch/method.h"

extern char KERNEL_DATA[];
extern char ALLOC_END[];
extern char trampoline[];

pgtbl_t kernel_pgtbl;

static bool pte_is_leaf(pte_t pte)
{
  return (pte & (PTE_R | PTE_W | PTE_X)) != 0;
}

/* Only new intermediate links need undo records: leaves are installed last. */
typedef struct table_change {
  pte_t *entry;
  pte_t previous;
} table_change_t;

typedef struct table_changes {
  struct table_changes *previous;
  uint64 used;
  uint64 capacity;
  table_change_t *entries;
} table_changes_t;

static int remember_table(table_changes_t **changes, pte_t *entry)
{
  table_changes_t *head = *changes;
  if (head->used == head->capacity) {
    table_changes_t *next = (table_changes_t *)pmem_alloc(true);
    if (next == NULL)
      return -1;
    next->previous = head;
    next->capacity = (PAGE_SIZE - sizeof(*next)) / sizeof(table_change_t);
    next->entries = (table_change_t *)(next + 1);
    *changes = head = next;
  }
  head->entries[head->used++] = (table_change_t){entry, *entry};
  return 0;
}

static void finish_tables(table_changes_t *changes, bool undo)
{
  while (changes != NULL) {
    table_changes_t *previous = changes->previous;
    if (undo) {
      while (changes->used != 0) {
        table_change_t *change = &changes->entries[--changes->used];
        uint64 page = PTE_TO_PA(*change->entry);
        *change->entry = change->previous;
        pmem_free(page, true);
      }
    }
    /* The first block is on the caller's stack. */
    if (previous != NULL)
      pmem_free((uint64)changes, true);
    changes = previous;
  }
}

static pte_t *walk(pgtbl_t pgtbl, uint64 va, table_changes_t **changes)
{
  if (pgtbl == NULL || va >= VA_MAX)
    return NULL;
  for (int level = 2; level > 0; --level) {
    pte_t *pte = &pgtbl[VA_TO_VPN(level, va)];
    if ((*pte & PTE_V) != 0) {
      if (pte_is_leaf(*pte))
        panic("leaf encountered while walking page table");
    } else {
      if (changes == NULL)
        return NULL;
      uint64 page = pmem_alloc(true);
      if (page == 0)
        return NULL;
      if (remember_table(changes, pte) < 0) {
        pmem_free(page, true);
        return NULL;
      }
      *pte = PA_TO_PTE(page) | PTE_V;
    }
    pgtbl = (pgtbl_t)PTE_TO_PA(*pte);
  }
  return &pgtbl[VA_TO_VPN(0, va)];
}

pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
  if (!alloc)
    return walk(pgtbl, va, NULL);
  table_change_t entries[2];
  table_changes_t first = {NULL, 0, 2, entries};
  table_changes_t *changes = &first;
  pte_t *pte = walk(pgtbl, va, &changes);
  finish_tables(changes, pte == NULL);
  return pte;
}

static bool valid_range(uint64 va, uint64 len)
{
  return (va & PAGE_MASK) == 0 && len != 0 && va < VA_MAX &&
         len <= VA_MAX - va;
}

int vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
  const uint64 pa_limit = 1UL << 56; /* Sv39 has a 44-bit physical page number. */
  if (pgtbl == NULL || !valid_range(va, len) || (pa & PAGE_MASK) != 0 ||
      pa >= pa_limit || (perm & ~(PTE_R | PTE_W | PTE_X | PTE_U)) != 0 ||
      (perm & (PTE_R | PTE_X)) == 0 ||
      ((perm & PTE_W) != 0 && (perm & PTE_R) == 0))
    return -1;

  uint64 size = PGROUNDUP(len);
  if (size > pa_limit - pa)
    return -1;

  /* Reject overlaps before allocating or changing any leaf. */
  for (uint64 offset = 0; offset < size; offset += PAGE_SIZE) {
    pgtbl_t table = pgtbl;
    for (int level = 2; level >= 0; --level) {
      pte_t pte = table[VA_TO_VPN(level, va + offset)];
      if ((pte & PTE_V) == 0)
        break;
      if (level == 0 || pte_is_leaf(pte))
        return -1;
      table = (pgtbl_t)PTE_TO_PA(pte);
    }
  }

  table_change_t entries[16];
  table_changes_t first = {NULL, 0, 16, entries};
  table_changes_t *changes = &first;
  for (uint64 offset = 0; offset < size; offset += PAGE_SIZE) {
    if (walk(pgtbl, va + offset, &changes) == NULL) {
      finish_tables(changes, true);
      return -1;
    }
  }
  for (uint64 offset = 0; offset < size; offset += PAGE_SIZE)
    *walk(pgtbl, va + offset, NULL) = PA_TO_PTE(pa + offset) | perm | PTE_V;
  finish_tables(changes, false);
  return 0;
}

void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
  if (pgtbl == NULL || !valid_range(va, len))
    panic("vm_unmappages arguments");

  uint64 size = PGROUNDUP(len);
  for (uint64 offset = 0; offset < size; offset += PAGE_SIZE) {
    pte_t *pte = vm_getpte(pgtbl, va + offset, false);
    if (pte == NULL || (*pte & PTE_V) == 0 || !pte_is_leaf(*pte))
      panic("vm_unmappages missing mapping");
    uint64 page = PTE_TO_PA(*pte);
    *pte = 0;
    if (freeit)
      pmem_free(page, check_inkernel(page));
  }
}

void vm_freewalk(pgtbl_t pgtbl)
{
  if (pgtbl == NULL)
    return;
  for (int i = 0; i < 512; ++i) {
    pte_t pte = pgtbl[i];
    if ((pte & PTE_V) != 0 && !pte_is_leaf(pte))
      vm_freewalk((pgtbl_t)PTE_TO_PA(pte));
  }
  pmem_free((uint64)pgtbl, true);
}

void kvm_init(void)
{
  pgtbl_t root = (pgtbl_t)pmem_alloc(true);
  uint64 stacks[NPROC] = {0};
  if (root == NULL)
    goto fail;

  uint64 text_end = PGROUNDUP((uint64)KERNEL_DATA);
  if (vm_mappages(root, UART0, UART0, PAGE_SIZE, PTE_R | PTE_W) < 0 ||
      vm_mappages(root, CLINT, CLINT, CLINT_SIZE, PTE_R | PTE_W) < 0 ||
      vm_mappages(root, PLIC, PLIC, PLIC_SIZE, PTE_R | PTE_W) < 0 ||
      vm_mappages(root, KERNEL_BASE, KERNEL_BASE,
                  text_end - KERNEL_BASE, PTE_R | PTE_X) < 0 ||
      vm_mappages(root, text_end, text_end,
                  (uint64)ALLOC_END - text_end, PTE_R | PTE_W) < 0 ||
      vm_mappages(root, TRAMPOLINE, (uint64)trampoline,
                  PAGE_SIZE, PTE_R | PTE_X) < 0)
    goto fail;
  for (uint32 i = 0; i < NPROC; ++i) {
    stacks[i] = pmem_alloc(true);
    if (stacks[i] == 0 ||
        vm_mappages(root, KSTACK(i), stacks[i], PAGE_SIZE,
                    PTE_R | PTE_W) < 0)
      goto fail;
  }
  kernel_pgtbl = root;
  return;

fail:
  vm_freewalk(root);
  for (uint32 i = 0; i < NPROC; ++i)
    if (stacks[i] != 0)
      pmem_free(stacks[i], true);
  panic("cannot construct kernel page table");
}

void kvm_inithart(void)
{
  if (kernel_pgtbl == NULL)
    panic("kernel page table is not initialized");
  sfence_vma();
  w_satp(MAKE_SATP(kernel_pgtbl));
  sfence_vma();
}

static void vm_print_level(pgtbl_t pgtbl, int level)
{
  for (int i = 0; i < 512; ++i) {
    pte_t pte = pgtbl[i];
    if ((pte & PTE_V) == 0)
      continue;
    printf("level %d index %d pte %x pa %x\n", level, i,
           pte, PTE_TO_PA(pte));
    if (level > 0 && !pte_is_leaf(pte))
      vm_print_level((pgtbl_t)PTE_TO_PA(pte), level - 1);
  }
}

void vm_print(pgtbl_t pgtbl)
{
  assert(pgtbl != NULL, "vm_print null page table");
  printf("page table %x\n", (uint64)pgtbl);
  vm_print_level(pgtbl, 2);
}
