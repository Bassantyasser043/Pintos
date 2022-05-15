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

#define DEPTH 9
static struct semaphore_elem *max_cond(struct list *waiters);
/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{
    ASSERT(sema != NULL);

    sema->value = value;
    list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
// ***********************************************************************************************************
void sema_down(struct semaphore *sema)
{
    // semaWait
    // The lock is already in use, So just wait until the thread release it
    enum intr_level old_level;

    ASSERT(sema != NULL);
    ASSERT(!intr_context());

    old_level = intr_disable();
    while (sema->value == 0)
    {
        // Modify to insert thread at waiters list in order of priority
        list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL);
        thread_block();
    }
    sema->value--;
    intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
    enum intr_level old_level;
    bool success;

    ASSERT(sema != NULL);

    old_level = intr_disable();
    if (sema->value > 0)
    {
        sema->value--;
        success = true;
    }
    else
        success = false;
    intr_set_level(old_level);

    return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
  and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
/// *************************************************************************
void sema_up(struct semaphore *sema)
{
    // semaSignal
    // Give signal that the lock is free, and could be reused
    ASSERT(sema != NULL);

    enum intr_level old_level;
    old_level = intr_disable();

    // If the list of waiters Not empty, then the lock is acquired
    // and will be reused
    bool preemption = false;
    if (!list_empty(&sema->waiters))
    {
        struct list_elem *ele;
        struct thread *next_thread;
        ele = list_max(&sema->waiters, cmp_priority, NULL);
        next_thread = list_entry(ele, struct thread, elem);
        // remove the thread from waiters for the lock
        list_remove(ele);
        // unblock it, insert to the ready_list
        thread_unblock(next_thread);
        // Okay, thread is inserted to the ready_list
        // we need to comapre the priority of the current_thread and the added one
        // if the added thread is higher in priority, context switch :)
        if (thread_current()->priority < next_thread->priority)
            preemption = true;
    }

    sema->value++;

    // if the thread_yield() function is applied inside if statement
    // then the deadlock may happen as u know the sema->value will be incremented
    // after context switch
    if (preemption) thread_yield();

    intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
    struct semaphore sema[2];
    int i;

    printf("Testing semaphores...");
    sema_init(&sema[0], 0);
    sema_init(&sema[1], 0);
    thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
    for (i = 0; i < 10; i++)
    {
        sema_up(&sema[0]);
        sema_down(&sema[1]);
    }
    printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
    struct semaphore *sema = sema_;
    int i;

    for (i = 0; i < 10; i++)
    {
        sema_down(&sema[0]);
        sema_up(&sema[1]);
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
void lock_init(struct lock *lock)
{
    ASSERT(lock != NULL);

    lock->holder = NULL;
    sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock)
{
    // like semaWait but in mutex locks
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(!lock_held_by_current_thread(lock));

    // YARAB
    // Interrupt disable Not handler
    enum intr_level old_level;

    int depth = 0;
    struct lock *cur_lock;
    struct thread *cur_thread = thread_current();

    // The lock holder isn't null (Thread using the lock) and Round-Robin Scheduler
    if(lock->holder != NULL && !thread_mlfqs) {

        // ya current_thread we are (lock) waiting for you
        // to finish the release the lock
        cur_thread->lock_waiting = lock;
        cur_lock = lock;

        // Nested Priority donation
        // current_thread priority greater than the required lock
        // Donate the priority to the lock as it has a higher priority
        while(cur_lock && cur_thread->priority > cur_lock->max_priority && depth++ < DEPTH) {
            cur_lock->max_priority = cur_thread->priority;
            thread_donate_priority(cur_lock->holder);
            cur_lock = cur_lock->holder->lock_waiting;
        }

    }

    sema_down(&lock->semaphore);
    old_level = intr_disable();
    cur_thread = thread_current();

    // Round-Robin Scheduler
    if(!thread_mlfqs) {
        // m34 7d mstany __ 5alas
        cur_thread->lock_waiting = NULL;
        lock->max_priority = cur_thread->priority;
        thread_add_lock(lock);
    }

    lock->holder = cur_thread;
    intr_set_level(old_level);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
    bool success;

    ASSERT(lock != NULL);
    ASSERT(!lock_held_by_current_thread(lock));

    success = sema_try_down(&lock->semaphore);
    if (success)
        lock->holder = thread_current();
    return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{
    enum intr_level old_level;

    ASSERT(lock != NULL);
    ASSERT(lock_held_by_current_thread(lock));

    old_level = intr_disable();

    // remove the thread that holds the lock on donation list
    // and set priority properly
    if (!thread_mlfqs)
        thread_remove_lock (lock);

    lock->holder = NULL;
    sema_up(&lock->semaphore);

    intr_set_level(old_level);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
    ASSERT(lock != NULL);

    return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
    struct list_elem elem;      /* List element. */
    struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
    ASSERT(cond != NULL);

    list_init(&cond->waiters);
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
void cond_wait(struct condition *cond, struct lock *lock)
{
    struct semaphore_elem waiter;

    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));

    sema_init(&waiter.semaphore, 0);
    // Modify to insert thread at waiters list in order of priority
    list_insert_ordered(&cond->waiters, &waiter.elem, cmp_priority, NULL);
    lock_release(lock);
    sema_down(&waiter.semaphore);
    lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));

    if (!list_empty(&cond->waiters))
    {
        struct semaphore_elem *max_sema = max_cond(&cond->waiters);
        list_remove(&max_sema->elem);
        sema_up(&max_sema->semaphore);

        /*
        sema_up(&list_entry(list_pop_front(&cond->waiters),
                            struct semaphore_elem, elem)
                    ->semaphore);
        */
    }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);

    while (!list_empty(&cond->waiters))
        cond_signal(cond, lock);
}


// Oooooooooooooooooooooooooooooooooooooooooooooooops
static struct semaphore_elem *max_cond(struct list *waiters)
{
    struct semaphore_elem *sema = list_entry(list_begin(waiters), struct semaphore_elem, elem);
    struct list_elem *max = list_front(&sema->semaphore.waiters); // thread element

    if (list_begin(waiters) != list_end(waiters))
    {
        struct list_elem *e;
        for (e = list_next(&sema->elem); e != list_end(waiters); e = list_next(e))
        {
            struct semaphore_elem *temp_sema = list_entry(e, struct semaphore_elem, elem);        // sema element of e
            struct list_elem *temp = list_front(&temp_sema->semaphore.waiters); // temp max thread elem

            struct thread *temp_thread = list_entry(temp, struct thread, elem); // temp max thread
            struct thread *max_thread = list_entry(max, struct thread, elem);   // max thread

            if (temp_thread->priority > max_thread->priority)
            {
                max = temp;
                sema = temp_sema;
            }
        }
    }
    return sema;
}