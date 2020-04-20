#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>

#include <proc.h>
#include <elf.h>
#include <spl.h>

/* Place your page table functions here */


void vm_bootstrap(void)
{
    /* Initialise any global components of your VM sub-system here.  
     *  
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    // if faulttype == VM_FAULT_READONLY then return EFAULT
    if (faulttype == VM_FAULT_READONLY){
        return EFAULT;
    }

    // load the address space
	struct addrspace *as;

	as = proc_getas();

    // test that the faultaddress falls within a defined region
	region *found_region = as->regions;
	while (found_region != NULL) {
        if(found_region->base <= faultaddress && faultaddress < found_region->base + found_region->size){
            break;
        }
        found_region = found_region->next;
	}

    // test that we found a region faultaddress falls within
    if(found_region == NULL){
        return EFAULT;
    }

    // load level 1 index, level 2 index, and the offset
    vaddr_t lvl1_index = faultaddress >> 22;
    vaddr_t lvl2_index = (faultaddress << 10) >> 22;
    
    // test if page table entry is invalid, if so malloc it
    if(as->pagetable[lvl1_index] == NULL){
        as->pagetable[lvl1_index] = kmalloc(1024 * sizeof(paddr_t));

        for(int i = 0; i < 1024; i++){
            as->pagetable[lvl1_index][i] = 0;
        }
    }

    // test if page is not defined, if so malloc it and initialize it to zero
    if(as->pagetable[lvl1_index][lvl2_index] == 0){
        as->pagetable[lvl1_index][lvl2_index] = KVADDR_TO_PADDR(alloc_kpages(1));

        bzero((void *)PADDR_TO_KVADDR(as->pagetable[lvl1_index][lvl2_index]), PAGE_SIZE);
    }

    // load it into the TLB and then return
    uint32_t ehi, elo;
    ehi = faultaddress & TLBHI_VPAGE;
    elo = as->pagetable[lvl1_index][lvl2_index];

    // if any flags set, then set the 'valid' TLB bit
    if(found_region->flags != 0){
        elo |= TLBLO_VALID;
    }       

    // if the write flag is set, then set the 'dirty' bit
    if(found_region->flags & PF_W){
        elo |= TLBLO_DIRTY;
    }

   	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();
    tlb_random(ehi, elo);
	splx(spl);

    return 0;
}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}

