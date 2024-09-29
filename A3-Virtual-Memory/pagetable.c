/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Andrew Peterson, Karen Reid, Alexey Khrabrov, Angela Brown, Kuei Sun
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019, 2021 Karen Reid
 * Copyright (c) 2023, Angela Brown, Kuei Sun
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "malloc369.h"
#include "sim.h"
#include "coremap.h"
#include "swap.h"
#include "pagetable.h"

// Counters for various events.
// Your code must increment these when the related events occur.
size_t hit_count = 0;
size_t miss_count = 0;
size_t ref_count = 0;
size_t evict_clean_count = 0;
size_t evict_dirty_count = 0;

// Accessor functions for page table entries, to allow replacement
// algorithms to obtain information from a PTE, without depending
// on the internal implementation of the structure.

struct top_level_entry top_level_arr[4096];
size_t num_frames_used = 0;

/* Returns true if the pte is marked valid, otherwise false */
bool is_valid(pt_entry_t *pte)
{
	if (pte == NULL) {return false;}
	if (pte->valid_bit == 1) {return true;} else {
		return false;
	}
}

/* Returns true if the pte is marked dirty, otherwise false */
bool is_dirty(pt_entry_t *pte)
{
	if (pte == NULL) {return false;}
	if (pte->dirty_bit == 1) {return true;} else {
		return false;
	}
}

/* Returns true if the pte is marked referenced, otherwise false */
bool get_referenced(pt_entry_t *pte)
{
	if (pte->referenced_bit == 1) {
		return true;
	} else {
		return false;
	}
}

/* Sets the 'referenced' status of the pte to the given val */
void set_referenced(pt_entry_t *pte, bool val)
{
	if (val == true){
		pte->referenced_bit = 1;
	} else {
		pte->referenced_bit = 0;
	}
}

/*
 * Initializes your page table.
 * This function is called once at the start of the simulation.
 * For the simulation, there is a single "process" whose reference trace is
 * being simulated, so there is just one overall page table.
 *
 * In a real OS, each process would have its own page table, which would
 * need to be allocated and initialized as part of process creation.
 * 
 * The format of the page table, and thus what you need to do to get ready
 * to start translating virtual addresses, is up to you. 
 */
void init_pagetable(void)
{
	for (int i =0; i<4096; ++i){
		top_level_arr[i].second_level_arr = NULL;
	}
	
}

/*
 * Write virtual page represented by pte to swap, if needed, and update 
 * page table entry.
 *
 * Called from                                    me() in coremap.c after a victim page frame has
 * been selected. 
 *
 * Counters for evictions should be updated appropriately in this function.
 */
void handle_evict(pt_entry_t * pte)
{
	int old_frame = pte->frame_number;
	pte -> valid_bit = 0;
	pte -> swap_on = 1;
	pte -> frame_number = -1;
	if (pte->dirty_bit == 0){
		++evict_clean_count;
		return;
	} else if (pte->swap_offset == -300){
		pte->dirty_bit = 0;
		++evict_dirty_count;
		pte->swap_offset = swap_pageout(old_frame, INVALID_SWAP);
	} else {
		pte->dirty_bit = 0;
		++evict_dirty_count;
		swap_pageout(old_frame, pte->swap_offset);
	}
}

/*
 * Locate the physical frame number for the given vaddr using the page table.
 *
 * If the page table entry is invalid and not on swap, then this is the first 
 * reference to the page and a (simulated) physical frame should be allocated 
 * and initialized to all zeros (using init_frame from coremap.c).
 * If the page table entry is invalid and on swap, then a (simulated) physical 
 * frame should be allocated and filled by reading the page data from swap.
 *
 * Make sure to update page table entry status information:
 *  - the page table entry should be marked valid
 *  - if the type of access is a write ('S'tore or 'M'odify),
 *    the page table entry should be marked dirty
 *  - a page should be marked dirty on the first reference to the page,
 *    even if the type of access is a read ('L'oad or 'I'nstruction type).
 *  - DO NOT UPDATE the page table entry 'referenced' information. That
 *    should be done by the replacement algorithm functions.
 *
 * When you have a valid page table entry, return the page frame number
 * that holds the requested virtual page.
 *
 * Counters for hit, miss and reference events should be incremented in
 * this function.
 */
int find_frame_number(vaddr_t vaddr, char type)
{
	++ref_count;
	// seperate first three inidices into vpn1, vpn2, vpn3
	vaddr_t vpn = (vaddr >> 12) & 0xFFFFFFFFF;
	vaddr_t vpn3 = vpn & 0xFFF;
	vaddr_t vpn2 = (vpn >> 12) & 0xFFF;
	vaddr_t vpn1 = (vpn >> 24) & 0xFFF;

	//set up page table
	if (top_level_arr[vpn1].second_level_arr == NULL){
		++miss_count;
		top_level_arr[vpn1].second_level_arr = malloc369(4096 * sizeof(struct second_level_entry));

		struct second_level_entry l2_index;
		l2_index.third_level_arr = malloc369(4096* sizeof(struct third_level_entry));
		top_level_arr[vpn1].second_level_arr[vpn2] = l2_index;

		struct pt_entry_s *pte_s = malloc369(sizeof(struct pt_entry_s));

		// define pte
		pte_s -> valid_bit = 1;
		pte_s -> dirty_bit = 1;
		pte_s -> swap_on = 0;
		pte_s -> swap_offset = -300;
		pte_s -> referenced_bit = 0;

		// put pte on coremap and update pte->frame_number
		// if swap_on == 1 and valid_bit == 0, then swap_pagein
		int frame = allocate_frame(pte_s);
		init_frame(frame);
		pte_s -> frame_number = frame;

		struct third_level_entry entry_z;
		entry_z.pte = pte_s;

		top_level_arr[vpn1].second_level_arr[vpn2].third_level_arr[vpn3] = entry_z;

		return frame;
	} else if (top_level_arr[vpn1].second_level_arr[vpn2].third_level_arr == NULL){
		++miss_count;
		struct second_level_entry l2_index;
		l2_index.third_level_arr = malloc369(4096* sizeof(struct third_level_entry));
		top_level_arr[vpn1].second_level_arr[vpn2] = l2_index;
		struct pt_entry_s *pte_s = malloc369(sizeof(struct pt_entry_s));

		// define pte
		pte_s -> valid_bit = 1;
		pte_s -> dirty_bit = 1;
		pte_s -> swap_on = 0;
		pte_s -> swap_offset = -300;
		pte_s -> referenced_bit = 0;

		int frame = allocate_frame(pte_s);
		init_frame(frame);
		pte_s -> frame_number = frame;
		struct third_level_entry entry_z;
		entry_z.pte = pte_s;
		top_level_arr[vpn1].second_level_arr[vpn2].third_level_arr[vpn3] = entry_z;
		//top_level_arr[vpn1].second_level_arr[vpn2].third_level_arr[vpn3].pte = pte_s;
		return frame;

	} else if (top_level_arr[vpn1].second_level_arr[vpn2].third_level_arr[vpn3].pte == NULL){
		++miss_count;
		struct pt_entry_s *pte_s = malloc369(sizeof(struct pt_entry_s));
		// define pte
		pte_s -> valid_bit = 1;
		pte_s -> dirty_bit = 1;
		pte_s -> swap_on = 0;
		pte_s -> swap_offset = -300;
		pte_s -> referenced_bit = 0;

		int frame = allocate_frame(pte_s);
		init_frame(frame);
		pte_s -> frame_number = frame;
		struct third_level_entry entry_z;
		entry_z.pte = pte_s;
		top_level_arr[vpn1].second_level_arr[vpn2].third_level_arr[vpn3] = entry_z;
		//top_level_arr[vpn1].second_level_arr[vpn2].third_level_arr[vpn3].pte = pte_s;
		return frame;

	} else {
		// already written to memory or in swap
		struct pt_entry_s* pt_entry= top_level_arr[vpn1].second_level_arr[vpn2].third_level_arr[vpn3].pte;
		// if it's in memory
		if (pt_entry->valid_bit == 1){
			assert(pt_entry->swap_on == 0);
			assert(pt_entry->frame_number != -1);
			++hit_count;
			if (type == 'S' || type == 'M'){pt_entry->dirty_bit = 1;}
			pt_entry -> valid_bit = 1;
			return pt_entry -> frame_number;
		// virtual address was swapped out
		} else {
			assert (pt_entry -> valid_bit == 0);
			assert (pt_entry->swap_on == 1);
			++miss_count;
			// find a frame for pt_entry
			int frame = allocate_frame(pt_entry);
			// swap page into memory
			swap_pagein(frame, pt_entry->swap_offset);
			// update pt_entry bits
			pt_entry->valid_bit = 1;
			pt_entry->swap_on = 0;
			assert(pt_entry->dirty_bit == 0);
			if (type == 'S' || type == 'M'){pt_entry->dirty_bit = 1;}
			pt_entry->frame_number = frame;
			return frame;
		}
	}
	printf("!!! WE SHOULD NOT GET HERE !!!\n");
	return -1;
}

void print_pagetable(void)
{
	for (int i = 0; i<4096; ++i){
		if (top_level_arr[i].second_level_arr == NULL){continue;}
		//struct top_level_entry entry_x = top_level_arr[i];

		for (int j=0; j<4096; ++j){
			if (top_level_arr[i].second_level_arr[j].third_level_arr == NULL){continue;}
			//assert(top_level_arr[i].second_level_arr[j].third_level_arr != NULL);
			//struct second_level_entry entry_y = top_level_arr[i].second_level_arr[j];

			for (int k =0; k<4096; ++k){
				if (top_level_arr[i].second_level_arr[j].third_level_arr[k].pte == NULL){continue;}
				//assert(top_level_arr[i].second_level_arr[j].third_level_arr[k].pte != NULL);
				//struct third_level_entry entry_z = top_level_arr[i].second_level_arr[j].third_level_arr[k];
				struct pt_entry_s *pte = top_level_arr[i].second_level_arr[j].third_level_arr[k].pte;
				printf("frameNum: %d, Virtual Address: %d %d %d\n",pte->frame_number, i, j, k);
			}
		}
	}
}


void free_pagetable(void)
{
	for (int i = 0; i<4096; ++i){
		if (top_level_arr[i].second_level_arr == NULL){continue;}
		//struct top_level_entry entry_x = top_level_arr[i];

		for (int j=0; j<4096; ++j){
			if (top_level_arr[i].second_level_arr[j].third_level_arr == NULL){continue;}
			//assert(top_level_arr[i].second_level_arr[j].third_level_arr != NULL);
			//struct second_level_entry entry_y = top_level_arr[i].second_level_arr[j];

			for (int k =0; k<4096; ++k){
				if (top_level_arr[i].second_level_arr[j].third_level_arr[k].pte == NULL){continue;}
				//assert(top_level_arr[i].second_level_arr[j].third_level_arr[k].pte != NULL);
				//struct third_level_entry entry_z = top_level_arr[i].second_level_arr[j].third_level_arr[k];
				free369(top_level_arr[i].second_level_arr[j].third_level_arr[k].pte);
				top_level_arr[i].second_level_arr[j].third_level_arr[k].pte = NULL;
			}
			free369(top_level_arr[i].second_level_arr[j].third_level_arr);
			top_level_arr[i].second_level_arr[j].third_level_arr = NULL;	
		}
		free369(top_level_arr[i].second_level_arr);
		top_level_arr[i].second_level_arr = NULL;
	}
}
