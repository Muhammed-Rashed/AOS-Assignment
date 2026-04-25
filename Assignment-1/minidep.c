#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/delay.h>

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

static void mylock_acquire(struct mutex *lock)
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
	int dest;
	struct AdjListNode *next;
};

struct Graph
{
	int V;
	struct AdjListNode **array;
};

static struct AdjListNode *newAdjListNode(int dest)
{
	struct AdjListNode *node;

	node = kmalloc(sizeof(struct AdjListNode), GFP_KERNEL);
	if (!node)
		return NULL;

	node->dest = dest;
	node->next = NULL;
	return node;
}

static struct Graph *createGraph(int V)
{
	struct Graph *graph;

	graph = kmalloc(sizeof(struct Graph), GFP_KERNEL);
	if (!graph)
		return NULL;

	graph->V = V;

	graph->array = kcalloc(V, sizeof(struct AdjListNode *), GFP_KERNEL);
	if (!graph->array)
	{
		kfree(graph);
		return NULL;
	}

	return graph;
}

static void addEdge(struct Graph *graph, int src, int dest)
{
	struct AdjListNode *node;

	if (!graph || src >= graph->V || dest >= graph->V)
		return;

	node = newAdjListNode(dest);
	if (!node)
		return;

	node->next = graph->array[src];
	graph->array[src] = node;
}

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

static void freeGraph(struct Graph *graph)
{
	int i;
	struct AdjListNode *cur, *tmp;

	if (!graph)
		return;

	for (i = 0; i < graph->V; i++)
	{
		cur = graph->array[i];
		while (cur)
		{
			tmp = cur;
			cur = cur->next;
			kfree(tmp);
		}
	}

	kfree(graph->array);
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

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mini Lockdep Implementation");
MODULE_AUTHOR("Mohamed");
