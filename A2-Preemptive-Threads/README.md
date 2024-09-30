# Preemptive User-Level Thread Package

The previous user-level thread package utilizes thread_yield causing control to be passed from one thread to the next. This assignment implements preemptive threading and has the four following procedures: 
* Implements preemptive threading, in which simulated "timer interrupts" cause the system to switch from one thread to another.
* Implements the thread_sleep and `thread_wakeup` scheduling functions. These functions enable further implementation of blocking, mutual exclusion and synchronization.
* Use of `thread_sleep` and `thread_wakeup` functions to implement `thread_wait`, which will block a thread until the target thread has finished executing.
* Implements blocking locks for mutual exclusion, and condition variables for synchronization.

<br /> `interrupt.[ch]` - Code for working with a timer signal as an interrupt.
<br /> <br /> `common.[ch]` - Functions used by most of the tests, including a function to busy-wait for a set amount of time, spin(), and a handler for certain common fatal signals (`SIGSEGV`, `SIGABRT`) that attempts to give you more information about the location where the failure occurred, especially for segmentation faults. Your mileage may vary. 
<br /> <br /> `malloc369.[ch]` and `khash.h` - a replacement for malloc.cpp that lets us do the memory allocation tracking in C, avoiding issues with mixing C and C++ code. The `free369()` function defined in `malloc369.c` includes a useful feature for detecting use-after-free bugs: it writes the value 0xee to every byte of the chunk of memory being freed. Attempts to read and use this freed memory as pointers, or as indexes into arrays will quickly lead to crashes, rather than running with corrupted memory long past the original source of the error. Please refer to the comments in `malloc369.c`. 

## Timer Signals

The code in the files `interrupt.h` and `interrupt.c` explained below:


`void register_interrupt_handler(bool verbose)`: This function installs a timer signal handler in the calling program using the sigaction system call. When a timer signal fires, the function `interrupt_handler` in the `interrupt.c` file is invoked. With the verbose flag, a message is printed when the handler function runs.

`bool interrupts_set(bool enable)`: This function enables timer signals when enable is 1, and disables/blocks them when `enable = 0`. We call the current enabled or disabled state of the signal the signal state. This function also returns whether the signals were previously enabled or not (i.e., the previous signal state). Notice that the operating system ensures that these two operations (reading previous state, and updating it) are performed atomically when the `sigprocmask` system call is issued. This function is used to disable signals when running any code that is a critical section (i.e., code that accesses data that is shared by multiple threads).

Why does this function return the previous signal state? The reason is that it allows "nesting" calls to this function. The typical usage of this function is as follows:
``` c
fn() {
    /* disable signals, store the previous signal state in "enabled" */
    int enabled = interrupts_set(false);
    /* critical section */
    interrupts_set(enabled);
}
```
\
The first call to `interrupts_set` disables signals. The second call restores the signal state to its previous state, i.e., the signal state before the first call to `interrupts_set`, rather than unconditionally enabling signals. This is useful because the caller of the function fn may be expecting signals to remain disabled after the function `fn` finishes executing. For example:
``` c
fn_caller() {
    int enabled = interrupts_set(false);
    /* begin critical section */
    fn();
    /* code expects signals are still disabled */
    ...
    /* end critical section */
    interrupts_set(enabled);
}
```
\
Notice how signal disabling and enabling are performed in "stack" order, so that the signal state remains disabled after `fn` returns.
\
The functions `interrupts_on` and `interrupts_off` are simple wrappers for the `interrupt_set` function.

`bool interrupts_enabled()`:
This function returns whether signals are enabled or disabled currently. You can use this function to check (i.e., assert) whether your assumptions about the signal state are correct.

`void interrupts_quiet()`:
This function turns off printing signal handler messages.

`void interrupts_loud()`:
This function turns on printing signal handler messages.

## Preemptive Threading

Signals can be sent to the process at any time, even when a thread is in the middle of a `thread_yield`, `thread_create`, or `thread_exit` call. It is a very bad idea to allow multiple threads to access shared variables (such as the ready queue) at the same time. You should therefore ensure mutual exclusion i.e., only one thread can be in a critical section (accessing the shared variables) in your thread library at a time.

We implement preemptive threading by adding the necessary initialization, signal disabling and signal enabling code in the thread library in `thread.c`, and enforce mutual exclusion by disabling signals when we enter procedures that access shared data structures and then we restore the signal state when we leave. This way we carefully maintain invariants by deciding when signals are enabled and disabled in our thread functions whilst using the `interrupts_enabled` function to check our assumptions.

As a result of thread context switches, the thread that disables signals may not be the one enables them. In particular, recall that `setcontext` restores the register state saved by `getcontext`. The signal state is saved when `getcontext` is called and restored by `setcontext`. As a result, if if code is running with a specific signal state (i.e., disabled or enabled) when `setcontext` is called, we make sure that `getcontext` is called with the same signal state, with use of `assert (!interrupts_enabled())` before calls to getcontext and setcontext. 


## Sleep and Wakeup

Now that we have implemented preemptive threading, we extend the threading library to implement the `thread_sleep` and `thread_wakeup` functions. These functions will us to implement mutual exclusion and synchronization primitives. In real operating systems, these functions would also be used to suspend and wake up a thread that performs IO with slow devices, such as disks and networks. The `thread_sleep` primitive blocks or suspends a thread when it is waiting on an event, such as a mutex lock becoming available or the arrival of a network packet. The thread_wakeup primitive awakens one or more threads that are waiting for the corresponding event.

#### The thread_sleep and thread_wakeup functions implemented are summarized below:

`Tid thread_sleep(struct wait_queue *queue)`:
This function suspends the caller and then runs some other thread. The calling thread is put in a wait queue passed as a parameter to the function. The `wait_queue` data structure is similar to the run queue, but there can be many wait queues in the system, one per type of event or condition. Upon success, this function returns the identifier of the thread that took control as a result of the function call. The calling thread does not see this result until it runs later. Upon failure, the calling thread continues running, and returns one of these constants:
* `THREAD_INVALID`: alerts the caller that the queue is invalid, e.g., it is `NULL`.
* `THREAD_NONE`: alerts the caller that there are no more threads, other than the caller, that are ready to run. Note that if the thread were to sleep in this case, then your program would hang because there would be no runnable thread.

This function suspends (blocks) the current thread rather than spinning (running) in a tight loop, thus blocking the thread means yielding to another thread. This would defeat the purpose of invoking `thread_sleep` because the thread would still be using the CPU. This function sets up a flag "sleeping" in the thread struct, and when thread_sleep is called it sets `thread->sleeping = true` and calls `thread_yield(THREAD_ANY)`. In thread_yield it checks if thread is sleeping; if `thread->sleeping == false` this means yield was called on a thread and that thread is then placed it the ready queue. If `thread->sleeping == true`, it is placed on the wait queue and thus suspended.

<br/>`int thread_wakeup(struct wait_queue *queue, int all)`:
This function wakes up one or more threads that are suspended in the wait queue. The awoken threads are put in the ready queue. The calling thread continues to execute and receives the result of the call. When "all" is 0 (false), then one thread is woken up. In this case, it wakes up threads in FIFO order, i.e., first thread to sleep must be woken up first. When "all" is 1 (true), all suspended threads are woken up. The function returns the number of threads that were woken up. It returns zero if the queue is invalid, or there were no suspended threads in the wait queue.


<br/>The `thread.h` file provides the interface for the `wait_queue` data structure, where each thread can be in only one queue at a time (a run queue or any one wait queue).

<br/>In A1, `thread_kill(tid)` ensured that the target thread (whose identifier is `tid`) did not run any further, and this thread would eventually exit when it ran the next time. In the case another thread invokes `thread_kill` on a sleeping thread, then this thread is immediately removed from the associated wait queue and woeken it up, placed in the ready queue (runnable). Then, the thread exit when it runs the next time.

## Waiting for Threads to Exit

Now that we have implemented the `thread_sleep` and `thread_wakeup` functions for suspending and waking up threads, we can use them to implement blocking synchronization primitives in the threads library. We should start by implementing the `thread_wait` function, which blocks or suspends a thread until a target thread terminates (or exits). Once the target thread exits, the thread that invokes thread_wait should continue operation. As an example, this synchronization mechanism can be used to ensure that a program (using a master thread) exits only after all its worker threads have completed their operations.

#### The thread_wait function is summarized below:

`int thread_wait(Tid tid, int *exit_code)`:
This function suspends the calling thread until the thread whose identifier is tid terminates. A thread terminates when it invokes `thread_exit`. Upon success, this function returns the identifier of the thread that exited. If `exit_code is not NULL`, the exit status of the thread that exited (i.e., the value it passed to thread_exit) will be copied into the location pointed to by exit_code. Upon failure, the calling thread continues running, and returns the  constant `THREAD_INVALID`. Failure can occur for the following reasons:
* Identifier tid is not a feasible thread id (e.g., any negative value of tid or tid larger than the maximum possible tid)
* No thread with the identifier tid could be found.
* The identifier tid refers to the calling thread.
* Another thread is already waiting for the identifier tid.

>We associate a wait_queue with each thread. When thread X invokes thread_wait for thread Y, it should sleep on the wait_queue of the target thread Y. When the thread Y invokes exit, and is about to be destroyed, it should wake up the threads in its wait_queue. Only exactly one caller of thread_wait(tid,...) can succeed for a target thread tid. All subsequent callers should fail immediately with the return value THREAD_INVALID.

<br/> If a thread with `tid` exits voluntarily (i.e., calls `thread_exit` without being killed by another thread) before it is waited for, a subsequent call to `thread_wait(tid, &tid_status)` is still able to retrieve the exit status that thread `tid` passed to `thread_exit`. 

<br/> If a thread with id tid is killed by another thread via `thread_kill`, set its exit code to `-SIGKILL`. In this case there are two possibilities:
* If `tid` has already been waited on at the time it is killed, the waiting thread must be woken up. If the waiting thread provided a non-NULL pointer for the exit code, then the killed thread's exit code `-SIGKILL` must be stored into the location it points to. 
* If tid has not yet been waited on before it is killed, a subsequent call to `thread_wait(tid, ...)` returns `THREAD_INVALID`. That is, a thread cannot wait for a killed thread.

No need to detect if the thread id is recycled between the kill and the wait calls. If this happens, the `thread_wait` succeeds. Thus we delay recycling thread ids as long as possible to avoid having a thread accidentally wait on the wrong target thread.

>Threads are all peers. A thread can wait for the thread that created it, for the initial thread, or for any other thread in the process. One issue this creates for implementing `thread_wait` is that a deadlock may occur. For example, if Thread A waits on Thread B, and then Thread B waits on Thread A, both threads will deadlock. This condition is not handled in the assignment.

## Mutex Locks
The final task implements mutual exclusion and synchronization primitives in your threads library. Recall that these primitives form the basis for managing concurrency, which is a core concern for operating systems. 
For mutual exclusion, we implement blocking locks, and for synchronization, we implement condition variables.



### The API for the lock functions are described below:

`struct lock *lock_create()`: Creates a blocking lock. Initially, the lock is available. Code should associate a wait queue with the lock so that threads that need to acquire the lock can wait in this queue.

`void lock_destroy(struct lock *lock)`:Destroys the lock. Checks that the lock is available when it is being destroyed.

`void lock_acquire(struct lock *lock)`: Acquires the lock. Threads are suspended until they can acquire the lock, after which this function returns.

`void lock_release(struct lock *lock)`: Releases the lock. Checks that the lock has been acquired by the calling thread, before it is released. Wakes up all threads that are waiting to acquire the lock.< br / >



### The API for the condition variable functions are described below:

`struct cv *cv_create()`: Creates a condition variable. Associates a wait queue with the condition variable so that threads can wait in this queue.

`void cv_destroy(struct cv *cv)`: Destroys the condition variable. Checks that no threads are waiting on the condition variable.

`void cv_wait(struct cv *cv, struct lock *lock)`: Suspends the calling thread on the condition variable `cv`. Checks that the calling thread has acquired the lock when this call is made. Releases the `lock` before waiting, and reacquires it before returning.

`void cv_signal(struct cv *cv, struct lock *lock)`: Wakes up one thread that is waiting on the condition variable `cv`. Checks that the calling thread has acquired the lock when this call is made.

`void cv_broadcast(struct cv *cv, struct lock *lock)`: Wakes up all threads that are waiting on the condition variable `cv`. Checks that the calling thread has acquired the lock when this call is made.

\
The `lock_acquire`, `lock_release` functions, and the `cv_wait`, `cv_signal` and `cv_broadcast` functions access shared data structures, thus **Mutual Exclusion** is enforced.


