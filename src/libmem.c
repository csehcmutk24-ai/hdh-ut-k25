/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  int inc_sz=0;

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->krnl->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->krnl->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
 
    *alloc_addr = rgnode.rg_start;

    // phusccea
#ifdef IODUMP
    printf("ALLOC: Successfully acquired from freerg_list | PID=%d | rgid=%d | size=%d | addr=%08x\n", caller->pid, rgid, size, *alloc_addr);
#endif

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  /*Attempt to increate limit to get space */
#ifdef MM64
  inc_sz = (uint32_t)(size / (int)PAGING64_PAGESZ);
  inc_sz = inc_sz + 1;
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif
  int old_sbrk;
  inc_sz = inc_sz + 1;

  old_sbrk = cur_vma->sbrk;

  /* TODO INCREASE THE LIMIT
   * SYSCALL 1 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
#ifdef MM64
  regs.a3 = size;
#else
  regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif
  _syscall(caller->krnl, caller->pid, 17, &regs); /* SYSCALL 17 sys_memmap */

  /*Successful increase limit */
  caller->krnl->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->krnl->mm->symrgtbl[rgid].rg_end = old_sbrk + size;

  *alloc_addr = old_sbrk;

// phusccea
#ifdef IODUMP
  printf("ALLOC: VMA limit increased successfully | PID=%d | rgid=%d | size=%d | addr=%08x\n", caller->pid, rgid, size, *alloc_addr);
#endif

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->krnl->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->krnl->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }
//#ifdef IODUMP
//  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
//#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    return -1;
  }
//printf("%s:%d\n",__func__,__LINE__);
//#ifdef IODUMP
//  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
//#endif
  return 0;//val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
// checkthinh
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{

  uint32_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    addr_t vicpgn, swpfpn;
    addr_t vicfpn;
    uint32_t vicpte;

    /* TODO Initialize the target frame storing our variable */
    addr_t tgtfpn = PAGING_SWP(pte);

    /* TODO: Play with your paging theory here */
    /* Find victim page */
    if (find_victim_page(caller->krnl->mm, &vicpgn) == -1)
    {
      return -1;
    }

    /* Get victim page's PTE to find its frame */
    vicpte = pte_get_entry(caller, vicpgn);
    vicfpn = PAGING_FPN(vicpte);

    /* Get free frame in MEMSWP */
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
    {
      return -1;
    }

    /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/

    /* TODO copy victim frame to swap
     * SWP(vicfpn <--> swpfpn)
     */
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);

    /* Copy target frame from swap to RAM: target SWAP -> RAM (reuse victim's frame) */
    __swap_cp_page(caller->krnl->active_mswp, tgtfpn, caller->krnl->mram, vicfpn);

    /* Update page table */
    /* Mark victim page as swapped out */
    pte_set_swap(caller, vicpgn, 0, swpfpn);

    /* Update its online status of the target page - now in RAM at vicfpn */
    pte_set_fpn(caller, pgn, vicfpn);

    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(caller, pgn));

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
// checkthinh
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  /* TODO
   *  MEMPHY_read(caller->krnl->mram, phyaddr, data);
   *  MEMPHY READ
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */
  MEMPHY_read(caller->krnl->mram, phyaddr, data);

  return 0;
}
/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
// checkthinh
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  /* TODO
   *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
   *  MEMPHY WRITE with SYSMEM_IO_WRITE
   * SYSCALL 17 sys_memmap
   */
  MEMPHY_write(caller->krnl->mram, phyaddr, value);

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
// checkthinh
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  /* TODO Invalid memory identify */
  if (currg == NULL || cur_vma == NULL)
    return -1;

  /* Check if the offset is within the allocated region */
  if (currg->rg_start + offset >= currg->rg_end)
    return -1;

  pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller);

  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
//printf("%s:%d\n",__func__,__LINE__);
  int val = __read(proc, 0, source, offset, &data);

  *destination = data;
//#ifdef IODUMP
//  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
//#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
//#ifdef IODUMP
//  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
//#endif

  return val;
}


/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
//addr_t  addr;
//int val = __kmalloc(caller, -1, reg_index, size, &addr);
  addr_t addr;
  int val = __kmalloc(caller, -1, reg_index, size, &addr);

  /* TODO: provide OS kmem allocation validation
   */
  if (val < 0) {
    return -1;
  }
  caller->regs[reg_index] = addr;

  return 0;
}


/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /* TODO: provide OS kernel memory allocation
   *       update krnl_pgd for OS kernel level management */

  //struct krnl_t *krnl = caller->krnl;
  //krnl->symrgtbl...
  //krnl->krnl_pgd ...

  if (caller == NULL || alloc_addr == NULL) {
    return (addr_t)-1;
  }

#ifdef MM64
  int num_pages = (size + PAGING64_PAGESZ - 1) / PAGING64_PAGESZ;
#else
  int num_pages = (size + PAGING_PAGESZ - 1) / PAGING_PAGESZ;
#endif
  if (num_pages <= 0) num_pages = 1;

  addr_t first_fpn = 0;
  addr_t fpn_list[1024];
  int got = 0;
  for (int i = 0; i < num_pages; i++) {
    addr_t fpn;
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) != 0) {
      break;
    }
    if (i == 0) first_fpn = fpn;
    fpn_list[got] = fpn;
    got++;
  }

  if (got < num_pages) {
    for (int i = 0; i < got; i++) {
      MEMPHY_put_freefp(caller->krnl->mram, fpn_list[i]);
    }
    return (addr_t)-1;
  }

#ifdef MM64
  addr_t base = first_fpn * PAGING64_PAGESZ;
#else
  addr_t base = first_fpn * PAGING_PAGESZ;
#endif

  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ) {
    caller->krnl->mm->symrgtbl[rgid].rg_start = base;
    caller->krnl->mm->symrgtbl[rgid].rg_end = base + size;
  }

#ifdef MM64
  for (int i = 0; i < num_pages; i++) {
    addr_t vaddr = base + i * PAGING64_PAGESZ;
    addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;
    get_pd_from_address(vaddr, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

    if (caller->krnl->krnl_pgd != NULL) {
      addr_t *p4d_table = (addr_t *)caller->krnl->krnl_pgd[pgd_idx];
      if (p4d_table != NULL) {
        addr_t *pud_table = (addr_t *)p4d_table[p4d_idx];
        if (pud_table != NULL) {
          addr_t *pmd_table = (addr_t *)pud_table[pud_idx];
          if (pmd_table != NULL) {
            addr_t *pt_table = (addr_t *)pmd_table[pmd_idx];
            if (pt_table != NULL) {
              addr_t *pte_ptr = &pt_table[pt_idx];
              SETBIT(*pte_ptr, PAGING_PTE_PRESENT_MASK);
              CLRBIT(*pte_ptr, PAGING_PTE_SWAPPED_MASK);
              SETVAL(*pte_ptr, fpn_list[i], PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
            }
          }
        }
      }
    }
  }
#else
  for (int i = 0; i < num_pages; i++) {
    int pgn = (base + i * PAGING_PAGESZ) / PAGING_PAGESZ;
    if (caller->krnl->krnl_pgd != NULL) {
      caller->krnl->krnl_pgd[pgn] = fpn_list[i];
    }
  }
#endif

  *alloc_addr = base;
  return 0;
}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
  /* TODO: provide OS level management */

  //struct krnl_t *krnl = caller->krnl;
  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...

#define KMEM_POOL_MAX 16
  if (caller == NULL || cache_pool_id >= KMEM_POOL_MAX) {
    return -1;
  }

  if (caller->krnl->mm->kcpooltbl == NULL) {
    caller->krnl->mm->kcpooltbl = calloc(KMEM_POOL_MAX, sizeof(struct kcache_pool_struct));
    if (caller->krnl->mm->kcpooltbl == NULL) {
      return -1;
    }
  }

  struct kcache_pool_struct *pool = &caller->krnl->mm->kcpooltbl[cache_pool_id];

  addr_t pool_addr = 0;
  int rgid = PAGING_MAX_SYMTBL_SZ - 1 - (int)cache_pool_id;
  if (rgid < 0) {
    rgid = 0;
  }
  if (__kmalloc(caller, -1, rgid, size, &pool_addr) < 0) {
    return -1;
  }

  pool->storage = pool_addr;
  pool->align = (int)align;
  pool->size = 0;

  return 0;
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  addr_t addr = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr);

  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...

  if (addr == (addr_t)-1) {
    return -1;
  }

  proc->regs[reg_index] = addr;
  return 0;
}

/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{
  /* TODO: provide OS level management */
  /* TODO: provide OS level management */

  //struct krnl_t *krnl = caller->krnl;
  //krnl->symrgtbl...
  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...

  if (caller == NULL || alloc_addr == NULL) {
    return (addr_t)-1;
  }
  if (caller->krnl->mm->kcpooltbl == NULL) {
    return (addr_t)-1;
  }
  if (cache_pool_id < 0 || cache_pool_id >= 16) {
    return (addr_t)-1;
  }

  struct kcache_pool_struct *pool = &caller->krnl->mm->kcpooltbl[cache_pool_id];
  if (pool->align <= 0) {
    return (addr_t)-1;
  }

  addr_t slot_addr = pool->storage + (addr_t)pool->size;
  pool->size += pool->align;

  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ) {
    caller->krnl->mm->symrgtbl[rgid].rg_start = slot_addr;
    caller->krnl->mm->symrgtbl[rgid].rg_end = slot_addr + pool->align;
  }

  *alloc_addr = slot_addr;
  return 0;

}


int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_user_mem(...)
  //__write_kernel_mem(...);
  addr_t user_addr = caller->regs[source] + offset;
  if (!is_user_address(user_addr)) return -1;



  addr_t krnl_addr = caller->regs[destination];
  if (!is_kernel_address(krnl_addr)) return -1;

  /* Copy one by at one time*/
  for (uint32_t i = 0; i < size; i++) {
      BYTE data = 0;
      
      // Read 1 byte from User space
      if (__read_user_mem(caller, -1, -1, user_addr + i, &data) != 0) {
          return -1;
      }
      
      // Write 1 byte to Kernel space
      if (__write_kernel_mem(caller, -1, -1, krnl_addr + i, data) != 0) {
          return -1; 
      }
  }
  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_kernel_mem(...)
  //__write_user_mem(...);

  addr_t kernel_addr = caller->regs[source] + offset;
  addr_t user_addr = caller->regs[destination];

  if (!is_kernel_address(kernel_addr)) return -1;
  if (!is_user_address(user_addr)) return -1;

  for (uint32_t i = 0; i < size; i++) {
      BYTE data = 0;
      if (__read_kernel_mem(caller, -1, -1, kernel_addr + i, &data) != 0) return -1;
      if (__write_user_mem(caller, -1, -1, user_addr + i, data) != 0) return -1;
  }
  return 0;
}


/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS memory operator for kernel memory region */
  //krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy
  
  addr_t access_addr = offset;
  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ) {
      struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
      if (currg != NULL) {
          access_addr = currg->rg_start + offset;
      }
  }


  addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;
  get_pd_from_address(access_addr, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

 
  if (caller->krnl->krnl_pgd == NULL) return -1; 
    
  // Tầng PGD 
  addr_t *p4d_table = (addr_t *)caller->krnl->krnl_pgd[pgd_idx];
  if (p4d_table == NULL) return -1; 

  // Tầng P4D 
  addr_t *pud_table = (addr_t *)p4d_table[p4d_idx];
  if (pud_table == NULL) return -1;

  // Tầng PUD
  addr_t *pmd_table = (addr_t *)pud_table[pud_idx];
  if (pmd_table == NULL) return -1;

  // Tầng PMD
  addr_t *pt_table = (addr_t *)pmd_table[pmd_idx];
  if (pt_table == NULL) return -1;

  // Tầng PT 
  addr_t pte = pt_table[pt_idx];


  if (!PAGING_PAGE_PRESENT(pte)) {
      return -1; 
  }

  int fpn = PAGING_FPN(pte);
  int off = PAGING_OFFST(access_addr);
  addr_t phyaddr = (fpn * PAGING_PAGESZ) + off;
    

  MEMPHY_read(caller->krnl->mram, phyaddr, data);
  
  return 0;
}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS memory operator for kernel memory region */
  //krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy
  
  addr_t access_addr = offset;
  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ) {
      struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
      if (currg != NULL) {
          access_addr = currg->rg_start + offset;
      }
  }

  
  addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;
  get_pd_from_address(access_addr, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);


  if (caller->krnl->krnl_pgd == NULL) return -1;
    
  addr_t *p4d_table = (addr_t *)caller->krnl->krnl_pgd[pgd_idx];
  if (p4d_table == NULL) return -1;

  addr_t *pud_table = (addr_t *)p4d_table[p4d_idx];
  if (pud_table == NULL) return -1;

  addr_t *pmd_table = (addr_t *)pud_table[pud_idx];
  if (pmd_table == NULL) return -1;

  addr_t *pt_table = (addr_t *)pmd_table[pmd_idx];
  if (pt_table == NULL) return -1;

  addr_t pte = pt_table[pt_idx];

  if (!PAGING_PAGE_PRESENT(pte)) {
      return -1; 
  }

  int fpn = PAGING_FPN(pte);
  int off = PAGING_OFFST(access_addr);
  addr_t phyaddr = (fpn * PAGING_PAGESZ) + off;
    
  MEMPHY_write(caller->krnl->mram, phyaddr, value);
    
  return 0;  
}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS level management user memory access */
  //krnl->pgd ...
  addr_t access_addr = offset;
  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ) {
      struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
      if (currg != NULL) {
          access_addr = currg->rg_start + offset;
      }
  }

  return pg_getval(caller->krnl->mm, access_addr, data, caller);
}


/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS level management user memory access */
  //krnl->pgd ...

  addr_t access_addr = offset;
  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ) {
      struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
      if (currg != NULL) {
          access_addr = currg->rg_start + offset;
      }
  }

  return pg_setval(caller->krnl->mm, access_addr, value, caller);
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->krnl->mm->pgd[pagenum];

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;

  // pthread_mutex_lock(&mmvm_lock);
  // int pagenum, fpn;
  // uint32_t pte;

  // struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, 0);
  // int max_user_pgn = 0;
  // if (cur_vma != NULL) {
  //     max_user_pgn = cur_vma->sbrk / PAGING64_PAGESZ;
  // }

  // for (pagenum = 0; pagenum <= max_user_pgn; pagenum++)
  // {
  //   pte = pte_get_entry(caller, pagenum);

  //   if (PAGING_PAGE_PRESENT(pte))
  //   {
  //     fpn = PAGING_FPN(pte);
  //     MEMPHY_put_freefp(caller->krnl->mram, fpn);
  //   }
  // }

  // pthread_mutex_unlock(&mmvm_lock);
  // return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
  {
    return -1;
  }
  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  
  //fix 3:20pm 23/05
  if (prev == NULL) 
  {
    mm->fifo_pgn = NULL;
  } 
  else 
  {
    prev->pg_next = NULL;
  }
  //fix

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

/* Check dchi user or kernel */
int is_user_address(addr_t addr) {
    /* Move right 57 bit take 7 bit cao I. If User, must == 0 */
    addr_t high_bits = addr >> 57;
    if (high_bits == 0) {
        return 1; 
    }
    return 0; 
}


int is_kernel_address(addr_t addr) {
    /* Move right 57 bit. If Kernel, must be 1 */
    addr_t high_bits = addr >> 57;
    if (high_bits == 0x7F) {
        return 1; 
    }
    return 0; 
}



// #endif
