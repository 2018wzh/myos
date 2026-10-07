#ifndef KERNEL_MEM_METHOD_H
#define KERNEL_MEM_METHOD_H

#include "type.h"

extern pmem_region_t kern_region;
extern pmem_region_t user_region;
extern pgtbl_t kernel_pgtbl;

bool check_inkernel(uint64 p);
void pmem_init(void);
uint64 pmem_alloc(bool in_kernel);
void pmem_free(uint64 page, bool in_kernel);

pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc);
int vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm);
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit);
/* Destroy an inactive hierarchy, retaining ownership of all mapped pages. */
void vm_freewalk(pgtbl_t pgtbl);
void kvm_init(void);
void kvm_inithart(void);
void vm_print(pgtbl_t pgtbl);

#endif
