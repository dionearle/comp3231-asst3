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

	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

    // test that the faultaddress falls within a defined region
    uint32_t isDirty = 0;
	region *found_region = as->regions;
	while (found_region != NULL) {
        if(found_region->base <= faultaddress && faultaddress < found_region->base + found_region->size){
            if ((found_region->flags & PF_W) == PF_W) {
                isDirty = TLBLO_DIRTY;
            }
            break;
        }
        found_region = found_region->next;
	}

    // test that we found a region faultaddress falls within
    if(found_region == NULL){
        if (!(faultaddress < as->stack && faultaddress > (as->stack - 16 * PAGE_SIZE))) {
            return EFAULT;
        }

        // else we're in the stack, so set the dirtybit
        isDirty = TLBLO_DIRTY;
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

        // used to keep track of whether the region is write protected or not
        // finally we setup the page
        vaddr_t virtualBase = alloc_kpages(1);
        bzero((void *)virtualBase, PAGE_SIZE);
        paddr_t physicalBase = KVADDR_TO_PADDR(virtualBase);
        as->pagetable[lvl1_index][lvl2_index] = (physicalBase & PAGE_FRAME) | TLBLO_VALID | isDirty;
    }

    // load it into the TLB and then return
    uint32_t ehi, elo;
    ehi = faultaddress & TLBHI_VPAGE;
    elo = as->pagetable[lvl1_index][lvl2_index] | as->loadingbit;

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

