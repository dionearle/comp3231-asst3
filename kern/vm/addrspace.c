/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

#include <elf.h>
/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	// setup the page table, 1024 = 2^10
    as->pagetable = kmalloc(1024 * sizeof(paddr_t *));

	// checking the kmalloc was successful
	if (as->pagetable == NULL) {
		kfree(as);
		return NULL;
	}

	// allocating all entries in the first level page table as null
	for(int i = 0; i < 1024; i++){
		as->pagetable[i] = NULL;
	}

	// next we want to set the list of regions as null
	as->regions = NULL;

	// and finally we set the addresses for the stack and heap to their initial values
	as->heap = 0;
	as->stack = USERSTACK;

    // set the loadingbit to false
    as->loadingbit = 0;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	// used to keep track of whether the frame is dirty or not
	uint32_t isDirty = 0;

	// copy all entries in the page table that are not null
	for(int i = 0; i < 1024; i++){
		if(old->pagetable[i]){
            // malloc the level 1 page table entry
			newas->pagetable[i] = kmalloc(1024 * sizeof(paddr_t));
            
            // next copy all the level 2 entries associated
			for(int j = 0; j < 1024; j++){
                // test if the original entry has not been defined, if so just copy it to newas
                if(old->pagetable[i][j] == 0){
                    newas->pagetable[i][j] = old->pagetable[i][j];
                } else {
					// else allocate the page and copy the physical memory
					vaddr_t copyFrame = alloc_kpages(1);
					bzero((void *)copyFrame,PAGE_SIZE);
					memmove((void *)copyFrame, (const void *)PADDR_TO_KVADDR(old->pagetable[i][j] & PAGE_FRAME), PAGE_SIZE);
                    isDirty = old->pagetable[i][j] & TLBLO_DIRTY;
                	newas->pagetable[i][j] = (KVADDR_TO_PADDR(copyFrame) & PAGE_FRAME) | TLBLO_VALID | isDirty;
                }

			}
		}
	}

	// loop through all regions, and copy them to newas
	region *new_region = NULL;
	region *curr_region = old->regions;
	while (curr_region != NULL) {

		// we then allocate memory for this new region
		region *tmp = kmalloc(sizeof(region));
		
		// checking the kmalloc was successful
		if (tmp == NULL) {
			return ENOMEM;
		}

		// we then setup all of the values for this region
		tmp->base = curr_region->base;
		tmp->size = curr_region->size;
		tmp->flags = curr_region->flags;
		tmp->prevFlags = curr_region->prevFlags;
		tmp->next = NULL;

		// if this is the first in the list, we set new_region to equal it
		if (new_region == NULL) {
			newas->regions = tmp;
			new_region = tmp;
		} else {
			// otherwise it is appended to the end of the list
			new_region->next = tmp;
		}

		curr_region = curr_region->next;
	}

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	// if there is not an address space to destroy, we simply return
	if (as == NULL) {
		return;
	}

	// we first want to free all pages in the page table
	for(int i = 0; i < 1024; i++){

		// if the current value in the page table is already NULL,
		// we can just continue
		if (as->pagetable[i] == NULL) {
			continue;
		}

		// otherwise, we step down into the 2nd level page table
		// and free all pages in this entry
		for (int j = 0; j < 1024; j++) {
			if (as->pagetable[i][j] != 0) {
				free_kpages(PADDR_TO_KVADDR(as->pagetable[i][j] & PAGE_FRAME));
			}
		}

		// once we have freed all entries in the second level,
		// we can free the first level itself
		kfree(as->pagetable[i]);
	}

	// once all pagetables entries are freed, we free the pagetable itself
	kfree(as->pagetable);

	// then we free all regions in the linked list
	region *tmp;
	while (as->regions != NULL) {
		tmp = as->regions;
		as->regions = as->regions->next;
		kfree(tmp);
	}

	// finally we free the address space itself
	kfree(as);
}

// copied from dumbvm.c
void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

// copied from dumbvm.c
void
as_deactivate(void)
{
	/* nothing */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	
	// if the address space is not valid, we return EFAULT for a bad memory reference
	if (as == NULL) {
		return EFAULT;
	}

	// by adding the memsize to the vaddr, we can see if the end of the region
	// goes into the stack. if it does, we are out of memory so return ENOMEM
	if (vaddr + memsize >= as->stack) {
		return ENOMEM;
	}

	// taken from dumbvm to align the region to a page
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	// we then allocate memory for this new region
	region *newRegion = kmalloc(sizeof(region));
	
	// checking the kmalloc was successful
	if (newRegion == NULL) {
		return ENOMEM;
	}

	// we can now setup this new region by using the arguments
	// passed in through the function
	newRegion->base = vaddr;
	newRegion->size = memsize;

	// we also want to assign the flags for this region based
	// on whether it is readable, writeable or executable
	newRegion->flags = 0;
	if (readable) {
		newRegion->flags |= PF_R;
	}
	if (writeable) {
		newRegion->flags |= PF_W;
	}
	if (executable) {
		newRegion->flags |= PF_X;
	}

	// we also want to set the prevFlags to equal the same
	newRegion->prevFlags = newRegion->flags;

	// now that we have finished setting up the new region,
	// we can add it to the head of the linked list of regions
	newRegion->next = as->regions;
	as->regions = newRegion;

	// finally we update the address for the heap,
	// which sits above the last region
	as->heap = vaddr + memsize;

	return 0;

}

int
as_prepare_load(struct addrspace *as)
{
	// if the address space is not valid, we return EFAULT for a bad memory reference
	if (as == NULL) {
		return EFAULT;
	}

	// set the loading bit
	as->loadingbit = TLBLO_DIRTY;

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
    // if the address space is not valid, we return EFAULT for a bad memory reference
    if (as == NULL) {
        return EFAULT;
    }

	// reset the loading bit
	as->loadingbit = 0;

    // flush the TLB since it will have outdated flags
	/* Disable interrupts on this CPU while frobbing the TLB. */
	int i, spl;
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);

	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */
	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

