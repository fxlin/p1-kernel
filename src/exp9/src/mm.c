#define K2_DEBUG_WARN

#include "utils.h"
#include "mmu.h"
#include "spinlock.h"
#include "sched.h"


/* 
	Minimalist page allocation 

	all alloc/free funcs below are locked
*/
static unsigned short mem_map [ PAGING_PAGES ] = {0,};
static struct spinlock alloc_lock = {.locked=0, .cpu=0, .name="alloc_lock"}; 

/* allocate a page (zero filled). return kernel va. */
void *kalloc() {
	unsigned long page = get_free_page();
	if (page == 0) {
		return 0;
	}
	return PA2VA(page);
}

/* free p which is kernel va */
void kfree(void *p) {
	free_page(VA2PA(p));
}

/* allocate & map a page to user. return kernel va of the page, 0 if failed.
	@mm: the user's task->mm
	@va: the user va to map the page to 
*/
void *allocate_user_page_mm(struct mm_struct *mm, unsigned long va, unsigned long perm) {
	unsigned long page;
	if (mm->user_pages_count == MAX_TASK_USER_PAGES) // no need to go further
		return 0; 

	page = get_free_page();
	if (page == 0) {
		W("get_free_page failed");
		return 0;
	}
	if (map_page(mm, va, page, 1 /*alloc*/, perm))
		return PA2VA(page);
	else {
		W("map_page failed");
		free_page(page);
		return 0; 
	}
}

void *allocate_user_page(struct task_struct *task, unsigned long va, unsigned long perm) {
	return allocate_user_page_mm(&task->mm, va, perm);
}

/* allocate a page (zero filled). return pa of the page. 0 if failed */
unsigned long get_free_page()
{
	acquire(&alloc_lock);
	for (int i = 0; i < PAGING_PAGES; i++){
		if (mem_map[i] == 0){
			mem_map[i] = 1;
			release(&alloc_lock);
			unsigned long page = LOW_MEMORY + i*PAGE_SIZE;
			memzero(PA2VA(page), PAGE_SIZE);
			return page;
		}
	}
	release(&alloc_lock);
	return 0;
}

/* free a page. @p is pa of the page. */
void free_page(unsigned long p){
	acquire(&alloc_lock);
	mem_map[(p - LOW_MEMORY) / PAGE_SIZE] = 0;
	release(&alloc_lock);
}

/*
	Virtual memory implementation
*/

/* set (or find) a pte, i.e. the entry in a bottom-level pgtable 
	@pte: the 0-th pte of that pgtable, kernel va
	@va: corresponding to @pte
	@pa: if 0, dont touch pte (just return its addr); otherwise update pte
	@perm: permission bits to be written to pte (cf mmu.h)
   return: kernel va of the pte set or found
   
   NB: the found pte can be invalid 
   */
static 
unsigned long * map_table_entry(unsigned long *pte, unsigned long va, unsigned long pa, 
			unsigned long perm) {
	unsigned long index = va >> PAGE_SHIFT;
	index = index & (PTRS_PER_TABLE - 1);
	if (pa) {
		unsigned long entry = pa | MMU_PTE_FLAGS | perm; 
		pte[index] = entry;
	}
	return pte + index; 
}

/* Extract table index from the virtual address and prepares a descriptor 
	in the parent table that points to the child table.
	Allocate the child table as needed. 
	Return: the phys addr of the next pgtable. 0 if failed to alloc.

   @table: a (virt) pointer to the parent page table. This page table is assumed 
   	to be already allocated, but might contain empty entries.
   @shift: indicate where to find the index bits in a virtual address corresponding 
   	to the the target pgtable level. See project description for details.
   @va: the virt address of the page to be mapped
   @alloc [in|out]: in: 1 means alloc a new table if needed; 0 means don't alloc
   	out: 1 means a new pgtable is allocated; 0 otherwise   
   @desc [out]: ptr (kern va) to the pgtable descriptor installed/located
*/
static 
unsigned long map_table(unsigned long *table, unsigned long shift, 
	unsigned long va, int* alloc, unsigned long **pdesc) {
	unsigned long index = va >> shift;

	index = index & (PTRS_PER_TABLE - 1);
	if (pdesc)
		*pdesc = table + index; 
	if (!table[index]) { /* next level pgtable absent. */
		if (*alloc) { /* asked to alloc. then alloc a page & install */
			unsigned long next_level_table = get_free_page();
			if (!next_level_table) {
				*alloc = 0; 
				return 0; 
			}
			unsigned long entry = next_level_table | MM_TYPE_PAGE_TABLE;
			table[index] = entry;
			return next_level_table;
		} else { /* asked not to alloc. bail out */
			*alloc = 0; /* didn't alloc */
			return 0; 
		}
	} else {  /* next lv pgtable exists */
		*alloc = 0; /* didn't alloc */
		return PTE_TO_PA(table[index]);
	}
}

#if 0 // obsoeletd. ref desgin
/* Walk a task's pgtable tree. Find and (optionally) update the pte corresponding to a user va 
   @mm: the user virt addd under question, can be obtained via task_struct::mm
   @va: given user va 
   @page: the phys addr of the page start. if 0, do not map (just locate the pte)
   @alloc: if 1, alloate any absent pgtables if needed.
   @perm: permission, only matters page!=0 & alloc!=0
   return: kernel va of the pte set or found; 0 if failed (cannot proceed w/o pgtable alloc)
   */
unsigned long *map_page_old(struct mm_struct *mm, unsigned long va, unsigned long page,
				int alloc, unsigned long perm) {
	unsigned long pgd, *desc; 
	BUG_ON(!mm); 

	// pointers to pgtable descriptors installed during walk. kern va. max four
	// for reversing them in case we bail out
	int nk = mm->kernel_pages_count, k = 0; 
	unsigned long *ker_pages[4] = {0}; 
	unsigned long alloc_kern_pages[4] = {0}; // pa

	// reached limit for user pages 
	if (page != 0 && alloc == 1 && mm->user_pages_count >= MAX_TASK_USER_PAGES)
		return 0; 

	/* start from the task's top-level pgtable. allocate if absent 
		this is how a task's pgtable tree gets allocated
	*/
	if (!mm->pgd) { 
		if (alloc) {
			mm->pgd = get_free_page();
			BUG_ON(!mm->pgd || mm->kernel_pages_count >= MAX_TASK_KER_PAGES); 
			mm->kernel_pages[mm->kernel_pages_count++] = mm->pgd;
		} else 
			goto no_alloc; 
	} 
	
	int allocated = alloc; 
	pgd = mm->pgd;
	/* move to the next level pgtable. allocate one if absent */
	unsigned long pud = map_table((unsigned long *)(pgd + VA_START), PGD_SHIFT, 
		va, &allocated, &desc);
	if (pud) {
		if (allocated) { /* we've allocated a new kernel page. take it into account for future reclaim */
			BUG_ON(mm->kernel_pages_count >= MAX_TASK_KER_PAGES); 
			mm->kernel_pages[mm->kernel_pages_count++] = pud;
		}
		else
			; /* use existing -- fine */
	} else { /* !pud */
		if (!alloc) /* failed b/c we reached nonexisting pgtable, and asked not to alloc */
			goto no_alloc;
		else	/* asked to alloc but still failed. low kernel mem? shouldn't happen */
			panic("map_page - pud"); 
	}

	/* next level ... same logic as above */
	allocated = alloc; 
	unsigned long pmd = map_table((unsigned long *)(pud + VA_START) , PUD_SHIFT, va, &allocated);
	if (pmd) {
		if (allocated) {
			BUG_ON(mm->kernel_pages_count >= MAX_TASK_KER_PAGES); 
			mm->kernel_pages[mm->kernel_pages_count++] = pmd;
		}
	} else {
		if (!alloc)
			goto no_alloc; 
		else 
			panic("map_page - pmd");
	}
	
	/* next level ... same logic as above */
	allocated = alloc; 
	unsigned long pt = map_table((unsigned long *)(pmd + VA_START), PMD_SHIFT, va, &allocated);
	if (pt) {
		if (allocated) {
			BUG_ON(mm->kernel_pages_count >= MAX_TASK_KER_PAGES); 
			mm->kernel_pages[mm->kernel_pages_count++] = pt;
		}
	} else {
		if (!alloc)
			goto no_alloc; 
		else
			panic("map_page - pte"); 
	}

	/* reached pt, the bottom level of pgtable tree */
	unsigned long *pte_va = 
		map_table_entry((unsigned long *)(pt + VA_START), va, page /*=0 for finding entry only*/, perm);
	if (page) { /* a page just installed, bookkeeping.. */
		struct user_page p = {page, va};
		BUG_ON(mm->user_pages_count >= MAX_TASK_USER_PAGES); // shouldn't happen, as we checked above
		mm->user_pages[mm->user_pages_count++] = p;
	}
	return pte_va;

no_alloc:
	// xzl: TODO: reverse allocated pgtables during tree walk
	E("failed. TODO: reverse allocated pgtables during tree walk"); 
	return 0; 	
}
#endif

/* Walk a task's pgtable tree. Find and (optionally) update the pte corresponding to a user va 
   @mm: the user virt addd under question, can be obtained via task_struct::mm
   @va: given user va 
   @page: the phys addr of the page start. if 0, do not map (just locate the pte)
   @alloc: if 1, alloate any absent pgtables if needed.
   @perm: permission, only matters page!=0 & alloc!=0
   
   return: kernel va of the pte set or found; 0 if failed (cannot proceed w/o pgtable alloc)
	the located pte could be invalid
   */
unsigned long *map_page(struct mm_struct *mm, unsigned long va, unsigned long page,
				int alloc, unsigned long perm) {
	unsigned long *desc; 
	BUG_ON(!mm); 

	// record pgtable descriptors installed & ker pages allocated during walk. 
	// for reversing them in case we bail out. most four (pud..pte)
	int nk = mm->kernel_pages_count; 
	unsigned long *descs[4] = {0}; 	
	unsigned long ker_pages[4] = {0}; // pa

	// reached limit for user pages 
	if (page != 0 && alloc == 1 && mm->user_pages_count >= MAX_TASK_USER_PAGES)
		return 0; 

	/* start from the task's top-level pgtable. allocate if absent 
		this is how a task's pgtable tree gets allocated
	*/
	if (!mm->pgd) { 
		if (alloc) {
			if (nk == MAX_TASK_KER_PAGES)
				goto fail; 
			if (!(mm->pgd = get_free_page()))
				goto fail; 
			ker_pages[0] = mm->pgd; 
			descs[0] = &(mm->pgd); 
			// mm->kernel_pages[mm->kernel_pages_count++] = mm->pgd;
		} else 
			goto fail; 
	} 
	
	__attribute__((unused)) const char *lvs[] = {"pgd","pud","pmd","pte"};
	const int shifts [] = {0, PGD_SHIFT, PUD_SHIFT, PMD_SHIFT}; 
	unsigned long table = mm->pgd; 	// pa of a pgd/pud/pmd/pte
	int allocated; 

	for (int i = 1; i < 4; i++) { // pud->pmd->pte
		allocated = alloc; 
		table = map_table(PA2VA(table), shifts[i], va, &allocated, &desc); 
		if (table) { 
			if (allocated) { 
				ker_pages[i] = table; 
				descs[i] = desc; 
				if (nk+i > MAX_TASK_KER_PAGES) { /* exceeding the limit, bail out*/
					W("MAX_TASK_KER_PAGES %d reached", MAX_TASK_KER_PAGES); 
					goto fail; 
				}
			} else 
				; /* use existing table -- fine */
		} else { /*!table*/
			if (!alloc)
				W("%s: failed b/c we reached nonexisting pgtable, and asked not to alloc",
					lvs[i]);
			else
				W("%s: asked to alloc but still failed. low kernel mem?", lvs[i]);
			goto fail; 
		}
	}	

	/* Now, pgtables at all levels are in place. table points to a pte, 
		the bottom level of pgtable tree. Install the actual user page */
	unsigned long *pte_va = 
		map_table_entry(PA2VA(table), va, page /* 0 for finding entry only*/, perm);
	if (page) { /* a page just installed, bookkeeping.. */
		// struct user_page p = {page, va};
		BUG_ON(mm->user_pages_count >= MAX_TASK_USER_PAGES); // shouldn't happen, as we checked above
		// mm->user_pages[mm->user_pages_count++] = p;
		mm->user_pages_count++; 
	}

	// success -- bookkeepin the kern pages ever allocated
	for (int i = 0; i < 4; i++) {
		if (ker_pages[i] == 0) 
			continue; 	
		mm->kernel_pages[mm->kernel_pages_count++] = ker_pages[i]; 
		V("now has %d kern pages", mm->kernel_pages_count); 
	}
	return pte_va;

fail:
	for (int i = 0; i < 4; i++) {
		if (ker_pages[i] == 0) 
			continue; 	 		
		BUG_ON(!alloc || !descs[i]); 
		free_page(ker_pages[i]); 
		*descs = 0; 	// nuke the descriptor 
	}
	W("failed. reverse allocated pgtables during tree walk"); 
	return 0; 	
}


/* duplicate the contents of the @current task's user pages to the @dst task, at same va.
	allocate & map pages for @dsk on demand
	return 0 on success
*/
int copy_virt_memory(struct task_struct *dst) {
	struct pt_regs *regs = task_pt_regs(current);
	struct task_struct* src = current;
	// go through the @dst task's virt pages, allocate & map phys pages, then copy the content
	// from the corresponding va from the @current task
	for (unsigned long i = 0; i < src->mm.sz; i += PAGE_SIZE) {
		// locate src pte, extract perm bits, copy over. 
		unsigned long *pte = map_page(&src->mm, i, 0/*just locate*/, 0/*no alloc*/, 0); 
		BUG_ON(!pte);  // bad user mapping? 
		unsigned long perm = PTE_TO_PERM(*pte); 
		void *kernel_va = allocate_user_page(dst, i, perm);
		if(kernel_va == 0)
			goto no_mem; 
		// xzl: src uses user va. this assumes the current task's va is active
		memmove(kernel_va, (void *)i, PAGE_SIZE);
	}

	// copy user stack 
	V("regs->sp %lx", regs->sp);
	for (unsigned long i = PGROUNDDOWN(regs->sp); i < USER_VA_END; i+=PAGE_SIZE) {
		unsigned long *pte = map_page(&src->mm, i, 0/*just locate*/, 0/*no alloc*/, 0); 
		BUG_ON(!pte);  // bad user mapping (stack)? 	
		void *kernel_va = allocate_user_page(dst, i, MM_AP_RW | MM_XN);
		if(kernel_va == 0)
			goto no_mem; 
		// xzl: src uses user va. this assumes the current task's va is active
		V("kern va %lx i %x", kernel_va, i);
		memmove(kernel_va, (void *)i, PAGE_SIZE);
	}

	return 0;
no_mem: 	// TODO: revserse allocation .. 
	return -1; 
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped or invalid. 
// Can only be used to look up user pages.
unsigned long walkaddr(struct mm_struct *mm, unsigned long va) {
	unsigned long *pte = map_page(mm, va, 0/*dont map, just locate*/, 0/*don't alloc*/, 0/*perm*/);
	if (!pte)
		return 0; 
	if ((*pte & (~PAGE_MASK) & (~(unsigned long)MM_AP_MASK)) != MMU_PTE_FLAGS) {
		W("pte found but invalid %016lx, %016lx", 
			(*pte & (~PAGE_MASK) & (~(unsigned long)MM_AP_MASK)), MMU_PTE_FLAGS);
		return 0; 
	}
	BUG_ON(PTE_TO_PA(*pte) < PHYS_SIZE);
	return PTE_TO_PA(*pte);
}

// TODO: do we need this? we can just free the list of user/kernel pages given a task
// Recursively free page-table pages.
// All leaf mappings must already have been removed.
// void freewalk(struct task_struct *task)


// TODO: mark a PTE invalid for user access.
// used by exec for the user stack guard page.
// void
// uvmclear(pagetable_t pagetable, uint64 va)

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(struct mm_struct * mm, uint64_t dstva, char *src, uint64_t len) {
    uint64_t n, va0, pa0;

	if (dstva > USER_VA_END) {
		W("illegal user va. a kernel va??");
		return -1; 
	} 

    while (len > 0) {
        va0 = PGROUNDDOWN(dstva); // va0 pagebase
        pa0 = walkaddr(mm, va0);
        if (pa0 == 0)
            return -1;
        n = PAGE_SIZE - (dstva - va0); // n: remaining bytes on the page
        if (n > len)
            n = len;
        memmove(PA2VA(pa0 + (dstva - va0)), src, n);

        len -= n;
        src += n;
        dstva = va0 + PAGE_SIZE;
    }
    return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(struct mm_struct * mm, char *dst, uint64 srcva, uint64 len) {
    uint64 n, va0, pa0;

	if (srcva > USER_VA_END) {
		W("// illegal user va. a kernel va??");
		return -1; 
	}
	V("%lx %lx", srcva, len);

    while (len > 0) {
        va0 = PGROUNDDOWN(srcva);  // xzl: user virt page base...
        pa0 = walkaddr(mm, va0); // xzl: phys addr for user va pagebase
        if (pa0 == 0)
            return -1;
        n = PAGE_SIZE - (srcva - va0);
        if (n > len)
            n = len;
        memmove(dst, PA2VA(pa0 + (srcva - va0)), n);

        len -= n;
        dst += n;
        srcva = va0 + PAGE_SIZE;
    }
    return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(struct mm_struct *mm, char *dst, uint64 srcva, uint64 max) {
    uint64 n, va0, pa0;
    int got_null = 0;

    if (srcva > USER_VA_END) {
		W("// illegal user va. a kernel va??");
		return -1; 
	}

    while (got_null == 0 && max > 0) {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(mm, va0);
        if (pa0 == 0)
            return -1;
        n = PAGE_SIZE - (srcva - va0);
        if (n > max)
            n = max;

        char *p = (char *)PA2VA(pa0 + (srcva - va0));
        while (n > 0) {
            if (*p == '\0') {
                *dst = '\0';
                got_null = 1;
                break;
            } else {
                *dst = *p;
            }
            --n;
            --max;
            p++;
            dst++;
        }

        srcva = va0 + PAGE_SIZE;
    }
    if (got_null) {
        return 0;
    } else {
        return -1;
    }
}

// Copy to either a user address, or kernel address 
// depending on usr_dst.
// xzl: if user_dst==1, dst is kernel va. (not pa)
//    cf: readi(), and its callers
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct task_struct *p = myproc();
  if(user_dst){
    return copyout(&p->mm, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
// xzl: cf above
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct task_struct *p = myproc();
  if(user_src){
    return copyin(&p->mm, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// free user pages only if useronly=1, otherwise free user/kernel pages
// NB: wont update pgtables for umapping freed user pages
void free_task_pages(struct mm_struct *mm, int useronly) {
	unsigned long page; 
	BUG_ON(!mm); 

	unsigned long sz = mm->sz; 

	I("%s enter sz %lu", __func__, sz);

	if (growproc(mm, -sz) == (unsigned long)(void *)-1) {
		BUG(); 
		return; 
	}
	// TODO: free stack pages.... 
	
	if (!useronly) {
		// free kern pages. must handle with care. 
		// task_struct and mm all live on the kernel pages. so once we 
		// free them, they could be grabbed away by other contexts. 
		// as a result we may have bad mm in the loop below. 
		// alloc_lock does not help: it only protects single page alloc/free.
		// we need to lock the whole mem allocator. 
		// for now, just disable irq...
		push_off(); 
		for (int i = 0; i < mm->kernel_pages_count; i++) {
			page = mm->kernel_pages[i]; 
			BUG_ON(!page);
			free_page(page); 
		}
		memzero(&mm->kernel_pages, sizeof(mm->kernel_pages));
		mm->kernel_pages_count = 0;
		mm->pgd = 0; // should we do this? 
		pop_off(); 
	}
}

/* unmap and free user pages. 
	caller must flush tlb (e.g. via set_pgd(mm->pgd))
	reutrn # of freed pages on success. -1 on failure 

	start_va and size must be page aligned 

	TODO: make it transactional. now if any page fails to unmap, it will abort 
		there. 
	TODO: need to grab any lock?
*/
static 
int free_user_page_range(struct mm_struct *mm, unsigned long start_va, 
		unsigned long size) {
	unsigned long *pte; 
	unsigned long page; // pa; 
	int cnt = 0; 

	BUG_ON((start_va & (~PAGE_MASK)) || (size & (~PAGE_MASK)) || !mm);

	for (unsigned long i = start_va; i < start_va + size; i+= PAGE_SIZE, cnt++) {		
		pte = map_page(mm, i, 0/*only locate pte*/, 0/*no alloc*/, 0/*dont care*/); 
		if (!pte)
			goto bad; 
		page = PTE_TO_PA(*pte); 
		free_page(page); 
		mm->user_pages_count --;
		*pte = 0; // nuke pte				
	}
	return cnt; 
bad: 
	BUG(); 
	return -1; 	
}

/* 
	On success, sbrk() returns the previous program break. 
	(If the break was increased, then this value is a pointer to the start of 
	the newly allocated memory). On error, (void *) -1 is returned, 
	https://linux.die.net/man/2/sbrk

	incr can be pos or neg. 
	allows incr == -sz, i.e. free all user pages.
*/
unsigned long growproc (struct mm_struct *mm, int incr) {
	unsigned long sz = mm->sz, sz1; 
	void *kva; 
	int ret; 

	if (incr>10000 || incr <0)
		V("sz 0x%lx %ld (dec) incr %d (dec). requested new brk 0x%lx", 
			sz, sz, incr, sz+incr); 
	
	// careful: sz is unsigned; incr is signed
	if (incr < 0 && sz < -incr) {
		W("incr too small"); 
		W("sz 0x%lx %ld (dec) incr %d (dec). requested new brk 0x%lx", 
			sz, sz, incr, sz+incr); 
		goto bad; 
	}
	if (incr > 0 && sz + incr >= USER_VA_END) {
		W("incr too large"); 
		W("sz 0x%lx %ld (dec) incr %d (dec). requested new brk 0x%lx", 
		sz, sz, incr, sz+incr); 
		goto bad; 		
	}

	// TODO: need to grab any lock?
	if (incr >= 0) {		// brk grows
		for (sz1 = PGROUNDUP(sz); sz1 < sz + incr; sz1 += PAGE_SIZE) {
			kva = allocate_user_page_mm(mm, sz1, MM_AP_RW | MM_XN); 
			if (!kva) {
				W("allocate_user_page_mm failed");
				goto reverse; 
			}
		}
	} else {	// brk shrinks
		// since it's shrinking, unmap from the next page boundary of the new brk, 
		// to the page start of the old brk (sz)
		int ret = free_user_page_range(mm, PGROUNDUP(sz+incr),  
			PGROUNDDOWN(sz) - PGROUNDUP(sz+incr)); 
		BUG_ON(ret == -1); // user va has bad mapping
	}
	sz1 = mm->sz; // old sz
	mm->sz += incr; 	
	if (&current->mm == mm)
		set_pgd(mm->pgd); // tlb flush

	V("succeeds. return old brk %lx new brk %lx", sz1, mm->sz); 
	return sz1; 	

reverse: 	
	// sz was old brk, sz1 was the failed va to allocate 	
	ret = free_user_page_range(mm, PGROUNDUP(sz), sz1 - PGROUNDUP(sz)); 
	BUG_ON(ret == -1); // user va has bad mapping.
	I("reversed user page allocation %d pages sz %lx sz1 %lx", ret, sz, sz1);
bad: 
	W("growproc failed");	 
	return (unsigned long)(void *)-1; 	
}

// called from el0_da, which was from data abort exception 
// @esr: value of error syndrome register, indicating the error reason
// xzl: limitations -- didn't check whether @addr is a legal user va
// 		@ind is global. at least, it should be per task or per addr (?)
//	TODO
static int ind = 1; // # of times we tried memory access
int do_mem_abort(unsigned long addr, unsigned long esr) {
	 __attribute__((unused))  struct pt_regs *regs = task_pt_regs(current);	 
	unsigned long dfs = (esr & 0b111111);

	if (addr > USER_VA_END) {
		E("do_mem_abort: bad user va. faulty addr 0x%lx > USER_VA_END %lx", addr, 
			USER_VA_END); 
		goto exit; 
	}

	/* whether the current exception is actually a translation fault? */		
	if ((dfs & 0b111100) == 0b100) { /* translation fault */
		unsigned long page = get_free_page();
		if (page == 0) {
			E("do_mem_abort: insufficient mem"); 
			goto exit; 
		}
		map_page(&(current->mm), addr & PAGE_MASK, page, 1/*alloc*/, MMU_PTE_FLAGS | MM_AP_RW); // TODO: set perm (XN?) based on addr 
		ind++; // return to user, give it a second chance
		if (ind > 5) {  // repeated fault
		    E("do_mem_abort: too many mem faults. ind %d", ind); 
			return -1;  // likely a bug?
		}
		W("demand paging at user va 0x%lx, elr 0x%lx", addr, regs->pc);
		return 0;
	} 
	/* other causes, e.g. permission... */
	E("do_mem_abort: cannot handle. faulty addr 0x%lx not translation fault", addr); 
exit: 	
	exit_process(-1); 
	return -1;
}