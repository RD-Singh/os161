/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;
volatile int active_threads = 0;

/* Data structures for rope mappings */

struct hooks {
    struct ropes *rope_ref;
};

struct ropes {
	struct lock *rope_lock;
	volatile bool rope_cut;
    int rope_num;
};

struct stakes {
    struct ropes *rope_ref;
	struct lock *stake_lock;
};

struct hooks ab_hooks[NROPES];
struct stakes ab_stakes[NROPES];

struct lock *remove_ropes;
struct lock *thread_count_lock;


/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

/**
 * ################################################## DATA STRUCTURES ##################################################
 * 
 * Hooks and stake data is stored inside of two arrays. These arrays of size NROPES and contain NROPES number of structs. The ab_hooks array 
 * (airballoon hooks) contains NROPES number of hook structs and the ab_stakes array (airballoon stakes) contains NROPES number of stake 
 * structs. The hooks structs contains only one variable, a pointer reference to the rope it is attached to. 
 * 
 * The stakes struct on contains a reference to the rope it is attached to and a lock. The lock prevents more than one character (marigold
 * and flowerkiller) from accessing a single stake at the same time. If flowerkiller is in the process of switching to ropes from a pair of stakes
 * marigold should not be able to access those stakes. This is essentially the equivalent of preventing marigold from being able to push 
 * flowerkiller away from ropes he is switching and vice versa. Although marigold/flowerkiller can't access a stake at the same time, they are 
 * still able to grab ropes from the other available stakes. 
 * 
 * The last data structure used to create this simulation is the ropes data structure. The ropes data structure stores critical information on 
 * the ropes that are attached to the airballon/have been severed from the airballoon. Each rope will have information on whether it has been
 * cut or not (rope_cut), what its assigned number is (rope_num) and finally a lock to prevent characters from accessing a specific rope at the
 * same time.
 * 
 * The idea behind these data structures is to prevent each character from accessing ropes directly. The determining factor when accessing rope
 * should be the stake or hook that the character chooses and not the rope itself. In other words, each character is to focused on their hooks
 * or stakes to see the rope and all they see is the connecting point between the rope and hook/stake.
 * 
 * Finally, there are two global variables called ropes_left and active_threads. Ropes_left keeps track of how many ropes have been severed. Once
 * the number hits 0 all threads are able to exit and this signifies that the thread is done and the airballoon has been detached. The active_threads
 * value keeps track of how many threads are currently still running. Both of these variables have locks specific to them that make it so that 
 * no threads can update the values at the same time (remove_ropes and thread_count_lock). This ensures mutual exclusion. The ropes_left value 
 * decrements whenever marigold or dandelion sever a thread and the value is initially set to NROPES. The active_threads value increments whenever 
 * a new thread is started and then when each thread finishes, it decrements. It helps to control when the main thread exits and prints the done 
 * print statement.
 * 
 * ################################################# AIRBALLOON THREAD #################################################
 * 
 * The airballoon thread is the initial thread that is called to start this simulation. The thread starts by initializing each of the structs for
 * hooks, ropes and stakes, the arrays for hooks and stakes and it also creates each of the locks. The initialization ensures that each hook and 
 * stake is associated with corresponding rope. Allocation of memory for each of the references to a rope is done during this step so that all other
 * data structures have access to the rope data. So rope 1 would initially be associated with stake 1 and hook 1, rope 2 would initially associated 
 * with stake 2 and hook 2 etc. This ensures that dandelion, marigold and flowerkiller are able to see if a rope is being used. The airballoon thread 
 * then starts the dandelion, marigold and flowerkiller threads. Once all threads are complete (active_threads == 0), the thread frees all its allocated 
 * memory, resets all global values (so that the thread can be seamlessly started again), destroys all created locks and finally prints that it is 
 * done and exits. 
 * 
 * ################################################# DANDELION THREAD ##################################################
 * 
 * The dandelion thread is one of the first threads that the airballoon thread forks and creates. This thread is responsible for removing ropes from hooks.
 * The thread will only ever have access to the hooks array from which it will be able to access the reference to the rope in order to sever it. The thread
 * starts out by indicating its starting and then immediately enters a loop where it continuously runs until the ropes left have hit 0. The thread gets 
 * a random value to access a random index in the hook array. This would represent Prince dandelion randomly selecting a rope to detach from a hook. Given 
 * this random index, the dandelion thread then gets the hook struct at the specific index and then the rope referenced in that struct. It calls the 
 * lock_acquire function which attempts to acquire the referenced rope's lock so that dandelion can have access to it. Once acquired, the thread checks
 * if the rope has already been detached or severed from a stake. If it has, the thread releases the lock as the rope as already been removed. If it hasn't,
 * it sets the boolean value representing whether a rope has been cut or not to true to indicate that the rope is now cut. It then decrements the value for
 * ropes_left to indicate that one more rope as been cut and finally, prints which index the rope was severed from. Once done, the thread releases the 
 * acquired rope's lock and yields. This will keep happening until no ropes are left to sever. Once all ropes have been severed, the thread prints that it 
 * is done and it acquires the lock for the active_threads variable in order to decrement it. Once done, it releases the lock and exits.
 * 
 * ################################################## MARIGOLD THREAD ##################################################
 * 
 * The marigold thread has the exact same behaviour as the dandelion thread except now, instead of just holding a rope, it is also holding a stake. This is
 * done to prevent flowerkiller from accessing the same stake the thread is trying to modify. In the same way that dandelion randomly chose an index for a 
 * hook in the hook's array, marigold choose a random index for a stake in the ab_stake array. The thread first acquires the lock for the stake so that 
 * flowerkiller is unable to access the rope on that stake (uses the stake structs stake_lock). Once this is done, it gets the referenced rope's lock and 
 * in a similar fashion to dandelion, checks if the rope has been cut. If it has it releases all locks and continues the loop. If it hasn't, it sets the 
 * ropes rope_cut value to true and prints out what rope it severed and the stake it severed the rope at. It also decrements the ropes_left global variable.
 * On a successful cut, all locks are released and the thread yields. It then has the same exit logic as dandelion.
 * 
 * ################################################ FLOWERKILLER THREAD ################################################
 * 
 * Flowerkiller works similar to marigold except it now grabs two stakes and two ropes. It does this by first getting a random index for two stakes (A and B)
 * that it wants to switch. Once it grabs these indexes, it then checks to see if stake_a <= stake_b. There are two reasons it does this. The first reason is 
 * that we don't want the two random indexes to be equal as flowerkiller can't switch a rope from a stake to the same stake. The second reason is to ensure t
 * that there are no deadlocks. By having it so that stake A is always less than stake B, this makes it so that no two flowerkiller threads can request the 
 * same pair of ropes. Let's say for example the less than condition didn't exist. Now let's say flowerkiller thread 1 was accessing stake's 1 and 2 and then
 * flowerkiller thread 2 was accessing stake's 2 and 1 in those orders respectively. If flowerkiller thread 1 acquires the lock for stake 1 and flowerkiler
 * thread 2 acquire's the lock for stake 2, there is now a deadlock as thread 1 is attempting to acquire stake 2 as its second lock and thread 2 is attempting
 * to acquire stake 1 as its second lock, both of which are already being used. This deadlock condition is prevented if no two flowerkiller threads can access 
 * the same pair of threads at the same time. Once this condition is checked, the thread acquires both stake locks and checks if the ropes at those locks have
 * been severed. If they have, it releases the locks and continues the loop execution. If they haven't (both have to be intact), then it acquires the locks
 * for both referenced ropes and switches the ropes that each stake is associated with. Then for both ropes, it prints what rope number it was, what stake it 
 * was at and what stake it was moved to. After completing this, all locks are released and finally, the thread yields. This thread also follows the exact 
 * same exit logic as the dandelion thread. 
 * 
 * ################################################## BALLOON THREAD ###################################################
 * 
 * The balloon thread continuously checks to see if all ropes have been severed using the ropes_left global variable. It keeps waiting until all ropes have been
 * severed. Once all ropes are severed, it prints the statement "Balloon freed and Prince Dandelion escapes!" and then prints that the thread is done. It also
 * decrements the active threads variable so it has to acquire the thread_count_lock to decrease the value and then releases the lock finally exiting.
 * 
 */

static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");

    /* Check if there are any ropes still left */
    while(ropes_left > 0){
        int hook_index = random() % NROPES;

        struct ropes *r = ab_hooks[hook_index].rope_ref;

        lock_acquire(r->rope_lock);

        /* Checks if the rope has already been cut */
        if(!r->rope_cut){

            r->rope_cut = true;

			kprintf("Dandelion severed rope %d\n", hook_index);

            /* Decrements the number of ropes left */
            lock_acquire(remove_ropes);
            ropes_left--;
            lock_release(remove_ropes);

            lock_release(r->rope_lock);
            thread_yield();
        } else {
            lock_release(r->rope_lock);
        }
    }

	kprintf("Dandelion thread done\n");

    /* Decrements the number of active threads for the main thread to finish*/
    lock_acquire(thread_count_lock);
    active_threads--;
    lock_release(thread_count_lock);
}

static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");

    while(ropes_left > 0){
        int stake_index = random() % NROPES;

        lock_acquire(ab_stakes[stake_index].stake_lock);

        struct ropes *r = ab_stakes[stake_index].rope_ref;
        lock_acquire(r->rope_lock);

        if(!r->rope_cut){

            r->rope_cut = true;

			kprintf("Marigold severed rope %d from stake %d\n", r->rope_num, stake_index);

            lock_acquire(remove_ropes);
            ropes_left--;
            lock_release(remove_ropes);

            lock_release(r->rope_lock);
            lock_release(ab_stakes[stake_index].stake_lock);
            thread_yield();

        } else {
            lock_release(r->rope_lock);
            lock_release(ab_stakes[stake_index].stake_lock);
        }
    }

   	kprintf("Marigold thread done\n");
    
    lock_acquire(thread_count_lock);
	active_threads--;
	lock_release(thread_count_lock);
}

static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Lord FlowerKiller thread starting\n");

    while(ropes_left > 0){

        /* Gets to indexes to switch a pair of ropes from a pair of stakes */
        int stake_a = random() % NROPES;
        int stake_b = random() % NROPES;
        
        /**
         * Ensures that the indexes it chose are not the same and that the first
         * index is smaller than the second to prevent any other active flowerkiller
         * thread from getting the same pair of stakes.
        */
        if(stake_a <= stake_b){
            continue;
        }

        lock_acquire(ab_stakes[stake_a].stake_lock);
        lock_acquire(ab_stakes[stake_b].stake_lock);

        struct ropes *rope_a = ab_stakes[stake_a].rope_ref;
        struct ropes *rope_b = ab_stakes[stake_b].rope_ref;

        if(!rope_a->rope_cut && !rope_b->rope_cut){

            lock_acquire(rope_a->rope_lock);
            lock_acquire(rope_b->rope_lock);

            /* Switches the ropes on the specified stakes */
            ab_stakes[stake_a].rope_ref = rope_b;
            ab_stakes[stake_b].rope_ref = rope_a;

            kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope_a->rope_num, stake_a, stake_b);
		    kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope_b->rope_num, stake_b, stake_a);

            lock_release(rope_b->rope_lock);
            lock_release(rope_a->rope_lock);
            lock_release(ab_stakes[stake_b].stake_lock);
            lock_release(ab_stakes[stake_a].stake_lock);

            thread_yield();

        } else {
            lock_release(ab_stakes[stake_b].stake_lock);
            lock_release(ab_stakes[stake_a].stake_lock);
        }
    }

    kprintf("Lord FlowerKiller thread done\n");
    
    lock_acquire(thread_count_lock);
	active_threads--;
	lock_release(thread_count_lock);
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	while(ropes_left > 0);

	kprintf("Balloon freed and Prince Dandelion escapes!\n");
	kprintf("Balloon thread done\n");

	lock_acquire(thread_count_lock);
	active_threads--;
	lock_release(thread_count_lock);
}

int
airballoon(int nargs, char **args)
{
	
	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

    /**
     * Initializes all locks, data structures, rope references and global variables
     * that all other threads make usre of 
     */

	remove_ropes = lock_create("ropes left lock");
	thread_count_lock = lock_create("thread count lock");

	for(int i = 0; i < NROPES; i++){
        struct ropes *r = kmalloc(sizeof(struct ropes));

        r->rope_lock = lock_create("rope lock");
		r->rope_cut = false;
        r->rope_num = i;

        ab_hooks[i].rope_ref = r;
        ab_stakes[i].rope_ref = r;
        ab_stakes[i].stake_lock = lock_create("stake lock");
	}

	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;
	active_threads++;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;
	active_threads++;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
		active_threads++;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;
	active_threads++;

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:

    /* Makes it so that this thread waits until all threads have exited */
	while(active_threads > 0);

    /* Destroys all locks that were created and frees all memory from rope references */
	for(int i = 0; i < NROPES; i++){
		lock_destroy(ab_stakes[i].rope_ref->rope_lock);
		lock_destroy(ab_stakes[i].stake_lock);

		kfree(ab_stakes[i].rope_ref);

        ab_stakes[i].rope_ref = NULL;
        ab_hooks[i].rope_ref = NULL;
	}
	lock_destroy(remove_ropes);
	lock_destroy(thread_count_lock);

    /** 
     * Sets the ropes left value back to its initial value so that the thread can be used again
     * without exiting the shell
    */
	ropes_left = NROPES;
	
	kprintf("Main thread done\n");
	return 0;
}