/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		// PSS - Semaphore를얻고waiters 리스트삽입시, 우선순위대로삽입되도록수정
		list_insert_ordered (&sema->waiters, &thread_current ()->elem, cmp_priority, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
		list_sort(&sema->waiters, cmp_priority, NULL); // Semaphore를얻고waiters 리스트삽입시, 우선순위대로삽입되도록수정
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	sema->value++;
	// priority preemption 기능추가.
	// waitlist 에서 나가면 readylist로 가기 때문에, preemtion을 readylist의 맨 앞에있는 친구와 현재 스레드를 비교하면 되니까
	// 이 기능이 이미 구현이 되어있는 test_max_priority()를 호출해서 preemtion을 맞춘다.
	//test_max_priority();
	thread_yield();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
   /* lock을 점유하고 있는 스레드와 요청하는 스레드의 우선순위를 비교하여 priority donation을 수행하도록 수정*/
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));
	struct thread *curr = thread_current();

	/* mlfqs스케줄러활성화시priority donation 관련코드비활성화*/
	if (lock->holder != NULL) { 
		curr->lock_im_waiting = lock;   // 내가 기다리고 있는 락에 등록
		// lock holder의 donors list에 현재 스레드 추가
		list_insert_ordered (&lock->holder->donor_list, &curr->donor_list_elem, cmp_donation_priority, NULL);
		if (thread_mlfqs == false)
			donate_priority (); 			// 기부! 
	}
	
	sema_down (&lock->semaphore);		// lock 점유!!
	curr->lock_im_waiting = NULL;		// 점유 했으니 풀기
	lock->holder = thread_current ();	// 이제 이 락은 현재 스레드의 것입니다
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	/* mlfqs스케줄러활성화시priority donation 관련코드비활성화*/
	if (thread_mlfqs == false) {
		remove_donor(lock);				 // 기부자 목록에서 반환될 락을 요청했던 스레드 제거
		refresh_priority();				 // 현재 스레드가 donee이고 락을 반환할 때, 기부 이전 priority로 복귀
	}

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/**
 *  LOCK을 원자적으로 해제하고 다른 코드에서 COND가 신호를 보낼 때까지 대기합니다. 
 *  COND가 신호를 받은 후에는 반환하기 전에 LOCK을 다시 획득합니다
 *  특정 조건 변수는 하나의 락과만 연관되어 있지만 하나의 락은 여러 개의 조건 변수와 연관될 수 있습니다. 즉, 락에서 조건 변수로의 일대다 매핑이 있습니다.
 *  cond_wait함수는 호출되기 전에 스레드에 lock이 걸리고,
 *  sema_down전에 lock이 해제되었다가 깨어난 뒤에 다시 lock을 걸고 리턴한다.
 * 
 *  호출되면 프로세스는 block state가 되고
 *  condition variable로부터 오는 시그널을 기다린다.*/
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	// PSS - condition variable의 waiters list에 우선순위 순서로 삽입되도록 수정
	list_insert_ordered (&cond->waiters, &waiter.elem, cmp_sema_elem_priority, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* condition variable에서 기다리는 가장 높은 우선순위의 스레드에 signal을 보냄 */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
	{
		list_sort(&cond->waiters, cmp_sema_elem_priority, NULL); // PSS - condition variable의waiterslist를우선순위로재정렬
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

bool 
cmp_sema_priority (const struct list_elem *a, const struct list_elem *b, void *aux) {
    return list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority;
}

/** 첫번째 인자로 주어진 세마포어를 위해 대기중인 가장 높은 우선순위의 스레드와, 
 * 두번째 인자로 주어진 세마포어를 위해 대기중인 가장높은 우선순위의 스레드와 비교하여,
 * a스레드의 우선순위가 더 높으면 1, otherwise 0*/
bool
cmp_sema_elem_priority (const struct list_elem *a, const struct list_elem *b, void *aux) {
	struct semaphore_elem *sema_elem_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_elem_b = list_entry(b, struct semaphore_elem, elem);

	struct semaphore sema_a = sema_elem_a->semaphore;
	struct semaphore sema_b = sema_elem_b->semaphore;

	struct list *waiters_a = &sema_a.waiters;
	struct list *waiters_b = &sema_b.waiters;

	if(list_empty(waiters_a) == true)
		return false;
	
	if(list_empty(waiters_b) == true)
		return true;
	
	struct thread* t_a = list_entry(list_begin(waiters_a), struct thread, elem);
	struct thread* t_b = list_entry(list_begin(waiters_b), struct thread, elem);
	
	return t_a->priority > t_b->priority;

	/** the first
	struct semaphore sema_a = se_a->semaphore;
	struct semaphore sema_b = se_b->semaphore;
	
	struct list *waiters_a = &sema_a.waiters;
	struct list *waiters_b = &sema_b.waiters;
	
	struct thread* t_a = list_begin(waiters_a);
	struct thread* t_b = list_begin(waiters_b);
	*/
	
	/** the second
	int64_t a_pri = list_entry(list_begin(&sema_a->semaphore), struct thread, elem)->priority;
	int64_t b_pri = list_entry(list_begin(&sema_b->semaphore), struct thread, elem)->priority;
	return a_pri > b_pri;
	*/
}

bool
cmp_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	struct thread *t_a = list_entry(a, struct thread, donor_list_elem);
	struct thread *t_b = list_entry(a, struct thread, donor_list_elem);
	return t_a->priority > t_b->priority;
}


/* 현재 스레드가 자신이 필요한 lock 을 점유하고 있는 스레드에게 priority를 상속하는 함수 */
void
donate_priority(void) {
    struct thread *curr = thread_current(); // 검사중인 스레드
    struct thread *holder;					// curr이 원하는 락을 가진드스레드

    int priority = curr->priority;

    for (int i = 0; i < 8; i++) {            // Nested donation 의 최대 깊이는 8
        if (curr->lock_im_waiting == NULL)   // if there is no more nested one
            return;
        holder = curr->lock_im_waiting->holder;
        holder->priority = priority;
        curr = holder;
    }
}

/* 기부자 목록에서 반환될 락을 요청했던 스레드 엔트리 제거 */
void 
remove_donor(struct lock *lock) {
    struct list_elem *e;
  	struct thread *curr = thread_current ();

  	for (e = list_begin (&curr->donor_list); e != list_end (&curr->donor_list); e = list_next (e)){
    	struct thread *t = list_entry (e, struct thread, donor_list_elem);
		if (t->lock_im_waiting == lock) {
			list_remove (&t->donor_list_elem);
		}
	}
}

// Donate 받은 priority 와 current priority를 비교해서 donate받은게 더 크다면 current을 update
void
refresh_priority (void) {
    struct thread *curr = thread_current ();
	curr->priority = curr->pri_before_dona;

    if (!list_empty (&curr->donor_list)) {  // 기부 받은 priority가 남아있다면
		list_sort (&curr->donor_list, cmp_donation_priority, 0);

    	struct thread *front = list_entry (list_front (&curr->donor_list), struct thread, donor_list_elem);
		if (front->priority > curr->priority)
			curr->priority = front->priority; // 가장 높은  priority를 현재 thread의 priority로 설정 
    }
}
