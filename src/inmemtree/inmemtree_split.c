#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"

/*
 * Split a leaf node.
 * Should be called with the leaf->rwlock write locked.
 */
int _van_inmemtree_split_bleaf(INMEM_TREE *im_tree, INMEM_BLEAF *leaf, 
    VAN_DATUM **splitp, INMEM_BLEAF **thenewp) {
	int ret;
	size_t size, halfsize, newsize;
	VAN_DATUM *split;
	INMEM_BLEAF *thenew, *next;

	thenew = NULL;
	split = NULL;

	/* Generate the new leaf */
	ret = _van_inmemtree_alloc_bleaf(im_tree, &thenew);
	if (ret != 0)
		return (ret);

	/* Link it */
	thenew->next = leaf->next;
	thenew->prev = leaf;

	/* 
	 * Move items, but does not change the
	 * original item.
	 */
	size = leaf->size;
	halfsize = size / 2; /* split index */
	newsize = halfsize + 1;
	thenew->size = size - newsize;
	memcpy(thenew->keys, leaf->keys + newsize,
	    thenew->size * sizeof(INMEM_KEY *));
	memcpy(thenew->values, leaf->values + newsize,
	    thenew->size * sizeof(INMEM_DATA *));

	/* 
	 * Current, we just move it up,
	 * but later we should support the prefix compression
	 */
	ret = _van_datum_clone(leaf->keys[halfsize]->v, &split);
	if (ret != 0) {
		_van_free(NULL, thenew);
		return (ret);
	}

	/*
	 * Now we can change the original item.
	 * We change the gen first, since it means
	 * the node will be changed now.
	 */
	leaf->gen++;
	leaf->size = newsize;
	memset(leaf->keys + newsize, 0,
	    thenew->size * sizeof(INMEM_KEY *));
	memset(leaf->values + newsize, 0,
	    thenew->size * sizeof(INMEM_DATA *));

	/*
	 * Do we need to lock the old next node ?
	 * I don't think so, since it does not
	 * change the pointer even after its split.
	 * Notice we first link current to the new node,
	 * and then then link next to the new node,
	 * since there is a check in the cursor prev
	 * action.
	 */
	next = leaf->next;
	leaf->next = thenew;
	if (next != NULL)
		next->prev = thenew;

	 /* Change the last pointer. */
	if (leaf == im_tree->last)
		im_tree->last = thenew;

	*splitp = split;
	*thenewp = thenew;
	/* Unlock the lock on the node. */
	INMEM_RWLOCK_UNLOCK(leaf, rwlock, ret);

	return (ret);
}

/*
 * Split an internal node.
 * Should be called with the internal->rwlock write locked.
 */
int _van_inmemtree_split_binternal(INMEM_TREE *im_tree,
    INMEM_BINTERNAL *internal, VAN_DATUM **splitp,
    INMEM_BINTERNAL **thenewp) {
	int ret;
	size_t size, halfsize, newsize;
	VAN_DATUM *split;
	INMEM_BINTERNAL *thenew;

	/* Generate the new internal node. */
	ret = _van_inmemtree_alloc_binternal(im_tree, &thenew);
	if (ret != 0)
		return (ret);

	thenew->level = internal->level;
	/* 
	 * Move items, but does not change the
	 * original item.
	 */
	size = internal->size;
	halfsize = size / 2;
	newsize = halfsize + 1; /* The split index */
	thenew->size = size - newsize;
	/* The first key is always NULL */
	memcpy(thenew->keys + 1, internal->keys + newsize + 1,
	    (thenew->size - 1) * sizeof(INMEM_KEY *));
	memcpy(thenew->childs, internal->childs + newsize,
	    thenew->size * sizeof(INMEM_CHILD *));

	/* 
	 * No need to clone here, since we just move it up,
	 * not copy it up, otherwise a free is also needed.
	 * ret = _van_datum_clone(internal->keys[newsize]->v, &split);
	 */
	split = internal->keys[newsize]->v;
	internal->keys[newsize]->v = NULL;
	_van_free(NULL, internal->keys[newsize]);
	internal->keys[newsize] = NULL;

	/*
	 * Now we can change the original item.
	 */
	internal->size = newsize;
	memset(internal->keys + newsize, 0,
	    thenew->size * sizeof(INMEM_KEY *));
	memset(internal->childs + newsize, 0,
	    thenew->size * sizeof(INMEM_CHILD *));
	internal->gen++;
	*splitp = split;
	*thenewp = thenew;
	INMEM_RWLOCK_UNLOCK(internal, rwlock, ret);

	return (ret);
}

/*
 * Split an internal node.
 * Should be called with the root->rwlock write locked.
 */
int _van_inmemtree_split_rootinternal(INMEM_TREE *im_tree) {
	INMEM_BINTERNAL *newroot;
	INMEM_BINTERNAL *right;
	INMEM_KEY *key;
	VAN_DATUM *sep;
	int ret;

	VAN_ASSERT(im_tree != NULL && im_tree->root != NULL);
	VAN_ASSERT(im_tree->first != NULL && im_tree->last != NULL);
	VAN_ASSERT(im_tree->root->type == NT_IM_BINTERNAL);

	ret = _van_inmemtree_split_binternal(im_tree,
	    im_tree->root, &sep, &right);
	if (ret != 0)
		return (ret);

	/* The new root node */
	ret = _van_inmemtree_alloc_binternal(im_tree, &newroot);
	if (ret != 0) {
		/* 
		 * TODO:
		 * What should we do here to clear
		 * the splitted node then ?
		 */
		return (ret);
	}

	ret = _van_inmem_initialize_newroot(newroot,
	    (INMEM_BNODE *)im_tree->root, (INMEM_BNODE *)right, sep);
	if (ret != 0) {
		/*
		 * TODO:
		 * What should we do here to clear
		 * the splitted node then ?
		 */
		return (ret);
	}
	im_tree->maxlevel++;
	im_tree->rootgen = newroot->gen;
	im_tree->root = newroot;

	return (ret);
}

int _van_inmemtree_split_node(INMEM_TREE *im_tree, INMEM_BNODE *node) {
	int ret, ret1;
	INMEM_BLEAF *lleaf, *rleaf;
	INMEM_BINTERNAL *linternal, *rinternal, *parent;
	INMEM_BNODE *left, *right;
	VAN_DATUM *sep;
	INMEM_TREECURSOR *cursor;
	INMEM_CHILD *child;


	/*
	 * (void)_van_inmemtree_dump_node(im_tree, node, 0, stderr);
	 */
	/*
	 * Left is current not used.
	 * But we need to use it for stale check later.
	 */
	if ((INMEM_BNODE *)im_tree->root == node) {
		/* 
		 * The insert has been done, so just return 
		 */
		ret = _van_inmemtree_split_rootinternal(im_tree);
		return (ret);
	} else if (node->type == NT_IM_BINTERNAL) {
		linternal = (INMEM_BINTERNAL *)node;
		ret = _van_inmemtree_split_binternal(im_tree,
		    linternal, &sep, &rinternal);
		left = (INMEM_BNODE *)linternal;
		right = (INMEM_BNODE *)rinternal;
	} else if (node->type == NT_IM_BLEAF) {
		lleaf = (INMEM_BLEAF *)node;
		ret = _van_inmemtree_split_bleaf(im_tree,
		    lleaf, &sep, &rleaf);
		left = (INMEM_BNODE *)lleaf;
		right = (INMEM_BNODE *)rleaf;
	}
	if (ret != 0) {
		/* 
		 * TODO: What should we do here ?
		 */
		return (ret);	
	}
	/* 
	 * TODO: Change the flags ?
	 */
	ret = _van_inmemtree_cursor_init(im_tree, &cursor, 0);
	if (ret != 0) {
		return (ret);
	}
	ret = _van_inmemtree_search(cursor, sep, left->level + 1, 
	    VAN_SET | VAN_CURSOR_WRITE);
	if (ret != VAN_NOTFOUND) { /* We should not find the value */
		if (ret == 0)
			ret = VAN_BAD;
		return (ret);
	}
	VAN_ASSERT(cursor->indx > 0);
	parent = (INMEM_BINTERNAL *)cursor->node;
	/* Now we should add the new item to the parent */
	ret = _van_inmemtree_cursor_insert_internal(cursor, sep,
	    right);
	/* Now change the gen of splitted node, so search can work. */
	child = parent->childs[cursor->indx - 1];
	child->gen++;

	ret1 = _van_inmemtree_cursor_cleanup(cursor);
	SET_RET(ret1, ret);

	return (ret);
}
