#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stdio.h>
#include "thread.h"

/*
marker = 0
get_context
(if marker = 0){
	marker = 1;
	set(context)
}*/

// 1. a function for removing thread from front of queue
/* 2. update yield() function, return -1 if yield impossible i.e
		- queue is empty, you cannot yeild to another thread
		- the thread with want_tid doesn't exist in queue
 */ 

/* This is the wait queue structure, needed for Assignment 2. */ 
struct wait_queue {
	/* ... Fill this in Assignment 2 ... */
};

/* For Assignment 1, you will need a queue structure to keep track of the 
 * runnable threads. You can use the tutorial 1 queue implementation if you 
 * like. You will probably find in Assignment 2 that the operations needed
 * for the wait_queue are the same as those needed for the ready_queue.
 */

/* This is the thread control block. */
/* we need: rip, rsp, rbp, rdi, rsi*/
/* we need: tid, thread_state("ready"=0 or "running"=1)*/
struct thread {
	ucontext_t context;
	Tid Tid;
	int* stack_addr;
	int killed;
};

Tid running_thread;
Tid ready_queue[THREAD_MAX_THREADS];
Tid available_threads[THREAD_MAX_THREADS];
struct thread* created_threads[THREAD_MAX_THREADS];


int num_threads_created = 0;

int* zombie_stack_addr = NULL;
int returning_from_exit = 0;
Tid yield_to;
Tid zombie_tid = (Tid) -300;
/**************************************************************************
 * Assignment 1: Refer to thread.h for the detailed descriptions of the six
 *               functions you need to implement. 
 **************************************************************************/

void
thread_init(void)
{
	/* Add necessary initialization for your threads library here. */
        /* Initialize the thread control block for the first thread */
	/* 1. initialize thread_control_block*/
	/*what should stackPointer be for this thread? Null?*/
	struct thread* init_thread = malloc(sizeof(struct thread));
    
	getcontext(&(init_thread->context));
    init_thread->Tid = 0;
	init_thread->killed = 0;
	++ num_threads_created;

	/* 2. initialize the ready_queue*/
	running_thread = (Tid) 0;
	for (int i = 0; i < THREAD_MAX_THREADS-1; i++) {
        available_threads[i] = (Tid) (i+1);
    }
	available_threads[THREAD_MAX_THREADS-1] = (Tid) -300;

	created_threads[0] = init_thread;
    for (int i = 0; i<THREAD_MAX_THREADS; ++i){
        ready_queue[i] = (Tid) -300;
    }
	for (int i = 1; i < THREAD_MAX_THREADS-1; i++) {
        created_threads[i] = NULL;
    }

}

Tid
thread_id()
{
	return running_thread;
	// return THREAD_INVALID;
}

/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
        thread_main(arg); // call thread_main() function with arg
        thread_exit(0);
}

void remove_from_available(){
	for (int i = 0; i < THREAD_MAX_THREADS -1; ++i) {
        available_threads[i] = available_threads[i + 1];
    }
	available_threads[THREAD_MAX_THREADS-1] = (Tid) -300;
	
}

void add_to_queue_tail(Tid id){
    for (int i = 0; i < THREAD_MAX_THREADS; ++i){
        if (ready_queue[i] == (Tid) -300) {
            ready_queue[i] = id;
            break;
        }
    }
}

// you will dynamycally allocate space for the stack on the thread struct
// each thread should have at least THREAD_MIN_STACK bytes
/* when we create our thread struct, we will call getcontext() to create a valid context for it, and then before we put this thread on the queues
   we will adjust some registers in the thread to ensure it begins executing in the right place
		- rpc should point to a stub function (this is the first function the newly created thread runs)
		- there are two regesters for passed arguments, you need to update these registers to hold arguments passed to the stub function
		- you need to allocate a new per-thread stack using malloc(), and have the rsp register hold the address of that stack on the heap
		  it should point to the TOP of the new stack because the stack grows down*/
Tid
thread_create(void (*fn) (void *), void *parg)
{
	// if no more space for another thread -> THREAD_NO_MORE
	// if created_threads[THREAD_MAX_THREADS -1] != -1
	if(num_threads_created == THREAD_MAX_THREADS){return THREAD_NOMORE;}
	++num_threads_created;

	// turns out we do have space to create a thread
	struct thread* create_thread = malloc(sizeof(struct thread));
	getcontext(&(create_thread->context));

	// what is the tid of our thread?
	Tid create_thread_tid = available_threads[0];
	create_thread->Tid = create_thread_tid;
	remove_from_available();
	/* now we have to adjust some values in our context before*/
	// 1. rip has to point to stub function
	create_thread->context.uc_mcontext.gregs[REG_RIP] = (unsigned long) &thread_stub;
	// 2. save argument registers in context, these registers will hold the two arguments passed above
	create_thread->context.uc_mcontext.gregs[REG_RDI]= (unsigned long) fn;
	create_thread->context.uc_mcontext.gregs[REG_RSI]= (unsigned long) parg;
	// 3. need to allocate a stack using malloc
	int *lower_limit = malloc(THREAD_MIN_STACK);
	if (lower_limit == NULL) {return THREAD_NOMEMORY;}
	create_thread->stack_addr = lower_limit;
	create_thread->killed = 0;
	// align (unsigned long)lower_limit) + (unsigned long) (THREAD_MIN_STACK) first 
	unsigned long upper_limit = ((unsigned long)lower_limit) + (unsigned long) (THREAD_MIN_STACK) - (unsigned long) 8;
	// 4. change the saved stack pointer register in the context to point to the top of the new stack
	create_thread->context.uc_mcontext.gregs[REG_RSP] = (unsigned long) upper_limit;

	// add this thread to created_threads
	created_threads[(int)create_thread_tid] = create_thread;
	// add this thread to ready_queue
	add_to_queue_tail(create_thread_tid);
	// return tid of created thread
	if (create_thread_tid < 0){
		printf("CREATED_ID NEGATIVE: %d\n ITERATION: %d \n", create_thread_tid, num_threads_created);
	}
	return create_thread_tid;

}

Tid find_index_in_queue(Tid want_tid){
    for (int i = 0; i < THREAD_MAX_THREADS; ++i) {
        if (ready_queue[i] == want_tid) {
            return i; // Return the index if the target is found
        }
    }
    return -1; 
}

void remove_from_queue(int index) {
    // Shift elements to the left

    for (int i = index; i < THREAD_MAX_THREADS -1; ++i) {
        ready_queue[i] = ready_queue[i + 1];
    }
	ready_queue[THREAD_MAX_THREADS-1] = (Tid) -300;
    if (index < 0 || index >= THREAD_MAX_THREADS) {
        printf("Invalid index\n");
        return;
    }

}

void append_to_available(Tid val){
	for (int i=0; i<THREAD_MAX_THREADS; ++i){
		if (available_threads[i] == (Tid)-300){
			available_threads[i] = val;
			break;
		}
	}
}

Tid
thread_yield(Tid want_tid)
{
	/* store and restore the caller's state */
	/* save thread's context in it's struct*/
	/* take running_thread Tid and find the thread in created_threads list*/
	
    struct thread* curr_thread = created_threads[(int) running_thread];
	struct thread* new_thread;
	Tid new_thread_id;

    /* get context of that running_thread*/
	//get thread you need and set that context
	if (want_tid == THREAD_SELF){
		 return running_thread;
	} else if (want_tid == THREAD_ANY){
		
		// take first thread from ready queue
        // if queue is empty return THREAD_NONE
        Tid old_thread = running_thread;
        if (ready_queue[0] == (Tid) -300) {return THREAD_NONE;}
        Tid new_thread_tid = ready_queue[0];
        new_thread = created_threads[(int)new_thread_tid];
		
		running_thread = new_thread ->Tid;
        remove_from_queue(0);
        add_to_queue_tail(old_thread);
		new_thread_id = new_thread->Tid;

		int flag = 0;
		getcontext(&(curr_thread->context));
        if (zombie_stack_addr != NULL){
            free(zombie_stack_addr);
            zombie_stack_addr = NULL;
        }
        if (returning_from_exit == 1){
            Tid old_thread = running_thread;
            if (ready_queue[0] == (Tid) -300) {return THREAD_NONE;}
            Tid new_thread_tid = ready_queue[0];
            new_thread = created_threads[(int)new_thread_tid];
		
		    running_thread = new_thread ->Tid;
            remove_from_queue(0);
            add_to_queue_tail(old_thread);
		    new_thread_id = new_thread->Tid;
            returning_from_exit = 0;
        }
        if (created_threads[(int) running_thread]->killed == 1){
            thread_exit(-8);
        } 
        if (flag ==0){
		    flag=1;
		    setcontext(&(new_thread->context));
        }
		return new_thread_id;

	} else if (want_tid < 0) {
        return THREAD_INVALID;
    } else{
		
        // look for thread with Tid = want_tid in queue, if it is not in the queue retrun THREAD_INVALID
        if (running_thread == want_tid) {return thread_id();}
		Tid old_thread = running_thread;
        int index = find_index_in_queue(want_tid);
        if (index == -1) {return THREAD_INVALID;}
        new_thread = created_threads[(int) want_tid];

		running_thread = new_thread ->Tid;
		new_thread_id = new_thread->Tid;
        // remove new_thread from ready queue and shift values over and place yielded thread at
        // the tail of the queue
        remove_from_queue(index);
        add_to_queue_tail(old_thread);

		int flag = 0;
		getcontext(&(curr_thread->context));
        if (zombie_stack_addr != NULL){
            free(zombie_stack_addr);
            zombie_stack_addr = NULL;
        }
        if ((returning_from_exit == 1) && (zombie_tid == want_tid)){
            zombie_tid = (Tid) -300;
            return THREAD_INVALID;
        }
        if (created_threads[running_thread]->killed == 1){
            thread_exit(-8);
        }
        // set context
        if (flag ==0){
		    flag=1;
		    setcontext(&(new_thread->context));
        }
    }
	return new_thread_id;

}

Tid find_tail_of_queue(){
	for (int i=0; i<THREAD_MAX_THREADS-1; ++i){
		if (ready_queue[i+1] == (Tid)-300){
			Tid want_tid = ready_queue[i];
			ready_queue[i] = (Tid) -300;
			return want_tid;
		}
	}
	printf("ERROR IN FINDING TAIL OF QUEUE, CHECK EXIT FUNCTION");
	return -1;
}

void
thread_exit(int exit_code)
{
	if (ready_queue[0] == (Tid)-300){exit(0);}
    else{
        zombie_tid = running_thread;
        append_to_available(running_thread);
        --num_threads_created;
        zombie_stack_addr = created_threads[(int) running_thread]-> stack_addr;
        free(created_threads[(int)running_thread]);
        created_threads[(int)running_thread] = NULL;

        Tid new_context_tid = find_tail_of_queue();
        int i = find_index_in_queue(new_context_tid);
        remove_from_queue(i);
        running_thread = new_context_tid;
        returning_from_exit = 1;
        setcontext(&(created_threads[(int)running_thread]->context));
    }
}

Tid
thread_kill(Tid tid)
{
	// check if thread exists, if not return THREAD_INVALID
	if (tid <= 0){
		return THREAD_INVALID;
	} else if (created_threads[(int) tid] == NULL){
		return THREAD_INVALID;
	} else if (tid == running_thread){
		return THREAD_INVALID;
	} else {
		struct thread* t = created_threads[(int)tid];
		t->killed = 1;
		return tid;
	}
	
}

/**************************************************************************
 * Important: The rest of the code should be implemented in Assignment 2. *
 **************************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc_csc369(sizeof(struct wait_queue));
	assert(wq);

	TBD();

	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	TBD();
	free_csc369(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	TBD();
	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	TBD();
	return 0;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid, int *exit_code)
{
	TBD();
	return 0;
}

struct lock {
	/* ... Fill this in ... */
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc_csc369(sizeof(struct lock));
	assert(lock);

	TBD();

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	TBD();

	free_csc369(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

struct cv {
	/* ... Fill this in ... */
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc_csc369(sizeof(struct cv));
	assert(cv);

	TBD();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	TBD();

	free_csc369(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}
