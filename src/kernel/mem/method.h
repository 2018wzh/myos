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
void kvm_init(void);
void kvm_inithart(void);
void vm_print(pgtbl_t pgtbl);
void vm_freewalk(pgtbl_t pgtbl);
int uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len);
int uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len);
int uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen);
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len);
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len);
int64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_ustack_npage, uint64 fault_addr);
void uvm_destroy_pgtbl(pgtbl_t pgtbl);
int uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint64 ustack_npage, mmap_region_t *mmap);
void mmap_init(void);
mmap_region_t *mmap_region_alloc(void);
void mmap_region_free(mmap_region_t *mmap);
uint64 uvm_mmap_find(mmap_region_t *head_mmap, uint64 len, mmap_region_t **p_last_mmap, mmap_region_t **p_tmp_mmap);
uint64 uvm_mmap(uint64 begin, uint32 npages, int perm);
int uvm_munmap(uint64 begin, uint32 npages);

#endif
