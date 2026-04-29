#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/sched.h>

struct Graph;
static void addEdge(struct Graph *graph, int src, int dest);
static void printGraph(struct Graph *graph);
static void find_cycles(struct Graph *graph);
static struct Graph *global_graph;

static struct completion threadA_done;
static struct completion threadB_done;

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
        struct lock_info *info = &lock_table[lock_count];
        info->lock = lock;
        info->owner = -1;
        lock_count++;
        return info;
    }

    // we are full and cant take anymore locks
    return NULL;
}

static bool force_stop = false;
static struct mutex *last_lock = NULL;

static void mylock_acquire(struct mutex *lock)
{
    pid_t pid = current->pid;
    struct lock_info *info;

    printk(KERN_INFO "[MiniLockdep] Thread %d acquiring lock\n", pid);

    // Build the dependency edge BEFORE trying to acquire
    if (last_lock && last_lock != lock)
    {
        struct lock_info *src_info = get_lock_info(last_lock);
        struct lock_info *dst_info = get_lock_info(lock);

        if (src_info && dst_info)
        {
            int src = src_info - lock_table;
            int dst = dst_info - lock_table;
            addEdge(global_graph, src, dst);
            printGraph(global_graph);
            find_cycles(global_graph);
        }
    }

    // I added this cuz when i ran rmmod it hung indefinatly
    while (!mutex_trylock(lock))
    {
        if (force_stop || kthread_should_stop())
        {
            printk(KERN_INFO "[MiniLockdep] Thread %d: exit signal received, aborting lock acquire\n", pid);
            return;
        }
        msleep(100);
    }

    last_lock = lock;

    info = get_lock_info(lock);
    if (!info)
        return;

    info->owner = pid;
    printk(KERN_INFO "[MiniLockdep] Thread %d acquired lock\n", pid);
}

static void mylock_release(struct mutex *lock)
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
// In order to show the dependaces correctly i will use a directed graph
// to do this i decided to use a linked list cuz it would be easier when showing cycles

struct AdjListNode
{
    // the destination node
    int dest;
    // next neighbor in the linked list
    struct AdjListNode *next;
};

struct Graph
{
    // total number of vertices
    int V;
    // array of linked lists one per vertex
    struct AdjListNode **array;
};

// We need a way to allocate and return a new edge node pointing to our destination
static struct AdjListNode *newAdjListNode(int dest)
{
    struct AdjListNode *node;

    node = kmalloc(sizeof(struct AdjListNode), GFP_KERNEL);
    if (!node)
        return NULL;

    // set the destination vertex
    node->dest = dest;
    // no next neighbor .... YET
    node->next = NULL;
    return node;
}

// Allocates and returns a new graph with V vertices and no edges
static struct Graph *createGraph(int V)
{
    struct Graph *graph;

    graph = kmalloc(sizeof(struct Graph), GFP_KERNEL);
    if (!graph)
        return NULL;

    graph->V = V;

    // All adjacency list heads start as NULL AKA no edges yet
    graph->array = kcalloc(V, sizeof(struct AdjListNode *), GFP_KERNEL);
    if (!graph->array)
    {
        kfree(graph);
        return NULL;
    }

    return graph;
}

// Adds a directed edge from vertex "src" to vertex "dest"
static void addEdge(struct Graph *graph, int src, int dest)
{
    struct AdjListNode *node;

    //  We must validate inputs before doing anything
    if (!graph || src >= graph->V || dest >= graph->V)
        return;

    // Create a new edge node
    node = newAdjListNode(dest);
    if (!node)
        return;

    node->next = graph->array[src];
    graph->array[src] = node;
}

// Print the adjacency list of the graph to the kernel log
static void printGraph(struct Graph *graph)
{
    int i;
    struct AdjListNode *cur;

    if (!graph)
        return;

    for (i = 0; i < graph->V; i++)
    {
        printk(KERN_INFO "[MiniLockdep] %d:", i);

        cur = graph->array[i];
        while (cur)
        {
            printk(KERN_CONT " -> %d", cur->dest);
            cur = cur->next;
        }

        printk(KERN_CONT "\n");
    }
}

// free up everything
static void freeGraph(struct Graph *graph)
{
    int i;
    struct AdjListNode *cur, *tmp;

    if (!graph)
        return;

    // free everynode in every adj list
    for (i = 0; i < graph->V; i++)
    {
        cur = graph->array[i];
        while (cur)
        {
            // save current node
            tmp = cur;
            // advance without freeing
            cur = cur->next;
            // BE FREE
            kfree(tmp);
        }
    }

    // Free the array of list heads
    kfree(graph->array);
    // Free the graph itself
    kfree(graph);
}

// ------- Part C -------
// Detects cycles in the graph (i.e., deadlocks)

// We use DFS traversal to detect cycles in directed graph

static void print_cycle(int *stack, int stack_size, int start)
{
    int i;
    printk(KERN_ALERT "[MiniLockdep] Cycle detected: ");

    // Print from where cycle starts
    for (i = start; i < stack_size; i++)
    {
        printk(KERN_CONT "%d -> ", stack[i]);
    }

    // close the loop
    printk(KERN_CONT "%d\n", stack[start]);
}

static void dfs_cycles(struct Graph *graph, int u, bool *visited, bool *recStack, int *stack, int *stack_index)
{
    struct AdjListNode *cur;
    int i;

    // Has the nodes that are already explored
    visited[u] = true;
    // Tracks the nodes in the current recursion path
    recStack[u] = true;

    // push to stack
    // This stack will store all our DFS path so we can print the cycles
    stack[*stack_index] = u;
    (*stack_index)++;

    cur = graph->array[u];
    while (cur)
    {
        int v = cur->dest;

        if (!visited[v])
        {
            dfs_cycles(graph, v, visited, recStack, stack, stack_index);
        }
        else if (recStack[v])
        {
            // if cycle found find where v appears in stack
            for (i = 0; i < *stack_index; i++)
            {
                if (stack[i] == v)
                {
                    print_cycle(stack, *stack_index, i);
                    break;
                }
            }
        }

        cur = cur->next;
    }

    // pop from stack
    (*stack_index)--;
    recStack[u] = false;
}

static void find_cycles(struct Graph *graph)
{
    bool *visited;
    bool *recStack;
    int *stack;
    int stack_index = 0;
    int i;

    if (!graph)
        return;

    visited = kcalloc(graph->V, sizeof(bool), GFP_KERNEL);
    recStack = kcalloc(graph->V, sizeof(bool), GFP_KERNEL);
    stack = kcalloc(graph->V, sizeof(int), GFP_KERNEL);

    if (!visited || !recStack || !stack)
    {
        kfree(visited);
        kfree(recStack);
        kfree(stack);
        return;
    }

    for (i = 0; i < graph->V; i++)
    {
        if (!visited[i])
        {
            dfs_cycles(graph, i, visited, recStack, stack, &stack_index);
        }
    }

    kfree(visited);
    kfree(recStack);
    kfree(stack);
}
// ------- Part 1 - deadlock test -------

struct mutex lock1;
struct mutex lock2;

struct task_struct *threadA, *threadB;

static int thread_A(void *data)
{
    mylock_acquire(&lock1);
    msleep(1000);

    mylock_acquire(&lock2);

    mylock_release(&lock2);
    mylock_release(&lock1);
    complete(&threadA_done);
    return 0;
}

static int thread_B(void *data)
{
    mylock_acquire(&lock2);
    msleep(1000);

    mylock_acquire(&lock1);

    mylock_release(&lock1);
    mylock_release(&lock2);
    complete(&threadB_done);
    return 0;
}

// ------- intilization and exit -------

static int my_init(void)
{
    mutex_init(&lock1);
    mutex_init(&lock2);

    init_completion(&threadA_done);
    init_completion(&threadB_done);

    global_graph = createGraph(MAX_LOCKS);

    threadA = kthread_run(thread_A, NULL, "Thread_A");
    threadB = kthread_run(thread_B, NULL, "Thread_B");

    printk(KERN_INFO "MiniLockdep Loaded\n");
    return 0;
}

static void my_exit(void)
{
    force_stop = true;

    wait_for_completion_timeout(&threadA_done, msecs_to_jiffies(2000));
    wait_for_completion_timeout(&threadB_done, msecs_to_jiffies(2000));

    freeGraph(global_graph);
    printk(KERN_INFO "MiniLockdep Unloaded\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mohamed");
MODULE_DESCRIPTION("Part 1 + Part 2");