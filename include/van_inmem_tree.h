/*
 * Copyright (c) 2026 VanDB Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _VAN_INMEM_TREE_H
#define _VAN_INMEM_TREE_H

#if defined(__cplusplus)
extern "C" {
#endif

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
	size_t	base_level;
#define IM_DEF_SPLIT_POINT	3
	size_t	split_point;
	VAN_DATUM_CMPFUNC	cmp_func;
};

struct _inmem_stat {
	atomic_size_t		n_nodes;
	atomic_size_t		n_leaves;
	atomic_size_t		n_internals;
};

#define DEF_KEY_ULEN 4096
#define DEF_VAL_ULEN 4096

#define IM_MAX_LEVEL 255
/*
 * The default threshold is 100MB, should be set larger
 * when there is more memory and just a few tables.
 */
#define IM_DEF_THRESHOLD	(100 * 1024 * 1024)
struct _inmem_tree {
	TAILQ_ENTRY(_inmem_tree)	links;
	VAN_TABLE	*table;
	INMEM_CONFIG	*config;
	INMEM_BINTERNAL	*root;
	INMEM_BLEAF	*first;
	INMEM_BLEAF	*last;
	INMEM_STAT	stat;

	/*
	 * As the size and records are operated nearly at
	 * the same time, we use the same mutex currently.
	 * if size >= threshold, we need to frozen the tree.
	 *
	 * Notice size and records are both protected by size_mutex.
	 */
	size_t		size; /* Total size, not very exact, used for frozen check */
	size_t		records; /*  Record Count. */
	pthread_mutex_t	size_mutex;
	size_t		threshold;

	size_t		rootgen;
	size_t		nlevel;
	size_t		maxlevel;

	/*
	 * The life cycle of a in-memory tree:
	 * active -> frozen -> dumped ->destroyed.
	 */
/*	ticks_t		created_ticks; */
	ticks_t		frozen_ticks; // actually not used.
	ticks_t		dumped_ticks; // actually not used.
/*	ticks_t		freed_ticks; */
/* #define INMEM_TREE_ACTIVE	0x01 */
#define INMEM_TREE_FROZEN	0x02
#define INMEM_TREE_DUMPED	0x04
/* #define INMEM_TREE_FREED	0x08 */
	flags_t		flags;

	atomic_size_t		refcount;

	pthread_mutex_t mtx_nodelist;
	size_t		nodes_cnt;
	TAILQ_HEAD(_nodelist, _inmem_bnode) nodelist;

	/* For key/data smaller equal to or small than the default. */
	VAN_DATUM	*defretkey;
	VAN_DATUM	*defretdata;

	/* For key/data bigger than the default. */
	VAN_DATUM *reakey;
	VAN_DATUM *readata;

	/* function handles */
	int (*get)(INMEM_TREE *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*getraw)(INMEM_TREE *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*put)(INMEM_TREE *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*del)(INMEM_TREE *, VAN_DATUM *, flags_t);
	void (*statprint)(INMEM_TREE *imtree, FILE *f, const char *prefix);
};


static inline void _inmemtree_inc_nodes_cnt(INMEM_TREE *imtree) {
    if (imtree) {
        atomic_fetch_add(&imtree->stat.n_nodes, 1);
    }
}

static inline void _inmemtree_inc_leaves_cnt(INMEM_TREE *imtree) {
    if (imtree) {
        atomic_fetch_add(&imtree->stat.n_leaves, 1);
    }
}

static inline void _inmemtree_inc_internals_cnt(INMEM_TREE *imtree) {
    if (imtree) {
        atomic_fetch_add(&imtree->stat.n_internals, 1);
    }
}

static inline void _inmemtree_add_nodes_cnt(INMEM_TREE *imtree, size_t n) {
    if (imtree) {
        atomic_fetch_add(&imtree->stat.n_nodes, n);
    }
}

static inline void _inmemtree_add_leaves_cnt(INMEM_TREE *imtree, size_t n) {
    if (imtree) {
        atomic_fetch_add(&imtree->stat.n_leaves, n);
    }
}

static inline void _inmemtree_add_internals_cnt(INMEM_TREE *imtree, size_t n) {
    if (imtree) {
        atomic_fetch_add(&imtree->stat.n_internals, n);
    }
}


struct _inmem_treecursor {
	VAN_TABLE	*table;
	INMEM_TREE	*im_tree;
	INMEM_BNODE	*node;
	size_t		indx;

	flags_t		flags;

	ticks_t		create_ticks;

	/* For key/data smaller equal to or small than the default. */
	VAN_DATUM *defretkey;
	VAN_DATUM *defretdata;

	/* For key/data bigger than the default. */
	VAN_DATUM *reakey;
	VAN_DATUM *readata;

	/* function handles */
	int (*get)(INMEM_TREECURSOR *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*getraw)(INMEM_TREECURSOR *, VAN_DATUM *, VAN_DATUM *, flags_t);
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
	VAN_DATUM v;
};

struct _inmem_data {
	flags_t flags;
	VAN_DATUM v;
};

struct _inmem_child {
	size_t gen;
	INMEM_BNODE *pointer;
};

#ifdef __cplusplus
}
#endif

#endif
