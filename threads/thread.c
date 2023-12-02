#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/fpMath.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

#define NESTED_DEPTH 8

#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

// fp : fixed point
int fp_load_avg;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

// 자는 리스트
static struct list sleep_list;

/* Idle thread. */
// idle 스레드는 맨 처음 시작된 스레드 하나만 말한다
static struct thread *idle_thread;

static int64_t next_tick_to_awake = INT64_MAX;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4		  /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

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
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the globla thread context */
	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&sleep_list);
	list_init(&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	fp_load_avg = LOAD_AVG_DEFAULT;

	/* Start preemptive thread scheduling. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */

	// 타임 슬라이스를 넘었다면
	// 시간 지났다는 인터럽트를 발생시킨다
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
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
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);

	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock(t);

	// 내림차순 리스트의 가장 첫 요소와 현재 스레드랑 비교해줌
	compare_Curr_ReadyList();

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);

	thread_current()->status = THREAD_BLOCKED;

	schedule();
}

// 스레드 재우기
void thread_sleep(int64_t _Times)
{
	/*
		인터럽트 비활성의 이유
		1. 원자성(Atomicity) 보장
		 - sleep list에 추가하고 thread_block을 호출하는 동안
		   다른 인터럽트로 인하여 해당 과정의 '임계 영역'이 보장되지 않을 수 있음
		2. 스케쥴러의 일관성을 유지
		 - 인터럽트를 비활성화 하여, 스케쥴러가 스레드를 재우는 과정을 명시적으로 관리
			(예상치 못한 문제를 예방)

		인터럽트 비활성은 일종의
		'동기화 매커니즘'

		이는
		임계영역을 보호하여, 데이터나 자원을 안전하게 접근하게 한다
		또한, 작업이 중간에 '중단'되지 않게하여 '원자성'을 보장한다
		=> 이를 통해 데이터의 일관성을 유지할 수 있음

		(다만 인터럽트를 막는것은, 시스템 전체의 인터럽트를 막는 것이므로
		시스템의 응답성을 저하시킬 수 있음)
		(필요할 때 쓰고, 바로바로 풀어야 함)
	*/
	enum intr_level old_level = intr_disable();

	thread_current()->wakeup_tick = _Times;
	list_push_back(&sleep_list, &thread_current()->elem);
	update_next_tick_to_awake();
	thread_block();

	intr_set_level(old_level);
}

// sleep que에서 깨울 thread를 찾아 wake up 해준다
void thread_awake(int64_t _Times)
{
	// 자는놈 없음
	if (list_empty(&sleep_list))
	{
		return;
	}

	// 현재 틱 기준으로 깨울 놈 없음
	if (_Times < next_tick_to_awake)
	{
		return;
	}

	struct list_elem *tempElem = list_begin(&sleep_list);
	if (tempElem == NULL)
		return;

	struct list_elem *sleep_Tail = list_tail(&sleep_list);

	while (tempElem != sleep_Tail &&
		   tempElem != NULL)
	{
		struct thread *t = list_entry(tempElem, struct thread, elem);
		struct list_elem *n = tempElem->next;
		if (_Times >= t->wakeup_tick)
		{
			list_remove(tempElem);
			thread_unblock(t);
		}
		tempElem = n;
	}

	// 여기서 'cpu' 비교를 하지 않는 이유는
	// 이 함수가 '하드웨어 인터럽트'인 'Timer interrupt'에 의하여 호출되고 있기에
	// yield 내부에서 assert가 걸리기 때문

	update_next_tick_to_awake();
}

// sleep list 내부에서 thread들이 가진 값 중 최솟값을 저장
void update_next_tick_to_awake()
{
	if (list_empty(&sleep_list))
	{
		next_tick_to_awake = INT64_MAX;
		return;
	}

	struct list_elem *tempElem = list_begin(&sleep_list);
	if (tempElem == NULL)
		return;

	struct list_elem *sleep_Tail = list_tail(&sleep_list);

	while (tempElem != sleep_Tail &&
		   tempElem != NULL)
	{
		struct thread *t = list_entry(tempElem, struct thread, elem);

		if (next_tick_to_awake > t->wakeup_tick)
		{
			next_tick_to_awake = t->wakeup_tick;
		}

		tempElem = list_next(tempElem);
	}
}

// 최솟 tick값을 반환
int64_t get_next_tick_to_awake(void)
{
	return next_tick_to_awake;
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t));

	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);

	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);

	t->status = THREAD_READY;
	intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	// 문맥교환해야 하기에
	// 외부 인터럽트가 들어오면 안되는 상태이다
	ASSERT(!intr_context());

	old_level = intr_disable();

	// 현재 스레드가 idle 스레드가 아니다
	if (curr != idle_thread)
		// 준비 리스트에 넣는다
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL);

	// 현재 스레드를 준비 상태로 만든다
	do_schedule(THREAD_READY);
	intr_set_level(old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{
	if(thread_mlfqs == true)
		return;

	thread_current()->initPriority = new_priority;

	// 리스트 없을 때도 강제로 원래 initpriority로 바꿔주는 부분 때문에
	// listwait가 비어있을 땐, 바꾸지 못하게 하여야 함
	// 또한 나중에 lock_release 할때, 원래 락이 필요한 녀석과 같은 priority를 가졌기에
	// 그 때는 initpriority로 비교해야 한다
	// condvar 에서 문제되는 부분은 원래는 바로 0이 아닌 32이기에
	// 발생하는 문제임
	refresh_priority();

	donate_priority();

	compare_Curr_ReadyList();
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
	struct thread *curr = thread_current();
	int pri = curr->priority;
	return pri;
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice)
{
	// nice 값 구하고,
	// priority 재계산
	struct thread* curr = thread_current();
	
	// nice 값 제한
	const int Max_Nice = 20;
	const int Min_Nice = -20;

	if(nice < Min_Nice)
		nice = Min_Nice;
	else if(nice > Max_Nice)
		nice = Max_Nice;

	curr->nice = nice;
	calc_priority(curr);

	compare_Curr_ReadyList();
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
	struct thread* curr = thread_current();
	int nice = curr->nice;

	return nice;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
	int real_load_avg = mult_mixed(fp_load_avg,100);

	int integ_load_avg = fp_to_int_round(real_load_avg);
	return integ_load_avg;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
	struct thread* curr = thread_current();

	int real_curr_recent_cpu = mult_mixed(curr->fp_recent_cpu,100);

	int integ_curr_recent_cpu = fp_to_int_round(real_curr_recent_cpu);
	return integ_curr_recent_cpu;
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
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* Let someone else run. */
		intr_disable();
		thread_block();

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
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	t->initPriority = priority;
	list_init(&t->waitList);
	t->waitingLock = NULL;

	t->nice = NICE_DEFAULT;
	t->fp_recent_cpu = RECENT_CPU_DEFAULT;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		// 특정 지점 개체의 변수인 elem을 list에서 가지고 있다가
		// 그 offset만큼 뺀 위치의 포인터를 반환하여
		// thread* 로 캐스팅하여 반환
		// list elem 이 포함된 구조체 instance를 찾아 반환
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"
		"movq 0(%%rsp),%%r15\n"
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n"
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq"
		: : "g"((uint64_t)tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		: : "g"(tf_cur), "g"(tf) : "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */

// 현재 스레드를 status로 바꿔주고
// schedule을 호출해줌
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current()->status = status;
	schedule();
}

// 다음 실행할 스레드를 스케쥴한다
static void
schedule(void)
{
	// 현재 실행 중인 스레드
	struct thread *curr = running_thread();

	// 다음에 실행할 스레드
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING);
	ASSERT(is_thread(next));
	/* Mark us as running. */
	// 다음거 실행 상태로 만들기
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	// 새로 실행하였다면 thread tick을 초기화한다
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next);
#endif

	if (curr != next)
	{
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *aThread = list_entry(a, struct thread, elem);
	struct thread *bThread = list_entry(b, struct thread, elem);

	if (aThread->priority > bThread->priority)
		return true;

	return false;
}

void compare_Curr_ReadyList()
{
	if (list_empty(&ready_list))
		return;
	// ready_list는 내림차순으로 이기에 (집어넣을때, sorted로 집어넣으므로)
	// 그러므로 첫번째가 제일 큰 값이다
	struct thread *bestPT = list_entry(list_front(&ready_list), struct thread, elem);
	if (bestPT == NULL)
		return;

	if (bestPT->priority > thread_get_priority())
		thread_yield();
}

void donate_priority(void)
{
	// priority donation을 수행
	// 현재 스레드가 기다리고 있는 lock과 연결된 모든 쓰레드들을 순회하며
	// 현재 쓰레드의 우선순위를 lock을 보유하고 있는 스레드에게 줌
	// ex : 현재 스레드가 기다리고 있는 락의 holder
	// -> holder가 기다리고 있는 lock의 holder... (이건 8번 제한)

	if (thread_current()->waitingLock == NULL)
		return;

	struct thread *ownLockThread = thread_current()->waitingLock->holder;
	int current_Priority = thread_get_priority();

	int i = 0;

	while (i < NESTED_DEPTH &&
		   ownLockThread != NULL)
	{
		if (ownLockThread->priority < current_Priority)
		{
			ownLockThread->priority = current_Priority;
		}
		else
		{
			break;
		}

		if (ownLockThread->waitingLock == NULL)
			break;
		ownLockThread = ownLockThread->waitingLock->holder;
		i++;
	}
}

void remove_with_lock(struct lock *_lock)
{
	// 현재 스레드의 waiters list를 확인하여 해지할 lock을 보유하고 있는 엔트리를 삭제
	// 현재 스레드의 waiter list에 존재하는 것은 각각
	// 락을 존재하는 녀석들이다
	if (_lock == NULL)
		return;

	if (list_empty(&thread_current()->waitList))
		return;

	struct thread *curr = thread_current();

	struct list_elem *tempElem = list_front(&curr->waitList);
	struct list_elem *tail_Elem = list_end(&curr->waitList);

	struct thread *t;
	while (tempElem != tail_Elem)
	{
		// sema 기준으로 curr->waitlist가 비어있지 않고 이상한 녀석이 들어가 있음
		// 음... 혹시 main이 들어가있는건 아닌지...??
		// main은 waitlist에 들어가지 않을텐데..??

		// 음.. low 기준으로 waitlist에 h가 들어있을텐데
		// h가 lock을 해제하면서
		// low에 있는 자신의 waitlist를 해제하지 않아 발생한 문제??

		t = list_entry(tempElem, struct thread, waitElem);
		if (t->waitingLock == _lock)
		{
			tempElem = list_remove(tempElem);
		}
		else
		{
			tempElem = tempElem->next;
		}
	}
}

void refresh_priority(void)
{
	// 현재 쓰레드의 우선순위를 기부 받기 전의 우선순위로 변경
	// 현재 쓰레드의 waiters 리스트에서 가장 높은 우선순위를 현재 쓰레드의 우선순위와 비교 후 우선순위 설정
	thread_current()->priority = thread_current()->initPriority;

	// 음... 낮게 설정하는 데 강제로 바꾸는것이 뭔가 좀 아닌것 같은데
	if (list_empty(&thread_current()->waitList))
		return;

	int max_priority = thread_get_priority();

	struct thread *t = list_entry(list_front(&thread_current()->waitList), struct thread, waitElem);
	if (t != NULL)
	{
		if (t->priority > max_priority)
			max_priority = t->priority;
	}

	thread_current()->priority = max_priority;
}

/*
	recent_cpu 와 load_avg 가 real number(실수) 라는 것을 잊지 말기

	이들은 fixed_point 의 연산을 이용하여
	계산해야 한다
*/

void calc_priority(struct thread *t)
{
	if(t == idle_thread)
		return;

	// priority = PRI_MAX - (recent_cpu / 4) - nice * 2
	// 다만 recent_cpu 가 실수이기에
	// fixed_point 내부의 함수를 이용하여 구한 후,
	// 정상적인 int 값으로 바꾸어줌 (일단 반올림)
	int real_recent_div_4 = div_mixed(t->fp_recent_cpu,4);

	t->priority = PRI_MAX -  fp_to_int_round(real_recent_div_4) - t->nice * 2;
}

void calc_recent_cpu(struct thread *t)
{
	if(t == idle_thread)
		return;
	
	// recent_cpu = decay * recent_cpu + nice
	// decay : 감쇠값
	int real_mult2_Fp = mult_mixed(fp_load_avg,2);
	int real_decay = div_fp(real_mult2_Fp,add_mixed(real_mult2_Fp,1));
	t->fp_recent_cpu = add_mixed(mult_fp(real_decay,t->fp_recent_cpu),t->nice);
}

void calc_load_avg(void)
{
	// load_average = (59/60) * load_average + (1/60) * ready_threads
	// load_average : 최근 1분 동안 수행 가능한 프로세스의 평균 
	// ready_threads : ready_list의 스레드들과 실행 중인 스레드의 개수
	// (idle 제외)
	// load_avg 는 실수이다

	// 이러한 공식을 지수 가중 이동 평균 (Exponetially Weighted Moving Average)라 하며,
	// 과거의 데이터의 가중 평균 과 현재 데이터 포인트를 결합하여 계산한다
	// (59/60) * load_avg       +  (1/60) * ready_threads
	// EMA(t) = (1 - a) * EMA(t-1) + a * x(t)
	// x(t) : 현재 시점의 데이터 포인트
	// a : 스무딩 파라미터 (데이터에 부여되는 가중치를 제어하는 값이며 0~1 사이의 값)
	// a 값이 작은 경우, 데이터 이동에 대한 평균이 더 완화되며,
	// a 값이 큰 경우, 현재 시점의 데이터를 더욱 반영하게 된다
	// 위 예시에서 a는 1/60 을 말한다

	int fp_oneMSParam = div_fp(59,60); // 1 - smooth parameter 이므로 one minus smooth param을 의미
	int fp_smoothParam = div_fp(1,60); // smooth parameter를 뜻함

	// 현재 실행중인 스레드
	int ready_thread = 1;
	if(thread_current() == idle_thread) // 다만 그게 idle인 경우 다시 0으로
		ready_thread--;

	ready_thread += list_size(&ready_list);

	fp_load_avg = add_fp(mult_fp(fp_oneMSParam,fp_load_avg),mult_mixed(fp_smoothParam,ready_thread));

	if(fp_load_avg < 0)
		fp_load_avg = 0;
}

void recent_cpu_incre(void)
{
	// tick 마다 호출되어 recent_cpu를 1 증가 시킨다
	struct thread* curr = thread_current();

	if(curr == idle_thread)
		return;
	
	curr->fp_recent_cpu = add_mixed(curr->fp_recent_cpu,1);
}

void atrp_recalc(void)
{
	// 모든 thread의 recent_cpu와 priority를 재 계산한다
	struct thread* tempT = thread_current();
	struct list_elem* tempElem = list_head(&ready_list);
	struct list_elem* tail_Elem = list_tail(&ready_list);

	// 현재 cpu, ready_list, sleep_list 를 순회하며 재계산한다

	do
	{
		calc_recent_cpu(tempT);
		calc_priority(tempT);
		tempElem = tempElem->next;
		tempT = list_entry(tempElem, struct thread, elem);
	} while (tempElem != tail_Elem);

	tempElem = list_head(&sleep_list);
	tail_Elem = list_tail(&sleep_list);

	while (tempElem != tail_Elem)
	{
		tempT = list_entry(tempElem, struct thread, elem);
		calc_recent_cpu(tempT);
		calc_priority(tempT);
		tempElem = tempElem->next;
	}

}