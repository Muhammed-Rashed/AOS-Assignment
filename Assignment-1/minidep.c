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
// idea: using our graph we can see which nodes have cycles
// We use DFS and get all nodes
// input: our depdancy nodes
// output: which group have a cycle

// if our nodes output were like this
/*
Adjacency list representation:
0: 4 1
1: 4 3 2 0
2: 3 1
3: 4 2 1
4: 3 1 0
*/

// then we know that
// 0 -> 4 and 4 -> 0 is a cycle and as such could cause a deadlock so we show
// 0 -> 4 -> 0 cycle detected
// so genrally our output will be: (start node -> node 1 -> node 2 -> .... -> node n -> start node) cycle detected deadlock possiable

// This is an Algo that only detects if there is a cycle not tell us how many cycles are there
// TODO: make it check for all cycles

bool isCyclicUtil(vector<vector<int>> &adj, int u, vector<bool> &visited, vector<bool> &recStack)
{

	// node is already in recursion stack cycle found
	if (recStack[u])
		return true;

	// already processed no need to visit again
	if (visited[u])
		return false;

	visited[u] = true;
	recStack[u] = true;

	// Recur for all adjacent nodes
	for (int v : adj[u])
	{
		if (isCyclicUtil(adj, v, visited, recStack))
			return true;
	}
	// remove from recursion stack before backtracking
	recStack[u] = false;
	return false;
}

// Function to detect cycle in a directed graph
bool isCyclic(vector<vector<int>> &adj)
{
	int V = adj.size();
	vector<bool> visited(V, false);
	vector<bool> recStack(V, false);

	// Run DFS from every unvisited node
	for (int i = 0; i < V; i++)
	{
		if (!visited[i] && isCyclicUtil(adj, i, visited, recStack))
			return true;
	}
	return false;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mini Lockdep Implementation");
MODULE_AUTHOR("Mohamed");
