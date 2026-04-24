#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/mutex.h>


// ------- Part A -------
// the size of the lock table array
#define MAX_LOCKS 10

struct lock_info
{
	struct mutex *lock;
	pid_t owner;
};

// we make a table of all our locks
static struct lock_info lock_table[MAX_LOCKS];
static int lock_count = 0;

// A helper function that search's the lock_table to find the entry corresponding to a given lock
static struct lock_info *get_lock_info(struct mutex *lock)
{
	int i;

	// we loop over all stored locks
	for (i = 0; i < lock_count; i++)
	{
		// if the lock we are looking for is found we return it so we can update or read it later
		if (lock_table[i].lock == lock)
		{
			return &lock_table[i];
		}
	}

	// create new entry if lock not found bec this is the first time we are tracking this lock
	if (lock_count < MAX_LOCKS)
	{
		lock_table[lock_count].lock = lock;
		lock_table[lock_count].owner = -1;
		return &lock_table[lock_count++];
	}

	// we are full and cant take anymore locks
	return NULL;
}

static void lock_acquire(struct mutex *lock)
{
	pid_t pid = current->pid;
	struct lock_info *info;

	printk(KERN_INFO "[MiniLockdep] Thread %d acquiring lock\n", pid);

	mutex_lock(lock);

	info = get_lock_info(lock);
	if (!info)
		return;

	if (info->owner != -1 && info->owner != pid)
	{
		printk(KERN_INFO "[MiniLockdep] TRANSFER: %d -> %d\n", info->owner, pid);
	}

	info->owner = pid;

	printk(KERN_INFO "[MiniLockdep] Thread %d acquired lock\n", pid);
}

static void lock_release(struct mutex *lock)
{
	pid_t pid = current->pid;
	struct lock_info *info;

	mutex_unlock(lock);

	info = get_lock_info(lock);
	if (!info)
		return;

	if (info->owner == pid)
	{
		printk(KERN_INFO "[MiniLockdep] Thread %d released lock\n", pid);
		info->owner = -1;
	}
}

// ------- Part B -------
// i will add stuff tom ISA