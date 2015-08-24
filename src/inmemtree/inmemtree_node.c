#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"

int _van_inmemtree_gen_key(VAN_DATUM *v, INMEM_KEY **keyp) {
	int ret;
	INMEM_KEY *key;

	VAN_ASSERT(keyp != NULL);
	ret = _van_calloc(NULL, 1, sizeof(INMEM_KEY), &key);
	if (ret != 0)
		return (ret);
	ret = _van_datum_clone(v, &key->v);
	if (ret == 0)
		*keyp = key;
	else	/* Free the allocated resource when failure */
		(void)_van_free(NULL, key);

	return (ret);
}

int _van_inmemtree_gen_key_nocopydatum(VAN_DATUM *v, INMEM_KEY **keyp) {
	int ret;
	INMEM_KEY *key;

	VAN_ASSERT(keyp != NULL);
	ret = _van_calloc(NULL, 1, sizeof(INMEM_KEY), &key);
	if (ret != 0)
		return (ret);
	key->v = v;
	*keyp = key;

	return (ret);
}

int _van_inmemtree_free_key(INMEM_KEY *key) {
	int ret;

	ret = 0;
	if (key == NULL)
		return (ret);

	if (key->v != NULL) {
		_van_datum_free(key->v);
		key->v = NULL;
	}
	_van_free(NULL, key);
	
	return (ret);
}

int _van_inmemtree_gen_data(VAN_DATUM *v, INMEM_DATA **datap) {
	int ret;
	INMEM_DATA *data;

	VAN_ASSERT(datap != NULL);
	ret = _van_calloc(NULL, 1, sizeof(INMEM_DATA), &data);
	if (ret != 0)
		return (ret);
	ret = _van_datum_clone(v, &data->v);
	if (ret == 0)
		*datap = data;
	else /* Free the allocated resource when failure */
		_van_free(NULL, data);

	return (ret);
}

int _van_inmemtree_free_data(INMEM_DATA *data) {
	int ret;

	ret = 0;
	if (data == NULL)
		return (ret);

	if (data->v != NULL) {
		_van_datum_free(data->v);
		data->v = NULL;
	}
	_van_free(NULL, data);

	return (ret);
}

/* Should not be called outside */
int _van_inmemtree_free_child(INMEM_TREE *tree,
    INMEM_CHILD *child, int recursive, int lock) {
	int ret;

	ret = 0;
	if (child == NULL)
		return (ret);

	if (child->pointer != NULL) {
		_van_inmemtree_free_node(tree, 
		    child->pointer, recursive, lock);
		child->pointer = NULL;
	}
	_van_free(NULL, child);

	return (ret);
}

/* Should not be called outside */
int _van_inmemtree_free_leaf(INMEM_TREE *tree, INMEM_BLEAF *leaf) {
	int ret;
	size_t i;

	ret = 0;
	if (leaf == NULL)
		return (ret);

	for (i = 0; i < leaf->size; i++) {
		(void)_van_inmemtree_free_key(leaf->keys[i]);
		leaf->keys[i] = NULL;
		(void)_van_inmemtree_free_data(leaf->values[i]);
		leaf->values[i] = NULL;
	}
	/*
	 * Both keys and values are after the node itself
	 */
	/*
	_van_free(NULL, leaf->keys);
	_van_free(NULL, leaf->values);
	*/
	leaf->keys = NULL;
	leaf->values = NULL;
	leaf->size = 0;
	_van_free(NULL, leaf);

	return (ret);
}

/* Should not be called outside */
int _van_inmemtree_free_internal(INMEM_TREE *tree,
    INMEM_BINTERNAL *internal, int recursive, int lock) {
	int ret;
	size_t i;

	ret = 0;
	if (internal == NULL)
		return (ret);

	for (i = 0; i < internal->size; i++) {
		(void)_van_inmemtree_free_key(internal->keys[i]);
		internal->keys[i] = NULL;
		if (recursive)
			(void)_van_inmemtree_free_child(
			    tree, internal->childs[i], recursive, lock);
		else
			_van_free(NULL, internal->childs[i]);
		internal->childs[i] = NULL;
	}
	/* 
	 * Both keys and childs are after the node itself.
	 */
	/*
	_van_free(NULL, internal->keys);
	_van_free(NULL, internal->childs);
	*/
	internal->keys = NULL;
	internal->childs = NULL;
	internal->size = 0;
	_van_free(NULL, internal);

	return (ret);
}

int _van_inmemtree_free_node(INMEM_TREE *tree, INMEM_BNODE *node, 
    int recursive, int lock) {
	int ret;

	ret = 0;
	if (node == NULL)
		return (ret);

	/* Destroy the rwlock member. */
	INMEM_RWLOCK_DESTROY(node, rwlock, ret);
	if (lock)
		INMEM_MUTEX_LOCK(tree, mtx_nodelist, ret);
	TAILQ_REMOVE(&tree->nodelist, node, links);
	tree->nodes_cnt--;
	if (lock)
		INMEM_MUTEX_UNLOCK(tree, mtx_nodelist, ret);
	if (node->type == NT_IM_BINTERNAL)
		ret = _van_inmemtree_free_internal(tree,
		    (INMEM_BINTERNAL *)node, recursive, lock);
	else if (node->type == NT_IM_BLEAF)
		ret = _van_inmemtree_free_leaf(tree,
		    (INMEM_BLEAF *)node);
	else {
		ret = _van_error_path("Invalid Node type!");
	}
	
	return (ret);
}

/* 
 * Copy a node, non-deeep copy.
 * For pointers array, we copy the items in the array,
 * instead of the pointers array itself.
 * This is because when allocate a node(internal or leaf),
 * the array is allocated, so overwriting it will lose the
 * memory.
 */
int _van_inmemtree_copy_node(INMEM_TREE *tree, INMEM_BNODE *src,
    INMEM_BNODE *dst) {
	size_t i;
	int ret;

	/* Must have the same capacity */
	VAN_ASSERT(src->capacity = dst->capacity);
	VAN_ASSERT(src->size <= src->capacity);
	ret = 0;

	/* 
	 * TODO:
	 * What about gen, rwlock and capacity ?
	 * Also, the unused1 and unused2 are not considered.
	 */
	dst->type = src->type;
	dst->level = src->level;
	dst->size = src->size;
	dst->left = src->left;
	dst->right  = src->right;
	for (i = 0; i < src->size; i++) {
		dst->ptrarray1[i] = src->ptrarray1[i];
		dst->ptrarray2[i] = src->ptrarray2[i];
	}

	return (ret);
}

/* 
 * Used for root split, to intialize a new root.
 * The gen should be set in the caller.
 */
int _van_inmem_initialize_newroot(INMEM_BINTERNAL *newroot, INMEM_BNODE *left, 
    INMEM_BNODE *right, VAN_DATUM *sep) {
	INMEM_KEY *key;
	INMEM_CHILD *child1, *child2;
	size_t i;
	int ret;

	ret = 0;
	key = NULL;
	/*
	 * Initialize.
	 */
	newroot->level = left->level + 1;
	for (i = 0; i < newroot->capacity; i++) {
		newroot->keys[i] = NULL;
		newroot->childs[i] = NULL;
	}

	/* 
	 * Now setup the node.
	 */
	/* First Child */
	ret = _van_calloc(NULL, 1, sizeof(INMEM_CHILD), &child1);
	if (ret != 0)
		return (ret);
	child1->gen = left->gen;
	child1->pointer = left;
	newroot->childs[0] = child1;
	/* Second Child */
	ret = _van_inmemtree_gen_key_nocopydatum(sep, &key);
	if (ret != 0)
		return (ret);
	newroot->keys[1] = key;
	ret = _van_calloc(NULL, 1, sizeof(INMEM_CHILD), &child2);
	if (ret != 0) {
		/*
		 * TODO:
		 * What to do here exactly ?
		 */
		return (ret);
	}
	child2->gen = right->gen;
	child2->pointer = right;
	newroot->childs[1] = child2;
	newroot->size = 2;
	
	return (ret);
}

int _van_inmemtree_node_rwlock_action(INMEM_BNODE *node, int action) {
	int ret;

	VAN_ASSERT(node != NULL);
	/* Only lock/unlock actions are valid. */
	switch (action) {
		case RWLOCK_RDLOCK: 
			INMEM_RWLOCK_RDLOCK(node, rwlock, ret);
			break;
		case RWLOCK_TRYRDLOCK: 
			INMEM_RWLOCK_TRYRDLOCK(node, rwlock, ret);
			break;
		case RWLOCK_WRLOCK: 
			INMEM_RWLOCK_WRLOCK(node, rwlock, ret);
			break;
		case RWLOCK_TRYWRLOCK: 
			INMEM_RWLOCK_TRYWRLOCK(node, rwlock, ret);
			break;
		case RWLOCK_UNLOCK: 
			INMEM_RWLOCK_UNLOCK(node, rwlock, ret);
			break;
		default:
			ret = _van_error_path(
		"Invalid action to _van_inmemtree_node_rwlock_action!");
			break;
	}

	return (ret);
}
