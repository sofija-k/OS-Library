#include "sim.h"
#include "coremap.h"
#include "pagetable.h"

int clock_handle;


/* Page to evict is chosen using the CLOCK algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
void advance_clock_handle(){++clock_handle;}
int find_first_evict(){
	int ret = 0;
	for (size_t i=clock_handle;i<memsize-1; ++i){
		struct pt_entry_s* evict = coremap[clock_handle].pte;
		if (get_referenced(evict) == 0){
			ret = i;
			advance_clock_handle();
			break;
		} else {
			set_referenced(evict, false);
			advance_clock_handle();}
		}
	return ret;
}

int clock_evict(void)
{
	int ret;
	int evicted = 0;
	for (size_t i=clock_handle;i<memsize-1; ++i){
		struct pt_entry_s* victim = coremap[clock_handle].pte;
		if (get_referenced(victim) == 1){
			set_referenced(victim, false);
			advance_clock_handle();}
		else {
			ret = i;
			++evicted;
			advance_clock_handle();
			break;
		}
	}
	struct pt_entry_s* victim = coremap[clock_handle].pte;
	if (evicted == 0){
		// we have last frame to check, and move handle to the front
		if (get_referenced(victim) == 1){
			set_referenced(victim, false);
			clock_init();
			// loop again
			ret = find_first_evict();
		} else {
			ret = clock_handle;
			clock_init();
		}
	} 
	return ret;
	
}

/* This function is called on each access to a page to update any information
 * needed by the CLOCK algorithm.
 * Input: The page table entry and full virtual address (not just VPN)
 * for the page that is being accessed.
 */
void clock_ref(int frame, vaddr_t vaddr)
{
	(void)vaddr;
	struct pt_entry_s* victim = coremap[frame].pte;
	set_referenced(victim, true);	
}

/* Initialize any data structures needed for this replacement algorithm. */
void clock_init(void)
{
	clock_handle = 0;
}

/* Cleanup any data structures created in clock_init(). */
void clock_cleanup(void)
{

}
