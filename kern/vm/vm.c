#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>

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
	as = proc_getas();

    // load level 1 index, level 2 index, and the offset
    vaddr_t lvl1_index = vaddr >> 22;
    vaddr_t lvl2_index = (vaddr >> 12) & 0x3ff;
    vaddr_t offset = vaddr & 0xfff;


    // test if page table entry is invalid, if so malloc it
    if(as->pagetable[lvl1_index] == NULL){
        as->pagetable[lvl1_index] = kmalloc(1024 * sizeof(paddr_t);

        for(i = 0; i < 1024; i++){
            as->pagetable[lvl1_index][i] = NULL;
        }
    }

    // test if page is not defined, if so malloc it and initialize it to zero
    if(as->pagetable[lvl1_index][lvl2_index] == NULL){
        pagetable[lvl1_index][lvl2_index] = KVADDR_TO_PADDR(alloc_kpages(1));

        bzero(PADDR_TO_KVADDR(pagetable[lvl1_index][lvl2_index]), PAGE_SIZE);
    }

    // load it into the TLB and then return
	uint32_t ehi, elo;
    ehi = faultaddress;
    elo = pagetable[lvl1_index][lvl2_index] | TLBLO_DIRTY | TLBLO_VALID;

   	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();
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

