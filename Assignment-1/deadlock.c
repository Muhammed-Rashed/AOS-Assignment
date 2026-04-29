#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/delay.h>

/*
Deadlock scenario:

Thread A <----- Lock 1
    |             ^
    |             |
    |             |
    ⌄             |
 Lock 2 ------> Thread B

Thread A: acquires lock1 then tries lock2
Thread B: acquires lock2 then tries lock1
Circular wait → deadlock

*/

// Global locks
struct mutex lock1;
struct mutex lock2;

// Kernel thread handles
struct task_struct *threadA, *threadB;

static int thread_A(void *data) {
    // Acquire lock1, can be interrupted if the thread is asked to stop
    if (mutex_lock_interruptible(&lock1))
        return 0;

    msleep(100); // Wait 100 milliseconds

    // Acquire lock2, can be interrupted if the thread is asked to stop
    if (mutex_lock_interruptible(&lock2)) {
        mutex_unlock(&lock1);
        return 0;
    }

    mutex_unlock(&lock1);
    mutex_unlock(&lock2);
    return 0;
}

static int thread_B(void *data) {
    // Acquire lock2, can be interrupted if the thread is asked to stop
    if (mutex_lock_interruptible(&lock2))
        return 0;

    msleep(100); // Wait 100 milliseconds
    
    // Acquire lock1, can be interrupted if the thread is asked to stop
    if (mutex_lock_interruptible(&lock1)) {
        mutex_unlock(&lock2);
        return 0;
    }

    mutex_unlock(&lock2);
    mutex_unlock(&lock1);
    return 0;
}

static int my_init(void)
{
    mutex_init(&lock1);
    mutex_init(&lock2);

    threadA = kthread_run(thread_A, NULL, "Thread A");
    threadB = kthread_run(thread_B, NULL, "Thread B");
    return 0;
}

static void my_exit(void)
{
    kthread_stop(threadA);
    kthread_stop(threadB);
    printk(KERN_INFO "Module Removed\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");