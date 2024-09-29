#include "sim.h"
#include "coremap.h"
#include <stdio.h>

int threshold;
struct list_head Am;
struct list_head A1;

void place_tail_Am(int frame){
	list_entry* entry = &coremap[frame].framelist_entry;
	list_add_tail(&Am, entry);
	++(&Am)->size;
	assert(list_entry_is_linked(entry));
}

void place_tail_A1(int frame){
	list_entry* entry = &coremap[frame].framelist_entry;
	list_add_tail(&A1, entry);
	++(&A1)->size;
	assert(list_entry_is_linked(entry));
}

int evict_head_A1(){
	assert(A1.size > 0);
	struct list_entry *head = A1.head.next;
	struct frame *f = container_of(head, struct frame, framelist_entry);
	assert(f->in_use);
	int frame = f-coremap;
	list_del(head);
	--(&A1)->size;
	assert(!list_entry_is_linked(head));
	return frame;
}
 void place_head_Am(int frame){
	list_entry* entry = &coremap[frame].framelist_entry;
	list_add_head(&Am, entry);
	++(&Am)->size;
	assert(list_entry_is_linked(entry));
 }

 void place_head_A1(int frame){
	list_entry* entry = &coremap[frame].framelist_entry;
	list_add_head(&A1, entry);
	++(&A1)->size;
	assert(list_entry_is_linked(entry));
 }

int evict_tail_Am(){
	assert(Am.size > 0);
	struct list_entry *tail = Am.head.prev;
	assert(list_entry_is_linked(tail));
	struct frame *f = container_of(tail, struct frame, framelist_entry);
	assert(f->in_use);
	assert(get_referenced(f->pte));
	int frame = f-coremap;
	list_del(tail);
	--(&Am)->size;
	assert(!list_entry_is_linked(tail));
	return frame;
}

void evict_from_A1(int frame){
	list_entry* entry = &coremap[frame].framelist_entry;
	list_del(entry);
	assert(!list_entry_is_linked(entry));
	--(&A1)->size;
}

void evict_from_Am(int frame){
	list_entry* entry = &coremap[frame].framelist_entry;
	list_del(entry);
	assert(!list_entry_is_linked(entry));
	--(&Am)->size;
}

/* Page to evict is chosen using the simplified 2Q algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int s2q_evict(void)
{
	int ret;
	if (A1.size > threshold){
		ret = evict_head_A1();
	} else {
		ret = evict_tail_Am();
	}
	set_referenced(coremap[ret].pte,false);
	return ret;
}

/* This function is called on each access to a page to update any information
 * needed by the simplified 2Q algorithm.
 * Input: The page table entry and full virtual address (not just VPN)
 * for the page that is being accessed.
 */
void s2q_ref(int frame, vaddr_t vaddr)
{
	
	struct pt_entry_s *ptes = coremap[frame].pte;
	struct list_entry* entry = &coremap[frame].framelist_entry;
	bool case1 = get_referenced(ptes);
	
	if (case1){
		evict_from_Am(frame);
		place_head_Am(frame);

	} else if (!case1 && list_entry_is_linked(entry)){
		evict_from_A1(frame);
		place_head_Am(frame);
		set_referenced(coremap[frame].pte, true);

	} else {
		place_tail_A1(frame);
	}
	(void)vaddr;
}

/* Initialize any data structures needed for this replacement algorithm. */
void s2q_init(void)
{
	Am.size=0;
	A1.size=0;
	threshold = memsize/10;
	list_init(&Am);
	list_init(&A1);
}

/* Cleanup any data structures created in s2q_init(). */
void s2q_cleanup(void)
{
	list_init(&Am);
	list_init(&A1);
}
