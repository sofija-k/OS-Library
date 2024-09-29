# Cooperative User-Level Thread Package
Threads and processes are key abstractions for enabling concurrency in operating systems. This code does not make calls to any existing thread libraries (e.g., Linux pthreads), or borrow code from these libraries. In this file we construct user threads by implementing a set of functions that the program will call directly to provide the illusion of concurrency. In contrast, modern operating systems provide kernel threads, and a user program invokes the corresponding kernel thread functions via system calls. Both types of threads use the same core techniques for providing the concurrency abstraction.

Here thread_main_i is a programmer-supplied "main" function that the ith thread starts executing when it is first scheduled to run after its creation. (Note that different threads may have the same "main" function.) The thread can perform useful work by calling any other functions in the program, or voluntarily yielding to other threads. A thread exits either explicitly or implicitly. It exits explicitly when it calls the thread_exit function in the thread library. It exits implicitly when its thread_main function returns. Additionally, to add more control to the program, a thread may call thread_kill to force other threads to exit as well.


## Cooperative Threads API

This threads library maintains a "thread control block” (otherwise known as struct thread) for each thread that is running in the system. This is similar to the process control block that an operating system implements to support process management. As such, each thread has a stack of at least THREAD_MIN_STACK bytes. Instead of using a global structure for static initialization, we dynamically allocate a stack, using malloc() whenever a new thread is created/launched and delete, using free() whenever a thread is destroyed. In addition, this library maintains a queue of the threads that are ready to run (Tid ready_queue), so that when the current thread yields, the next thread in the ready queue can be run. The library allows running a fixed number of threads (THREAD_MAX_THREADS threads), and allocates these structures statically as a global array struct threads* created_threads. For every created thread we update our global counter int num_created_threads.

This library uses getcontext and setcontext to save and restore thread context state (see Thread Context below), but it does not use makecontext or swapcontext or any other existing C library code to manipulate a thread's context, the code is written from scratch to perform these operations

### void thread_init(void):
This function to performs any initialization that is needed by the threading system. It hand-crafts the first user thread in the system by configuring thread state data structures so that the (kernel) thread that is running when the program begins (before any calls to thread_create) will appear as the first user thread in the system (with tid = 0). No stack allocation is necessary for this thread, because it will run on the (user) stack allocated for this kernel thread by the OS.

### Tid thread_id():
This function returns the thread identifier of the currently running thread. The return value lies between 0 and THREAD_MAX_THREADS-1 (inclusive). Supports the creation of a maximum of THREAD_MAX_THREADS concurrent threads by a program (including the initial main thread). Thus, the maximum value of the thread identifier should be THREAD_MAX_THREADS - 1 (since thread identifiers start from 0). Note that when a thread exits, its thread identifier can be reused by another thread created later. The thread_id() function runs in constant time (i.e., the time to find the TID of the current thread should not depend on the number of threads that have been, or could be, created). 

### Tid thread_yield(Tid to_tid):
This function suspends the caller and activates the thread given by the identifier to_tid, this allows our current running thread to yield the CPU to a specified thread to_tid. The caller is put on the ready_queue and can be run later in a similar fashion. A reasonable policy is to add the caller to the tail of the ready queue to ensure fairness. The value of to_tid may take the identifier of any available thread. It also can take any of the following constants:
* **THREAD_ANY**: tells the thread system to run any thread in the ready queue. Which runs the thread at the head of the ready queue, which ensures fairness. This policy is called FIFO (first-in, first-out), since the thread that first entered the ready queue (among the threads that are currently in the ready queue) is scheduled first. 
* **THREAD_SELF**: tells the thread system to continue the execution of the caller. This function is implemented as a no-op.

The thread_yield function returns the identifier of the thread that took control as a result of the function call. Note that the caller does not get to see the result until it gets its turn to run (later). The function may also fail and the caller continues execution immediately. To indicate the reason for failure, the call returns one of these constants:
* **THREAD_INVALID**: alerts the caller that the identifier to_tid does not correspond to a valid thread.
* **THREAD_NONE**: alerts the caller that there are no more threads, other than the caller, that are available to run, in response to a call with to_tid set to **THREAD_ANY**. 

### Tid thread_create(void (*fn)(void *), void *arg):
This function creates a thread whose starting point is the function fn. The second argument, arg, is a pointer that will be passed to the function fn when the thread starts executing. The created thread is put on a ready queue but does not start execution yet. The caller of the thread_create function continues to execute after thread_create returns. Upon success, thread_create returns a thread identifier of type Tid. If thread_create fails, it returns a value that indicates the reason for failure as follows:
* **THREAD_NOMORE**: alerts the caller that the thread package cannot create more threads. 
* **THREAD_NOMEMORY**: alerts the caller that the thread package could not allocate memory to create a stack of the desired size. 

### void thread_exit(int exit_code):
This function ensures that the current thread does not run after this call, i.e., this function should never return and thus we switch contexts and run another thread. If there are other threads in the system, one of them should be run. If there are no other threads (this is the last thread invoking thread_exit), then the program should exit with the supplied exit_code. A thread that is created later should be able to reuse this thread's identifier, but only after this thread has been destroyed. 

### Tid thread_kill(Tid victim):
This function kills another thread whose identifier is victim. The victim can be the identifier of any available thread. The killed thread should not run any further and the calling thread continues to execute. Upon success, this function returns the identifier of the thread that was killed. Upon failure, it returns the following:
* **THREAD_INVALID**: alerts the caller that the identifier victim does not correspond to a valid thread (a thread that doesn’t exist or isn’t created), or is the currently running thread. 


## Thread Context
Each thread has per-thread state that represents the working state of the thread -- the thread's program counter, local variables, stack, etc. A thread context is a subset of this state that must be saved/restored from the processor when switching threads. (To avoid copying the entire stack, the thread context includes a pointer to the stack, not the entire stack.) This library stores the thread context in a per-thread data structure (this structure is sometimes called the "thread control block") This state is stored in struct* thread under thread-> context.

When a thread yields the CPU in thread_yield, we save the current thread's context, which contains the processor register values at the time the thread yields the CPU. The library restores the saved context later when the thread gets its turn to run on the processor again. Additionally, the library creates a fresh context and a new stack when it creates a thread. Fortunately, the C runtime system allows an application to retrieve its current context and store it in a memory location, and to set its current context to a predetermined value from a memory location. This library makes use of these two existing C library calls: getcontext and setcontext.

Where getcontext saves the current context into a structure of type struct ucontext_t. By allocating a ucontext_t and passing its memory pointer to a call to getcontext, the current registers and other context will be stored to that memory. We call setcontext to copy that state from that memory to the processor, restoring the saved state. 
To suspend a currently running thread (to run another one), we use getcontext to save its state and later use setcontext to restore its state. Second, to create a new thread, we use getcontext to create a valid context, but leave the current thread running which will change a few registers in this valid context to initialize the new thread, and put this new thread into the ready queue. Eventually, at some point, the new thread will be chosen by the scheduler, and it will run when setcontext is called on this new thread's context.

When creating a thread, we don’t just make a copy of the current thread's context (using getcontext), we need to make a few changes to initialize the new thread:
* change the saved program counter register (%rip, see Contexts & Calling Conventions below) in the context to point to a stub function which should be the first function the newly created thread runs
* change the saved argument registers (%rid and %rsi, see Contexts & Calling Conventions below) in the context to hold the arguments fd and parg that are to be passed to the stub function.
* allocate a new per-thread stack using malloc()
* change the saved stack pointer register (%rsp) in the context to point to the top of the new stack (in x86-64, stacks grow down!)

In the real world, we would take advantage of an existing library function, makecontext, to make these changes to the copy of the current thread's context. The advantage of using this function is that it abstracts away the details of how a context is saved in memory, which simplifies things and helps portability. The disadvantage is that it abstracts away the details of how a context is saved in memory, which might leave us unclear about exactly what's going on. In the spirit of "there is no magic", for this assignment we do not use makecontext or swapcontext. Instead, we manipulate the fields in the saved ucontext_t directly. 

## The Stub Function
```c
/* thread starts by calling thread_stub. The arguments to thread_stub are the
 * thread_main() function, and one argument to the thread_main() function. 
 */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
	thread_main(arg); // call thread_main() function with arg
	thread_exit(0);
}
```
When we create a new thread, we want it to run the thread_main function that defines the work we want the thread to do. A thread exits implicitly when it returns from its thread_main function, much like the main program thread is destroyed by the OS when it returns from its main function in C, even when the main function doesn't invoke the exit system call. To implement a similar implicit thread exit, rather than having your thread begin by running the thread_main function directly, you should start the thread in a "stub" function that calls the thread_main function of the thread (much like main is actually called from the crt0 stub function in UNIX). In other words, thread_create should initialize a thread so that it starts in the thread_stub function shown below. When the thread runs for the first time, it will execute thread_stub, which will call thread_main. If the thread_main function returns, it will return to the stub function which will call thread_exit to terminate the thread. The argument thread_main, passed into the stub function, is a pointer to the thread_main function that describes the real work the thread should do. Notice that in C, a function's name refers to the address of its code in memory. 


### Contexts & Calling Conventions

The ucontext_t structure contains many data fields, but we deal with four of them when creating new threads: the stack pointer, the program counter, and two argument registers. While a procedure executes, it can allocate stack space by moving the stack pointer, down (stack grows downwards). However, it can find local variables, parameters, return addresses, and the old frame pointer, %rbp, by indexing relative to the frame pointer (%rbp) register because its value does not change during the lifetime of a function call. When a function needs to make a function call, it copies the arguments of the "callee" function (the function to be called) into the registers(%rdi, %rsi, %rdx, %rcx, %r8, %r9) in the x86-64 architecture. The %rdi register will contain the first argument, the %rsi register will contain the second argument, etc. 
