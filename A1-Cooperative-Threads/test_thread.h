#ifndef _TEST_THREAD_H_
#define _TEST_THREAD_H_

#include <stdbool.h>

#define DURATION  60000000
#define NTHREADS       128
#define LOOPS	        10

extern unsigned long get_current_bytes_malloced();
extern unsigned long get_current_num_malloced();
extern unsigned long get_num_malloced();
extern unsigned long get_bytes_malloced();
extern void init_csc369_malloc();
extern bool is_leak_free();

extern int set_flag(int value);

#endif /* _TEST_THREAD_H_ */
