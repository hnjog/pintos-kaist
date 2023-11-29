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

	// 다른 스레드가 반환할때까지 대기
	while (sema->value == 0) {
		// 삽입될 때, 스레드 우선순위에 따라 삽입
		list_insert_ordered(&sema->waiters,&thread_current()->elem,cmp_priority,NULL);

		// 여기서 문맥교환하며
		// 대기한다
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

	if (!list_empty (&sema->waiters))
	{
		// 차후, donation 과정에서 리스트 중간의 priority가 변형될 수 있기에
		// 미리 sort함
		list_sort(&sema->waiters,cmp_priority,NULL);

		// thread_unblock해준다
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	sema->value++;
	// unblock 한 녀석이 우선순위가 높다면 현재 실행되는 녀석이 아니라
	// 그 녀석이 실행되어야 함
	compare_Curr_ReadyList();
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
// 락을 획득하고, 필요한 경우 락을 얻을 때까지 대기(현재 스레드가 이미 락을 가지고 있지 않아야 함)
// 이 함수는 인터럽트 핸들러 내에서 호출되면 안된다
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	// 내부에 holder가 존재하여 wait를 해야한다면
	// wait를 하게 될 lock 자료구조 포인터를 저장(thread에 추가한 것)
	// lock의 현재 holder의 대기자 list에 추가 (holder->waitlist)
	// donate_priority 호출
	// lock 획득 후, lock의 holder를 갱신

	if(lock->holder != NULL)
	{
		thread_current()->lpWaitLock = lock;

		list_insert_ordered(&lock->holder->waitList, &thread_current()->waitElem, cmp_priority, NULL);

		donate_priority();
	}

	sema_down(&lock->semaphore);

	lock->holder = thread_current();
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

	remove_with_lock(lock);
	
	refresh_priority();
	
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

// 이 함수는 lock을 해제하고, cond가 signal을 받을 때까지 대기한다
// 이 함수 호출 시, lock을 호출해야 함
// mesa 스타일의 모니터 함수이며
// 신호를 보내고 받는 것은 atomic 하지 않음
// mesa : 조건 변수의 대기(wait)와 신호(signal)이 스레드 간의 동기화에 영향을 주지 않음
// 스레드가 조건 변수를 기다리고 있고, 누가 신호를 보내도, 누가 해당 신호를 받을지 보장되지 않음
// 그렇기에, 신호를 받은 스레드는 자신의 조건을 다시 확인(조건에 맞지 않으면 다시 대기)
// hoare : 대기와 신호가 동기화에 직접 영향을 줌
// 신호가 발생하면 대기하고 있는 스레드가 신호를 받아 실행
// mesa가 더 안정적이고, hoare가 더 빠른 응답성을 지님
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	// 0으로 초기화 하여, 밑의 sema_down에서 바로 '대기 상태'로 만들어준다 (문맥교환)
	// 이거 cond의 waiter 들의 세마포어들은
	// 하나의 요소만 가지게 되므로
	// waiter.semaphore가 0이면 mutex 에 가까워짐
	// 또한 추가적으로 cond->waiters를 지정하여
	// list_insert 해주는 것은 아님
	// 따라서 cond 는 'mutex'리스트를 가지고 다닌다고 생각한다
	// 세마포어 쪽에서 '다수'의 리스트를 가지고 있다 가정하는 것은
	// 말 그대로 이거와는 별도의 이야기이다
	sema_init (&waiter.semaphore, 0);

	// 모니터의 세마포어 리스트에 우선순위에 따라서 넣어준다
	// 다만 비어있는 녀석이기에 가장 끝(내림차순)에 정렬될 것

	// 애초에 그렇다면 기존 코드가 틀린것은 아닌것 같은데?
	//list_push_back(&cond->waiters,&waiter.elem);
	list_insert_ordered(&cond->waiters,&waiter.elem,cmp_sem_priority,NULL);

	// 락을 풀어준다(해당 락은 이진 세마포어(mutex) 들을 리스트로 가짐)
	// (이전에 lock_acquire가 호출이 되었음)
	// 이 위까지는 '원자적'인 작동이 보장됨 (lock이 되어 있었으므로)

	// lock을 해제함으로서, 특정한 조건을 만족하도록 한다
	// 일반적인 모니터는 모니터 내부의 스레드가 계속 lock을 유지하는 것이지만
	// 해당 코드는 위에서 계속 cond->waiters에 넣어주는 것을 생각하고 있음
	// (다만 윗부분은 atomic 함)
	lock_release (lock);

	// 선언한 녀석의 p 연산을 통하여
	// 문맥교환을 하기 위함
	// 따라서 밑의 lock_acquire이 바로 호출되지는 않음
	// 여기서 wait 상태로 들어가고 lock이 풀려 있음
	// 특정한 상황을 기다리기에 '조건 변수'라 할 수 있음
	sema_down (&waiter.semaphore);

	// 누군가가 sema_up을 호출해줌
	// cond에 적절한 sema_up이 호출된 상태이다
	// lock을 요청한다
	// 조건 변수로 인하여 스레드가 깨어났을때, 반드시 락을 요청해야 함
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	// cond 내부에서 하나를 깨우려고 함
	// 따라서 semaphore 리스트 를 정렬하여, 가장 높은 우선순위를 가진
	// 녀석을 sema_up 해준다
	if (!list_empty (&cond->waiters))
	{
		list_sort(&cond->waiters,cmp_sem_priority,NULL);

		// thread_unblock 되는 녀석은 현재 block 된 녀석들 중
		// 가장 우선순위가 높음
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

bool cmp_sem_priority(const struct list_elem* a,const struct list_elem* b, void* aux)
{
	struct semaphore_elem* aSemaElem = list_entry (a, struct semaphore_elem, elem);
	struct semaphore_elem* bSemaElem = list_entry (b, struct semaphore_elem, elem);

	// 왼쪽 리스트 비어있으면
	// 오른쪽이 더 큰 상황
	if (list_empty(&aSemaElem->semaphore.waiters) == true)
		return false;
	
	// 오른쪽 리스트 비어있으면
	// 왼쪽이 더 크다
	if(list_empty(&bSemaElem->semaphore.waiters) == true)
		return true;

	struct list_elem* aSemalistFirstElem = list_front(&aSemaElem->semaphore.waiters);
	struct list_elem* bSemalistFirstElem = list_front(&bSemaElem->semaphore.waiters);

	struct thread* aBPT = list_entry (aSemalistFirstElem, struct thread, elem);
	struct thread* bBPT = list_entry (bSemalistFirstElem, struct thread, elem);

	if(aBPT->priority > bBPT->priority)
		return true;

	return false;
}