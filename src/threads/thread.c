#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include <round.h>
#include "threads/fixed-point.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* ------ NEW ----- 
    Processes that have been put to sleep.  */
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
bool priority_comp(const struct list_elem *a, 
                          const struct list_elem *b, void *unused);
bool wake_comp (const struct list_elem *a, 
                       const struct list_elem *b, void *unused);



static tid_t allocate_tid (void);
void prioritize(void);


int load_avg;
int ready_threads;

void 
thread_recalc_priority( struct thread *t, void *unused);

void reassess_priorities(struct thread *cur, void *unused);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{

  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  load_avg = 0;

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();



  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  // force highest priority thread to be running.


  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();

  // check for waiting threads to wake
  wake_ready_threads();

}

void prioritize(void)
{
 enum intr_level old_level = intr_disable ();
 if(!list_empty(&ready_list)){  
   if (thread_current()->priority < list_entry(list_front(&ready_list),
       struct thread, elem)-> priority){
          thread_yield();
     }
  }
  intr_set_level (old_level);
}

void reassess_priorities(struct thread *cur, void *unused)
{
  if (!cur->donatee)
    return;

  if (list_empty(&cur->donators))
  {
    cur->priority = cur->original_priority;
    cur->donatee = false;
  }
  else
  {
    
    list_sort(&cur->donators, priority_comp, NULL);
    cur->priority = list_entry(list_back(&cur->donators), struct thread, 
                               donate_elem)->priority;
  


  //  int k = list_entry(list_max(&cur->donators, priority_comp, NULL), struct thread, donate_elem)->priority;
    //printf("K: %d \n", k);
  //  int si = list_size(&cur->donators);
    //printf("size: %d \n", si);
   
  }
}

/* recalculates the priority of each thread */
void 
thread_reassess_priorities(void)
{
  enum intr_level old_level = intr_disable ();
  thread_foreach(reassess_priorities, NULL);
  intr_set_level (old_level);
}

/* donates the priority of the running thread to the donatee thread */
void 
thread_donate_priority(struct thread *d)
{
  struct thread *cur = thread_current();

  if (!d->donatee)
  {
    d->donatee = true;
  }
  enum intr_level old_level = intr_disable ();
  list_insert_ordered(&d->donators, &cur->donate_elem, priority_comp, NULL);
  reassess_priorities(d, NULL);
  intr_set_level (old_level);
}

/* increments the running thread's value of recent_cpu by one. Called every tick */
void 
increment_recent_cpu(void){

  enum intr_level old_level = intr_disable ();
  if (thread_current() != idle_thread){
    thread_current()->recent_cpu =  add_fp_int(thread_current()->recent_cpu, 1);
 //  thread_current()->recent_cpu =  thread_current ()->recent_cpu + 1;
  } 
  intr_set_level (old_level);
}


/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);



  prioritize();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered (&ready_list, &t->elem, priority_comp, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered(&ready_list, &cur->elem, priority_comp, NULL);

  cur->status = THREAD_READY;
  schedule();
  intr_set_level (old_level);
  
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if(!thread_mlfqs)
  {
    struct thread *cur = thread_current();
    if (!cur->donatee)
      cur->priority = new_priority;
    cur->original_priority = new_priority;
    prioritize();
  }
}



/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}


/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int new_nice) 
{

  enum intr_level old_level = intr_disable ();
  thread_current ()->nice = new_nice;
  thread_recalc_priority(thread_current(), NULL);
  intr_set_level (old_level);
  prioritize();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return fp_to_int_nearest (multiply_fp_int(load_avg, 100));
}

/* recalculates system load average */
void
update_load_avg (void)
{ 
  update_ready_threads();

  int temp = divide_fp_int( int_to_fp(59), 60);
  temp = multiply_fp_fp(temp, load_avg);
  int temp2 = divide_fp_int( int_to_fp(1), 60);
  temp2 = multiply_fp_int( temp2 , ready_threads );

  load_avg =  add_fp_fp( temp, temp2 );
}

/* updates the value of ready_threads, which represents the number of threads in
   the ready list at time of update */ 
void
update_ready_threads (void) 
{  
  
  int count = list_size(&ready_list);
  if (thread_current () != idle_thread){
    count = count + 1;
  }
  ready_threads = count;

}

/* Recalculates the value of recent_cpu for a given thread */
void
update_recent_cpu (struct thread *t, void *unused)
{
  int temp  = multiply_fp_int( load_avg, 2);
  temp = divide_fp_fp( temp, add_fp_int( temp, 1) );
  temp = multiply_fp_fp( temp, t->recent_cpu ); 
  temp = add_fp_int( temp, t->nice);
  t->recent_cpu = temp;

}

/* Recalculates the priority of each thread */ 
void
recalc_priorities (void)
{
  enum intr_level old_level = intr_disable(); 
  thread_foreach(thread_recalc_priority, NULL);
  intr_set_level(old_level);
}

/* Performs the calculations needed to be done on every second,
   which include updating the load average and recent_cpu values */ 
void
recalc_on_second(void)
{
  enum intr_level old_level = intr_disable(); 
  update_load_avg();
  thread_foreach(update_recent_cpu, NULL);
  intr_set_level(old_level);
}

/* Recalculates the priority of a given thread */ 
void 
thread_recalc_priority( struct thread *t, void *unused)
{

  if (t != idle_thread){
    int temp = int_to_fp(PRI_MAX);
    temp = subtract_fp_fp( temp, divide_fp_int( t->recent_cpu, 4) );
    temp = subtract_fp_int( temp, (t->nice *2) );   
    temp = fp_to_int_down(temp);
    if (temp > PRI_MAX){
      temp = PRI_MAX;
    }
    else if (temp < PRI_MIN){
      temp = PRI_MIN;
    }

    t->priority = temp;
 }
}



/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return fp_to_int_nearest(multiply_fp_int( thread_current ()->recent_cpu, 100));
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  // added to allow priority donation
  t->original_priority = priority;

  list_init(&t->donators);
  t->donatee = false; 

  t->magic = THREAD_MAGIC;



  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}



/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
  
}


/* Returns whether or not item a is greater in priority than item b. */
bool 
priority_comp(const struct list_elem *a, const struct list_elem *b, 
                          void *unused)
{
    struct thread *thread_a = list_entry(a, struct thread, elem);
    struct thread *thread_b = list_entry(b, struct thread, elem);

    return thread_a->priority > thread_b->priority;
}

/* returns whether or not thread a's priority less than thread b's. */
bool 
wake_comp (const struct list_elem *a, 
                       const struct list_elem *b, void *unused)
{
  struct thread *thread_a = list_entry(a, struct thread, sleep_elem);
  struct thread *thread_b = list_entry(b, struct thread, sleep_elem);

  int comp = thread_a->wakeup - thread_b->wakeup;
  return comp < 0 || (comp == 0 && thread_a->priority >= thread_b->priority);
}

/* check the first element in the wait list to see if it needs to be put on 
   the ready list. If it does, then put it on the ready list, and check the
   next element in the list, otherwise return immediately. */
void 
wake_ready_threads (void)
{
  /* if the list is empty, then we don't want to check anything and we just 
     return. */
  if (list_empty(&sleep_list))
    return;

  struct thread *thread_to_check = list_entry(list_begin(&sleep_list), 
				              struct thread, sleep_elem);

  if (thread_to_check->wakeup > timer_ticks ())
    return;
  else
  {
    //printf("(wake) %s Wake up ticks: %d\n",thread_to_check->name,
     //thread_to_check->wakeup);
    //printf("(wake) sleep list size: %d\n", list_size(&sleep_list));
    //printf("(wake) ready list size: %d\n", list_size(&ready_list));
    /* turn interrupts off, remove the thread from the wait list, and put it
       on the ready list. Once this is done, turn interrupts back on and 
       unblock the thread using thread_unblock. */
    enum intr_level old_level = intr_disable ();
    // pull the list element from the sleep list.
    thread_to_check = list_entry(list_pop_front(&sleep_list), 
                                 struct thread, sleep_elem);
    intr_set_level (old_level);

    //printf("(wake) sleep list size: %d\n", list_size(&sleep_list));
    //printf("(wake) ready list size: %d\n", list_size(&ready_list));

    thread_unblock(thread_to_check);
    //printf("(wake) passed unblock.\n");
  }

  wake_ready_threads();
}






/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* puts the currently active thread to sleep. The thread is put on the wait
   list, its position the list is determined by its sleep time. The thread is
   removed from being the running thread by the scheduler. */
void 
thread_sleep (int64_t wakeup_ticks)
{
  if (wakeup_ticks < 0)
    wakeup_ticks = 0;

  struct thread *cur = running_thread ();
  cur->wakeup = wakeup_ticks;
  //printf("(sleep) %s Wake up ticks: %d\n",cur->name, cur->wakeup);
  enum intr_level old_level = intr_disable ();
  list_insert_ordered(&sleep_list, &cur->sleep_elem, wake_comp, NULL);
  thread_block();
  intr_set_level (old_level);
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;
  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
