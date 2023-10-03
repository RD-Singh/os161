/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <threadlist.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
        struct semaphore *sem;

        sem = kmalloc(sizeof(struct semaphore));
        if (sem == NULL) {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void
P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

/*
 * Create a lock with a given NAME string
 */
struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        /* Create instance of the lock struct */
        lock = kmalloc(sizeof(struct lock));
        if (lock == NULL) {
                return NULL;
        }

        /* Associate the provided name with the lock instance */
        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }

        /**
         * Create a wait channel to put threads to sleep when 
         * the lock is held and wake threads up when the lock
         * is released. This will keep track of any threads put
         * to sleep and is used so that the spinlock in the
         * lock_acquire implementation doesn't busy-wait. This 
         * is used similar to how it is used in the semaphore 
         * implementation
         */
        lock->lock_wchan = wchan_create(lock->lk_name);
	if (lock->lock_wchan == NULL) {
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}

        /* Creates an instance of a spinlock */
        spinlock_init(&lock->lock_sl);

        /* Set the thread that is holding a lock to NULL */
        lock->held_thread = NULL;

        return lock;
}

/* Destroys the provided lock */
void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);

        /**
         * Regardless of whether there is a thread holding the lock
         * we want to make sure any thread we put to sleep is reawakened 
         * so that it can continue its execution. Lock_destroy should 
         * only be called if the lock has been released and the assumption
         * is that this is how someone will use the lock.
         */
        spinlock_acquire(&lock->lock_sl);

        lock->held_thread = NULL;

        wchan_wakeall(lock->lock_wchan, &lock->lock_sl);

        spinlock_release(&lock->lock_sl);

        /** 
         * Frees memory used by the lock, cleans up the spinlock and destroys
         * the wait channel
        */
        spinlock_cleanup(&lock->lock_sl);
	wchan_destroy(lock->lock_wchan);
        kfree(lock->lk_name);
        kfree(lock);
}

/**
 * Given a lock, checks whether the lock is held by a thread.
 * If it is, the thread attempting to acquire the lock is put 
 * to sleep. If it isn't, the lock's held_thread value is set
 * to whatever thread is attempting to acquire the lock at the
 * time. 
 */
void
lock_acquire(struct lock *lock)
{
        KASSERT(lock != NULL);
        KASSERT(curthread->t_in_interrupt == false);

        /**
         * ADD A COMMENT ABOUT WHY THE SPINLOCK IS USED 
         * The spinlock is used to protect the wait channel as well.
        */
        spinlock_acquire(&lock->lock_sl);

        /**
         * Checks if a thread already holds the lock. If it
         * does, the thread is put into a loop and immediately 
         * put to sleep. This way, the thread will not busy-wait 
         * due to the usage of spinlocks to protect the wait channel.
         * Since wait channels are being used, the threads that are 
         * put to sleep will be tracked inside the lock's wchan 
         * making it possible for them be awakened when necessary.
         */
        while(lock->held_thread != NULL){
		wchan_sleep(lock->lock_wchan, &lock->lock_sl);
        }

        /** 
         * Sets the the thread holding the lock to the current thread 
         * (from <current.h>) 
        */
        lock->held_thread = curthread;

        spinlock_release(&lock->lock_sl);
}

/** 
 * Releases the lock that is held and wakes a single thread on the
 * wait channel (if there is any thread that is asleep)
*/
void
lock_release(struct lock *lock)
{
        KASSERT(lock != NULL);

        /* Ensure that only the thread that holds the lock is releasing it */
        KASSERT(lock_do_i_hold(lock));

	spinlock_acquire(&lock->lock_sl);

        /* Sets the value for the thread holding the lock to NULL */
        lock->held_thread = NULL;

        /* Wakes a single thread so that it can acquire the lock */
	wchan_wakeone(lock->lock_wchan, &lock->lock_sl);

	spinlock_release(&lock->lock_sl);
}

/** 
 * Checks if the thread that is held is equal to the
 * current thread 
*/
bool
lock_do_i_hold(struct lock *lock)
{
        KASSERT(lock != NULL);
        
        return curthread == lock->held_thread;
}

////////////////////////////////////////////////////////////
//
// CV

/* Creates a condition variable */
struct cv *
cv_create(const char *name)
{
        struct cv *cv;

        cv = kmalloc(sizeof(struct cv));
        if (cv == NULL) {
                return NULL;
        }

        cv->cv_name = kstrdup(name);
        if (cv->cv_name==NULL) {
                kfree(cv);
                return NULL;
        }

        /** 
         * Creates a wait channel that will keep track of threads
         * that have been put to sleep so that they can be woken
        */
        cv->cv_wchan = wchan_create(cv->cv_name);
	if (cv->cv_wchan == NULL) {
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}

        /** 
         * Initializes a spinlock
         */
        spinlock_init(&cv->cv_sl);

        return cv;
}

/** 
 * Cleans up any allocated memory, destroys the cv that
 * was passed as an argument
*/
void
cv_destroy(struct cv *cv)
{
        KASSERT(cv != NULL);

        spinlock_cleanup(&cv->cv_sl);
	wchan_destroy(cv->cv_wchan);
        kfree(cv->cv_name);
        kfree(cv);
}

/**
 * Puts the thread to sleep by adding it to the wait channel. 
 * The lock that is passed an argument is used to protect
 * the critical section in the function that called this function 
 */
void
cv_wait(struct cv *cv, struct lock *lock)
{
        KASSERT(cv != NULL);
        KASSERT(lock != NULL);

        /** 
         * Ensures that the current thread is holding the lock
        */
        KASSERT(lock_do_i_hold(lock));

        /** 
         * Spinlock to both protect the wait chanel and to ensure
         * no race conditions. 
        */
        spinlock_acquire(&cv->cv_sl);

        /** 
         * As per the specification, releases the provided lock
        */
        lock_release(lock);

        /* Puts the current thread to sleep */
        wchan_sleep(cv->cv_wchan, &cv->cv_sl);

        spinlock_release(&cv->cv_sl);

        /** 
         * As per the specification, acquires the lock again.
         * This only happens after the thread has woken up
        */
        lock_acquire(lock);
}

/**
 * Wakes up the function at the top of the wait channel's
 * wc_threads list. This thread can then reacquire the lock 
 * that it released before going to sleep. 
 */
void
cv_signal(struct cv *cv, struct lock *lock)
{
        KASSERT(cv != NULL);
        KASSERT(lock != NULL);

        KASSERT(lock_do_i_hold(lock));

        spinlock_acquire(&cv->cv_sl);

        /**
         * Wakes the thread at the top of the wc_threads list
         */
        wchan_wakeone(cv->cv_wchan, &cv->cv_sl);

        spinlock_release(&cv->cv_sl);
}

/**
 * This function wakes up all threads in the wait channel.
 * This would most likely be used when there is a shared 
 * resource and multiple threads are able to access that
 * resource at the same time without causing any problems.
 * 
 * Eg. If some data that is used in multiple places in
 * the OS just finished updating, when this function is 
 * called all threads in the wait channel now wake up and
 * can access this data. These threads would only be 
 * reading the data and would not be making any changes
 */
void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	KASSERT(cv != NULL);
        KASSERT(lock != NULL);

        KASSERT(lock_do_i_hold(lock));

        spinlock_acquire(&cv->cv_sl);

        /* Wakes all threads in the wc_threads list */
        wchan_wakeall(cv->cv_wchan, &cv->cv_sl);

        spinlock_release(&cv->cv_sl);
}
