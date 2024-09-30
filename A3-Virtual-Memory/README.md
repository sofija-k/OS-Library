# Virtual Memory: Virtual to Physical Translation

## Address Translation and Paging
Implement virtual-to-physical address translation and demand paging using a pagetable design of your choice in `pagetable.c` and `pagetable.h`.

The main driver for the memory simulator, `sim.c`, reads memory reference traces in the format produced by the simify-trace.py tool from trimmed, reduced valgrind memory traces. For each line in the trace, the program asks for the simulated physical page frame that corresponds to the given virtual address by calling find_physpage, and then reads from the simulated physical memory at the location given by the physical frame number and the page offset. If the access type is a write ('M' for modify or 'S' for store), it will also write the value from the trace to the location. 

The simulator is executed as `./sim -f <tracefile> -m <memory size> -s <swapfile size> -a <replacement algorithm>` where memory size and swapfile size are the number of frames of simulated physical memory and the number of pages that can be stored in the swapfile, respectively. The swapfile size should be as large as the number of unique virtual pages in the trace. 
####

### There are Four Main Data Structures Used:

#### 1. unsigned char *physmem: 
This is the space for our simulated physical memory. We define a simulated page frame size of `SIMPAGESIZE` and allocate `SIMPAGESIZE` * "memory size" bytes for physmem.

#### 2. struct frame *coremap: 
The coremap array represents the state of (simulated) physical memory. Each element of the array represents a physical page frame. It records if the physical frame is in use and, if so, a pointer to the page table entry for the virtual page that is using it.

#### 3. struct pt_entry: 
A page table entry, which records the frame number if the virtual page is in (simulated) physical memory and an offset into the swap file if the page has been written out to swap. It must also contain flags to represent whether the entry is Valid, Dirty, and Referenced. 

#### 4. swap.c: 
The swapfile functions are all implemented in this file, along with bitmap functions to track free and used space in the swap file, and to move virtual pages between the swapfile and (simulated) physical memory. The swap_pagein and swap_pageout functions take a frame number and a swap offset as arguments. The simulator code creates a temporary file in the current directory where it is executed to use as the swapfile, and removes this file as part of the cleanup when it completes. 
>CAUTION:
>It does not, however, remove the temporary file if the simulator crashes or exits early due to a detected error. Instead it manually removes the swapfile.XXXXXX files in this case.

## Replacement Algorithms

Implement Round Robin, CLOCK (with one ref-bit) and simplified 2Q page replacement algorithms.

Space complexities for all policies are O(1). Time complexities are shown below: 

| | Round Robin | Clock | 2Q | 
| --- | :---: | :---: | :---: |
| **Init**|O(1)|O(1)|O(1) 
| **Evict** |O(1)|O(M)|O(1)
|**Ref** |O(1)|O(1)|O(1)

> M = size of memory

#### 

<br/>When a page is being evicted, there are only 2 possibilities: (1) The page is dirty and needs to be written to the swap; or (2) The page is clean and already has a copy in the swap. A newly initialized page (zero-filled) should be marked dirty on the very first access.

**CLOCK** uses the "Referenced" flag stored in the page table entry. Implemented functions in `pagetable.c` get the values of the Valid, Dirty, and Referenced flags given a page table entry. These functions are used in the replacement algorithm implementations to perform necessary flag checks.

The simulator writes a value into the simulated physical memory pages for Store or Modify references, and checks that simulated physical memory contains the last written value on Load or Instruction references. If there is a mismatch, the simulator prints an error message. These errors indicate that there is something wrong with the address translation implementation. 




