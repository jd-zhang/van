#ifndef _VAN_INMEM_TREE_H
#define _VAN_INMEM_TREE_H

#define NT_IM_INVALID	0	/* Invalid Node Type */
#define NT_IM_BINTERNAL	1	/* Btree Internal Node */
#define NT_IM_BLEAF	2	/* Btree Leaf Node. */

#define RET_KEY 	1	/* Return key from a cursor->get */
#define RET_VALUE	2	/* Return value from a cursor->get */

/*
typedef int (*INMEM_KEY_CMPFUNC)(const INMEM_KEY *k1, const INMEM_KEY *k2);
*/
typedef int (*INMEM_TRAVERSE_FUNC)(void *firstarg, const INMEM_KEY *key, 
    const INMEM_DATA *data);

#define IM_MAX_NENTRIES 32 /* 2K entries per node most. */
#define IM_BASE_LEVEL	1
struct _inmem_config {
	size_t	node_capacity;
	int	base_level;
#define IM_DEF_SPLIT_POINT	3
	size_t	split_point;
	VAN_DATUM_CMPFUNC	cmp_func;
};

struct _inmem_stat {
	size_t		n_nodes;
	size_t		n_leaves;
	size_t		n_internals;
};

#define DEF_KEY_ULEN 4096
#define DEF_VAL_ULEN 4096

#define IM_MAX_LEVEL 255
struct _inmem_tree {
	VAN_TABLE	*table;
	INMEM_CONFIG	*config;
	INMEM_BINTERNAL	*root;
	INMEM_BLEAF	*first;
	INMEM_BLEAF	*last;
	INMEM_STAT	stat;
	size_t		size; /* What should this mean ? */
	size_t		rootgen;
	int		nlevel;
	int		maxlevel;

	size_t		nodes_cnt;

	/* 
	 * The life cycle of a in-memory tree:
	 * active -> frozen -> dumped ->destroyed.
	 */
	ticks_t		created_ticks;
	ticks_t		frozen_ticks;
	ticks_t		dumped_ticks;
	ticks_t		freed_ticks;
#define INMEM_TREE_ACTIVE	0x01
#define INMEM_TREE_FROZEN	0x02
#define INMEM_TREE_DUMPED	0x04
#define INMEM_TREE_FREED	0x08
	flags_t		flags;
	int		refcount;

	/* Protect the ref, and flags. */
	pthread_mutex_t	tree_mutex;


	int		nlmtx_allocated;
	pthread_mutex_t mtx_nodelist;
	TAILQ_HEAD(_nodelist, _inmem_bnode) nodelist;

	/* For key/data smaller equal to or small than the default. */
	VAN_DATUM	*defretkey;
	VAN_DATUM	*defretdata;

	/* For key/data bigger than the default. */
	VAN_DATUM *reakey;
	VAN_DATUM *readata;

	/* function handles */
	int (*get)(INMEM_TREE *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*put)(INMEM_TREE *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*del)(INMEM_TREE *, VAN_DATUM *, flags_t);
};



struct _inmem_treecursor {
	VAN_TABLE	*table;
	INMEM_TREE	*im_tree;
	INMEM_BNODE	*node;
	size_t		indx;

	flags_t		flags;
	
	ticks_t		create_ticks;

	INMEM_TREECURSOR_CONFIG *config;

	/* For key/data smaller equal to or small than the default. */
	VAN_DATUM *defretkey; 
	VAN_DATUM *defretdata;

	/* For key/data bigger than the default. */
	VAN_DATUM *reakey;
	VAN_DATUM *readata;

	/* function handles */
	int (*get)(INMEM_TREECURSOR *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*put)(INMEM_TREECURSOR *, VAN_DATUM *, flags_t);
	int (*del)(INMEM_TREECURSOR *, flags_t);
};

#define INMEM_NT(p) (*((int *)p))
/* 
 * The _inmem_bnode, _inmem_binternal, _inmem_bleaf
 * should have the same memory layout, for corresponding
 * members, so that we alloc the space using _inmem_bnode.
 * The _inmem_binternal, _inmem_bleaf is used to easy
 * the programing.
 *
 * The same memory layout depends on the fact that:
 * A pointer always has the same size, no matter what
 * it points to.
 */
struct _inmem_bnode {
	TAILQ_ENTRY(_inmem_bnode) links;
	int type;
	int level;
	size_t capacity;
	size_t size;
	size_t gen;
	pthread_rwlock_t rwlock;
	void *left;
	void *right;
	void **ptrarray1;
	void **ptrarray2;
};

struct _inmem_rwlock_stat {
	size_t ninit;
	size_t ndestroy;
	size_t nrdlocks;
	size_t nwrlocks;
	size_t nunlocks;
	size_t ntryrdlocks;
	size_t ntrywrlocks;
};

struct _inmem_mutex_stat {
	size_t ninit;
	size_t ndestroy;
	size_t nlocks;
	size_t ntrylocks;
	size_t nunlocks;
};

extern INMEM_RWLOCK_STAT inmem_rwlock_stats;

#define INMEM_RWLOCK_INIT(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_init(&_node->rwlock, NULL); \
	inmem_rwlock_stats.ninit++; \
} while(0)

#define INMEM_RWLOCK_DESTROY(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_destroy(&_node->rwlock); \
	inmem_rwlock_stats.ndestroy++; \
} while(0)

#define INMEM_RWLOCK_RDLOCK(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_rdlock(&_node->rwlock); \
	inmem_rwlock_stats.nrdlocks++; \
} while(0)

#define INMEM_RWLOCK_TRYRDLOCK(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_tryrdlock(&_node->rwlock); \
	inmem_rwlock_stats.ntryrdlocks++; \
} while(0)

#define INMEM_RWLOCK_WRLOCK(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_wrlock(&_node->rwlock); \
	inmem_rwlock_stats.nwrlocks++; \
} while(0)

#define INMEM_RWLOCK_TRYWRLOCK(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_trywrlock(&_node->rwlock); \
	inmem_rwlock_stats.ntrywrlocks++; \
} while(0)

#define INMEM_RWLOCK_UNLOCK(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_unlock(&_node->rwlock); \
	inmem_rwlock_stats.nunlocks++; \
} while(0)

extern INMEM_MUTEX_STAT inmem_mutex_stats;

#define INMEM_MUTEX_INIT(_node, mutex, _ret) do { \
	_ret = pthread_mutex_init(&_node->mutex, NULL); \
	inmem_mutex_stats.ninit++; \
} while(0)

#define INMEM_MUTEX_DESTROY(_node, mutex, _ret) do { \
	_ret = pthread_mutex_destroy(&_node->mutex); \
	inmem_mutex_stats.ndestroy++; \
} while(0)

#define INMEM_MUTEX_LOCK(_node, mutex, _ret) do { \
	_ret = pthread_mutex_lock(&_node->mutex); \
	inmem_mutex_stats.nlocks++; \
} while(0)

#define INMEM_MUTEX_TRYLOCK(_node, mutex, _ret) do { \
	_ret = pthread_mutex_trylock(&_node->mutex); \
	inmem_mutex_stats.ntrylocks++; \
} while(0)

#define INMEM_MUTEX_UNLOCK(_node, mutex, _ret) do { \
	_ret = pthread_mutex_unlock(&_node->mutex); \
	inmem_mutex_stats.nunlocks++; \
} while(0)

/* 
 * For internal, we define the left child is 
 * with keys <= current key, while right child
 * is with keys > current key.
 * This affects the split actually.
 * In leaf split, the split-point belongs to left child.
 */
struct _inmem_binternal {
	TAILQ_ENTRY(_inmem_bnode) links;
	int type;
	int level;
	size_t capacity;
	size_t size;	/* size means the number of childs here. */
	size_t gen;
	pthread_rwlock_t rwlock;
	void *unused1;
	void *unused2;
	INMEM_KEY **keys;
	INMEM_CHILD **childs;
};

struct _inmem_bleaf {
	TAILQ_ENTRY(_inmem_bnode) links;
	int type;
	int level;
	size_t capacity;
	size_t size;
	size_t gen;
	pthread_rwlock_t rwlock;
	INMEM_BLEAF *prev;
	INMEM_BLEAF *next;
	INMEM_KEY **keys;
	INMEM_DATA **values;
};

#define IM_DEL	0x01
struct _inmem_key {
	flags_t flags;
	VAN_DATUM *v;
};

struct _inmem_data {
	flags_t flags;
	VAN_DATUM *v;
};

struct _inmem_child {
	size_t gen;
	INMEM_BNODE *pointer;
};

#endif
