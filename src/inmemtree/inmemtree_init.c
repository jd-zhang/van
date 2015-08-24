#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"

/* 
 * We allocate the node and the pointer arrays together,
 * So when free, we just need to free the node. No need
 * to free the two pointer arrays.
 */
int _van_inmemtree_alloc_bnode(INMEM_TREE *im_tree, 
    INMEM_BNODE **nodep) {
	int ret, ret1, lock_init;
	size_t capacity, node_sz;
	INMEM_BNODE *node;

	VAN_ASSERT(nodep != NULL);
	capacity = im_tree->config->node_capacity;
	node = NULL;
	lock_init = 0;

	node_sz = sizeof(INMEM_BNODE) + capacity * sizeof(void *) +
	    capacity * sizeof(void *);
	ret = _van_calloc(NULL, 1, node_sz, &node);
	if (ret != 0)
		goto err;

	/* Use the default attribute currently */
	INMEM_RWLOCK_INIT(node, rwlock, ret);
	if (ret != 0)
		goto err;
	else
		lock_init = 1;

	node->capacity = capacity;
	node->gen = 0;
	node->size = 0;
	/* The addr just after node */
	node->ptrarray1 = (void **)(node + 1);
	node->ptrarray2 = node->ptrarray1 + capacity;
	INMEM_MUTEX_LOCK(im_tree, mtx_nodelist, ret);
	im_tree->nodes_cnt++;
	TAILQ_INSERT_TAIL(&im_tree->nodelist, node, links);
	INMEM_MUTEX_UNLOCK(im_tree, mtx_nodelist, ret);

	*nodep = node;

	if (0) {
err:
		if (lock_init)
			INMEM_RWLOCK_DESTROY(node, rwlock, ret1);
		if (node != NULL)
			_van_free(NULL, node);
	}

	return (ret);
}

int _van_inmemtree_alloc_binternal(INMEM_TREE *im_tree, 
    INMEM_BINTERNAL **nodep) {
	int ret;
	INMEM_BNODE *node;
	
	VAN_ASSERT(nodep != NULL);

	ret = _van_inmemtree_alloc_bnode(im_tree, &node);
	if (ret != 0)
		return (ret);

	node->type = NT_IM_BINTERNAL;
	*nodep = (INMEM_BINTERNAL *)node;

	return (ret);
}

int _van_inmemtree_alloc_bleaf(INMEM_TREE *im_tree, 
    INMEM_BLEAF **nodep) {
	int ret;
	INMEM_BNODE *node;

	VAN_ASSERT(nodep != NULL);

	ret = _van_inmemtree_alloc_bnode(im_tree, &node);
	if (ret != 0)
		return (ret);

	node->type = NT_IM_BLEAF;
	node->level = im_tree->config->base_level;
	*nodep = (INMEM_BLEAF *)node;

	return (ret);
}
