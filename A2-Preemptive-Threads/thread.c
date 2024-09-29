#include <assert.h>
#include "malloc369.h"
#include <stdlib.h>
#include <ucontext.h>
#include <stdio.h>
#include "thread.h"
#include "interrupt.h"

/* This is the wait queue structure, needed for Assignment 2. */ 
struct waiting_thread{
	Tid data;
	struct waiting_thread* next;
};

struct wait_queue {
	struct waiting_thread* head;
	struct waiting_thread* tail;
	int size;
	int id;
};

/* For Assignment 1, you will need a queue structure to keep track of the 
 * runnable threads. You can use the tutorial 1 queue implementation if you 
 * like. You will probably find in Assignment 2 that the operations needed
 * for the wait_queue are the same as those needed for the ready_queue.
 */

/* This is the thread control block. */
struct thread {
	ucontext_t context;
	Tid Tid;
	int* stack_addr;
	bool killed;
	bool sleeping;
	struct wait_queue* wq;
	Tid waiting_on;
	int exit;
	Tid parent;
	bool stack_freed;
};

Tid running_thread;
Tid ready_queue[THREAD_MAX_THREADS];
Tid available_threads[THREAD_MAX_THREADS];
struct thread* created_threads[THREAD_MAX_THREADS];
struct wait_queue* all_wait_queues[THREAD_MAX_THREADS*THREAD_MAX_THREADS];


int num_threads_created = 0;
bool returning_from_exit = false;
int* zombie_stack_addr = NULL;
Tid zombie_tid = (Tid) -300;
int num_wait_queues = 0;

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
	struct thread* init_thread = malloc369(sizeof(struct thread));

	getcontext(&(init_thread->context));

    init_thread->Tid = 0;
	init_thread->killed = false;
	++ num_threads_created;
	init_thread -> sleeping = false;
	init_thread->wq = NULL;
	init_thread -> waiting_on = -300;
	init_thread -> exit = -300;
	init_thread->parent = 0;
	init_thread->stack_freed = false;

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
}

/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */

void
freeup_leftover_zombies();

void 
thread_cleanup_zombie(bool freed_stack);

void
thread_routine_cleanup();

void
thread_stub(void (*thread_main)(void *), void *arg)
{
		thread_routine_cleanup();
		interrupts_on();
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

Tid
thread_create(void (*fn) (void *), void *parg)
{
	int e = interrupts_off();
	// if no more space for another thread -> THREAD_NO_MORE
	// if created_threads[THREAD_MAX_THREADS -1] != -1
	if(num_threads_created == THREAD_MAX_THREADS){
		interrupts_set(e);
		return THREAD_NOMORE;}
	++num_threads_created;

	// turns out we do have space to create a thread
	struct thread* create_thread = malloc369(sizeof(struct thread));

	assert (!interrupts_enabled());
	getcontext(&(create_thread->context));
	
	// what is the tid of our thread?
	Tid create_thread_tid = available_threads[0];
	remove_from_available();
	create_thread->Tid = create_thread_tid;
	/* now we have to adjust some values in our context before*/
	// 1. rip has to point to stub function
	create_thread->context.uc_mcontext.gregs[REG_RIP] = (unsigned long) &thread_stub;
	// 2. save argument registers in context, these registers will hold the two arguments passed above
	create_thread->context.uc_mcontext.gregs[REG_RDI]= (unsigned long) fn;
	create_thread->context.uc_mcontext.gregs[REG_RSI]= (unsigned long) parg;
	// 3. need to allocate a stack using malloc
	int *lower_limit = malloc369(THREAD_MIN_STACK);
	if (lower_limit == NULL) {
		interrupts_set(e);
		return THREAD_NOMEMORY;}
	create_thread->stack_addr = lower_limit;
	create_thread->killed = false;
	create_thread -> sleeping = false;
	create_thread->wq = NULL;
	create_thread -> waiting_on = -300;
	create_thread -> exit = -300;
	create_thread ->parent = running_thread;
	create_thread->stack_freed = false;
	// align (unsigned long)lower_limit) + (unsigned long) (THREAD_MIN_STACK) first 
	unsigned long upper_limit = ((unsigned long)lower_limit) + (unsigned long) (THREAD_MIN_STACK) - (unsigned long) 8;
	// 4. change the saved stack pointer register in the context to point to the top of the new stack
	create_thread->context.uc_mcontext.gregs[REG_RSP] = (unsigned long) upper_limit;

	// add this thread to created_threads
	created_threads[(int)create_thread_tid] = create_thread;
	// add this thread to ready_queue
	add_to_queue_tail(create_thread_tid);
	// return tid of created thread
	interrupts_set(e);
	return create_thread_tid;

}

void 
append_to_available(Tid val){
	for (int i=0; i<THREAD_MAX_THREADS; ++i){
		if (available_threads[i] == (Tid)-300){
			available_threads[i] = val;
			break;
		}
	}
}

void 
thread_create_zombie(Tid thread){
    zombie_tid = thread;
    zombie_stack_addr = created_threads[(int)thread]->stack_addr;
}

void 
cleanup_before_zombifying(Tid zombie){
	/* CLEANUP WAITS BEFORE EXITING*/
	struct thread* zombie_thread = created_threads[(int)zombie];
	if (zombie_thread ->waiting_on != -300){
		Tid waiting_for = zombie_thread ->waiting_on;
		created_threads[waiting_for] -> wq -> head = NULL;
		created_threads[waiting_for] -> wq -> tail = NULL;
		created_threads[waiting_for] -> wq -> size = 0;
	}
	if (zombie_thread ->wq != NULL && zombie_thread->wq->head != NULL){
		thread_wakeup(zombie_thread->wq, false);
	}
}

void 
thread_cleanup_zombie(bool freed_stack){
	append_to_available(zombie_tid);
    assert(zombie_tid != (Tid)-300);
	wait_queue_destroy(created_threads[(int) zombie_tid]->wq);
	created_threads[(int) zombie_tid]->wq = NULL;
	assert(created_threads[(int) zombie_tid]->wq == NULL);

	if (!freed_stack){
		free369(created_threads[(int)zombie_tid]->stack_addr);
		created_threads[(int)zombie_tid]->stack_addr = NULL;
	}
	assert (created_threads[(int)zombie_tid]->stack_addr == NULL);

    free369(created_threads[(int) zombie_tid]);
    created_threads[(int) zombie_tid] = NULL;
    zombie_tid = (Tid)-300;

    if (zombie_stack_addr != NULL){
		free369(zombie_stack_addr);
    	zombie_stack_addr = NULL;
	}
	
}


void
thread_routine_cleanup(){
	// check if running thread is exited
	if (zombie_tid != -300){ 
		//cleanup stack pointer
		free369(created_threads[(int)zombie_tid]->stack_addr);
		created_threads[(int)zombie_tid]->stack_addr = NULL;
		created_threads[(int)zombie_tid]->stack_freed = true;
		zombie_tid = (Tid)-300;
		zombie_stack_addr = NULL;
	}
	interrupts_off();
    if (created_threads[(int) running_thread]->killed){
        thread_exit(-SIGKILL);
    }
}

Tid 
find_index_in_queue(Tid want_tid){
    for (int i = 0; i < THREAD_MAX_THREADS; ++i) {
        if (ready_queue[i] == want_tid) {
            return i; // Return the index if the target is found
        }
    }
    return -1; 
}

void 
remove_from_queue(int index) {

    for (int i = index; i < THREAD_MAX_THREADS -1; ++i) {
        ready_queue[i] = ready_queue[i + 1];
    }
	ready_queue[THREAD_MAX_THREADS-1] = (Tid) -300;
    if (index < 0 || index >= THREAD_MAX_THREADS) {
        return;
    }

}

Tid find_tail_of_queue(){
	for (int i=0; i<THREAD_MAX_THREADS-1; ++i){
		if (ready_queue[i+1] == (Tid)-300){
			Tid want_tid = ready_queue[i];
			ready_queue[i] = (Tid) -300;
			return want_tid;
		}
	}
	return -1;
}

Tid
thread_yield(Tid want_tid)
{
	int e = interrupts_off();
	/* ERROR CHECKING*/
    if (want_tid == THREAD_SELF) {
		interrupts_set(e);
        return running_thread;
    } else if (want_tid == THREAD_ANY){
        if (ready_queue[0] == (Tid)-300) {
			interrupts_set(e);
			return THREAD_NONE;}
    } else if (want_tid < 0){ 
		interrupts_set(e);
        return THREAD_INVALID;
    } else {
        if (running_thread ==  want_tid){ 
			interrupts_set(e);
			return thread_id();}
        int index = find_index_in_queue(want_tid);
        if (index == -1){
			interrupts_set(e);
			return THREAD_INVALID;}
    }


    /* FIND THREAD YOU WANT TO YIELD TO*/
    int new_thread_tid;
    if (want_tid == THREAD_ANY){
        new_thread_tid = ready_queue[0];
        remove_from_queue(0);
    } else {
        new_thread_tid = want_tid;
        int index = find_index_in_queue(want_tid);
        remove_from_queue(index);
    }

    /* YIELDING */
    Tid run_thread = running_thread;
	if (created_threads[(int) running_thread] -> sleeping != true) {add_to_queue_tail(run_thread);}
    running_thread = new_thread_tid;
    bool setcontext_called = false;
	assert (!interrupts_enabled());
    getcontext(&(created_threads[(int)run_thread]->context));

    if (setcontext_called){
        thread_routine_cleanup();
    } else {
        setcontext_called = true;
		assert (!interrupts_enabled());
		assert (created_threads[(int)new_thread_tid]->sleeping == false);
        setcontext(&(created_threads[(int)new_thread_tid]->context));
    }
	interrupts_set(e);
    return new_thread_tid;
}

void
freeup_leftover_zombies(){
	int e = interrupts_off();
	if (ready_queue[0]== (Tid)-300){
		for (int i = 0; i<THREAD_MAX_THREADS; ++i){
			if (created_threads[i] != NULL && i != running_thread){
				thread_create_zombie(i);
				thread_cleanup_zombie(created_threads[i]->stack_freed);
				
			}
		}
	}
	interrupts_set(e);
}

void
thread_exit(int exit_code)
{
	interrupts_off();
	cleanup_before_zombifying(running_thread);
	if (ready_queue[0] == (Tid)-300){
		freeup_leftover_zombies();
		exit(0);}
    else{
		created_threads[(int)running_thread]->exit = exit_code;
		--num_threads_created;
        thread_create_zombie(running_thread);
		running_thread = ready_queue[0];
		remove_from_queue(0);
		returning_from_exit = true;
		assert (!interrupts_enabled());
        setcontext(&(created_threads[(int)running_thread]->context));
    }
}

Tid
thread_kill(Tid tid)
{
	int e = interrupts_off();
	int i = find_index_in_queue(tid);
	// check if thread exists, if not return THREAD_INVALID
	if (tid == running_thread) {
		interrupts_set(e);
		return THREAD_INVALID;
	} else if (tid < 0 || tid >= THREAD_MAX_THREADS){
		interrupts_set(e);
		return THREAD_INVALID;
	} else if (i == -1 && created_threads[tid]!=NULL && created_threads[tid]->sleeping == false){
		interrupts_set(e);
		return THREAD_INVALID;
	} else if (created_threads[(int) tid] == NULL){
		interrupts_set(e);
		return THREAD_INVALID;
	} else if (tid == zombie_tid || created_threads[(int) tid]->killed){
		interrupts_set(e);
		return THREAD_INVALID;
	} else {
		struct thread* t = created_threads[(int)tid];
		t->killed = true;
		if (t->wq != NULL && t->wq->head != NULL){
			thread_wakeup(t->wq, false);
		} 
	}
	interrupts_set(e);
	return tid;
}

/**************************************************************************
 * Important: The rest of the code should be implemented in Assignment 2. *
 **************************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	int e = interrupts_off();
	struct wait_queue *wq;
	wq = malloc369(sizeof(struct wait_queue));
	assert(wq);

	// wq->head = malloc369(sizeof(struct waiting_thread));
	// wq->tail = malloc369(sizeof(struct waiting_thread));
	wq->head = NULL;
	wq->tail = NULL;
	wq->size = 0;

	interrupts_set(e);
	return wq;
}

void
put_to_sleep(struct wait_queue *wq, Tid thread_id){
	int e = interrupts_off();
	created_threads[(int) running_thread]->sleeping = true;
	struct waiting_thread *waiting = malloc369(sizeof(struct waiting_thread));
	waiting->data = thread_id;
	waiting->next = NULL;
	if (wq->head == NULL){
		wq->head = waiting;
		wq->tail = waiting;
	} else {
		wq->tail->next = waiting;
		wq->tail = waiting;
	}
	++ wq->size;
	interrupts_set(e);
}

int
wakeup(struct wait_queue *wq){
	int e = interrupts_off();
	if (wq == NULL || wq->head == NULL){
		interrupts_set(e);
		return 0;
	} else {
		struct waiting_thread *first_out = wq->head;
		Tid awoken_thread = first_out->data;
		created_threads[(int) awoken_thread]->sleeping = false;
		created_threads[(int) awoken_thread] -> waiting_on = (Tid)-300;
		add_to_queue_tail(awoken_thread);

		--wq->size;
		wq->head = first_out->next;
		if (wq->head == NULL){ wq->tail = NULL;}

		free369(first_out);
		first_out = NULL;
		assert (wq != NULL);

		interrupts_set(e);
		return 1;
	}
}

int 
wakeup_all(struct wait_queue *wq){
	int e = interrupts_off();
	if (wq == NULL){
		interrupts_set(e);
		return 0;
	}
	int count = 0;
	while (wq->size > 0){
		count = count + wakeup(wq);
	}
	interrupts_set(e);
	return count;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	int e = interrupts_off();
	if (wq != NULL) {
		assert (wq->head == NULL);
		assert (wq->size == 0);
	}
	free369(wq);
	wq = NULL;
	interrupts_set(e);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	int e = interrupts_off();
	/* ERROR CHECK*/
	if (queue == NULL){
		interrupts_set(e);
		return THREAD_INVALID;
	} else if (ready_queue[0] == (Tid)-300){
		interrupts_set(e);
		return THREAD_NONE;
	}
	/* PUT RUNNING_THREAD in WAIT QUEUE */
	put_to_sleep(queue, running_thread);
	assert (queue != NULL);
	/* THREAD YIELD(THREAD_ANY)
	   - make sure you don't place running thread back into ready_queue
	   - set up flag sleeping in thread, and when thread_sleep is called place it to true
	   - in thread_yield check if thread is sleeping, and thread->sleeping == false place it the ready queue
	   - do we ever place the only running_thread to sleep???
	*/
	assert (queue != NULL);
	Tid yielded = thread_yield(THREAD_ANY);
	assert (queue != NULL);
	interrupts_set(e);
	return yielded;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	int e = interrupts_off();
	int num_awoken;
	if (all){
		num_awoken = wakeup_all(queue);
		interrupts_set(e);
		return num_awoken;
	} else {
		num_awoken = wakeup(queue);
		interrupts_set(e);
		return num_awoken;
	}
	
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid, int *exit_code)
{
	int e = interrupts_off();

	/* ERROR CHECKING, RETURN THREAD INVALID*/
	if ((int) tid < 0 || (int) tid>=THREAD_MAX_THREADS){
		interrupts_set(e);
		return THREAD_INVALID;
	} else if (tid == running_thread) {
		interrupts_set(e);
		return THREAD_INVALID;
	} else if (created_threads[(int)tid]== NULL){
		interrupts_set(e);
		return THREAD_INVALID;
	} else if (created_threads[(int)tid]->wq != NULL){
		if (created_threads[(int)tid]->wq->head != NULL){
		interrupts_set(e);
		return THREAD_INVALID;} 
	} else if (created_threads[(int)tid]->killed == true){
		interrupts_set(e);
		return THREAD_INVALID;
	}

	if (created_threads[(int)tid]->exit == -300){
		created_threads[(int) running_thread]-> sleeping = true;
	} else {created_threads[(int) running_thread]-> sleeping = false;}

	if (created_threads[(int) running_thread]-> sleeping){
		if (created_threads[(int) tid]-> wq == NULL){
			created_threads[(int) tid]-> wq = wait_queue_create();
		}
		created_threads[(int) running_thread]-> waiting_on = tid;
		thread_sleep(created_threads[(int) tid]-> wq);
	}

	// if this thread waited for a thread thaat has been killed, exit_code = -SIGKILL
	assert(created_threads[(int)tid]->exit != -300);
	if (exit_code != NULL){*exit_code = created_threads[(int)tid]->exit;}
	thread_create_zombie(tid);
	--num_threads_created;
	thread_cleanup_zombie(created_threads[(int)tid]->stack_freed);
	assert (created_threads[(int)running_thread]->waiting_on == -300);
	interrupts_set(e);
	return tid;
}

struct lock {
	struct wait_queue* wq;
	Tid held_by;
	bool free;
};

struct lock *
lock_create()
{
	int e = interrupts_off();
	struct lock *lock;

	lock = malloc369(sizeof(struct lock));
	assert(lock);
	
	lock ->wq = wait_queue_create();
	lock->held_by = (Tid)-300;
	lock->free = true;
	interrupts_set(e);
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	int e = interrupts_off();
	assert(lock != NULL);
	// check lock is available when it's destroyed
	assert (lock->free);
	wait_queue_destroy(lock->wq);

	free369(lock);
	lock = NULL;
	interrupts_set(e);
}

void
lock_acquire(struct lock *lock)
{
	int e = interrupts_off();
	assert(lock != NULL);
	while (!lock->free){
		thread_sleep(lock->wq);
	}
	lock->held_by = running_thread;
	lock->free = false;
	interrupts_set(e);
}

void
lock_release(struct lock *lock)
{
	int e = interrupts_off();

	lock->held_by = (Tid)-300;
	lock->free = true;
	thread_wakeup(lock->wq, false);

	interrupts_set(e);
}

struct cv {
	bool condition_reach;
	struct wait_queue* wq;
	int num_waiting;
};

struct cv *
cv_create()
{
	int e = interrupts_off();
	struct cv *cv;
	cv = malloc369(sizeof(struct cv));
	assert(cv);

	cv->condition_reach = true;
	cv-> wq = wait_queue_create();
	cv->num_waiting = 0;

	interrupts_set(e);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	int e = interrupts_off();
	assert(cv != NULL);
	assert (cv->wq != NULL);
	free369(cv->wq);
	cv->wq = NULL;
	free369(cv);
	interrupts_set(e);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	if (lock->held_by != running_thread){
		printf("thread doesn't hold the lock, can't execute cv_wait");
		return;}
	assert (lock->held_by == running_thread);
	lock_release(lock);
	++cv->num_waiting;
	thread_sleep(cv->wq);
	lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	int e = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);
	if (cv->wq->head == NULL){
		interrupts_set(e);
		return;}
	Tid thread_id = cv->wq->head->data;
	--cv->num_waiting;
	//interrupts_set(e);
	thread_wakeup(cv->wq, false);
	assert(lock->held_by != thread_id);
	interrupts_set(e);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	int e = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);
	cv->num_waiting = 0;
	thread_wakeup(cv->wq, true);
	assert (cv->wq->head == NULL);
	interrupts_set(e);
	//submitted
}
