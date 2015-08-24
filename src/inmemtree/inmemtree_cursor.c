#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"

int _van_inmemtree_cursor_init(INMEM_TREE *im_tree,
    INMEM_TREECURSOR **cursorp, flags_t flags) {
	INMEM_TREECURSOR *cursor;
	int ret;

	VAN_ASSERT(cursorp != NULL);
	*cursorp = NULL;
	cursor = NULL;

	ret = _van_calloc(NULL, 1, sizeof(*cursor), &cursor);
	if (ret != 0)
		return (ret);
	cursor->im_tree = im_tree;
	cursor->table = im_tree->table;
	cursor->node = NULL;
	cursor->indx = INVALID_CURSOR_INDX;
	cursor->flags = flags;
	cursor->get = _van_inmemtree_cursor_get;
	/* 
	 * We only support using put to cover current item.
	 */
	cursor->put = _van_inmemtree_cursor_update;
	cursor->del = _van_inmemtree_cursor_justdel;
	/* 
	 * TEMP cursor is created by invoking an 
	 * operation using the tree handle. So
	 * we use the memory in the tree handle.
	 */
	if (LF_ISSET(VAN_TEMPCURSOR)) {
		cursor->defretkey = im_tree->defretkey;
		cursor->defretdata = im_tree->defretdata;
		cursor->reakey = im_tree->reakey;
		cursor->readata = im_tree->readata;
	} else {
		ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
		    &cursor->defretkey);
		if (ret == 0) {
			cursor->defretkey->ulen = DEF_KEY_ULEN;
			ret = _van_calloc(NULL, 1, DEF_KEY_ULEN,
			    &cursor->defretkey->data);
		}
		if (ret == 0)
			ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
			    &cursor->defretdata);
		if (ret == 0) {
			cursor->defretdata->ulen = DEF_VAL_ULEN;
			ret = _van_calloc(NULL, 1, DEF_VAL_ULEN,
			    &cursor->defretdata->data);
		}
		if (ret == 0)
			ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
			    &cursor->reakey);
		if (ret == 0)
			ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
			    &cursor->readata);		
	}
	
	if (ret == 0)
		*cursorp = cursor;
	else if (cursor != NULL)
		(void)_van_inmemtree_cursor_cleanup(cursor);

	return (ret);
}

int _van_inmemtree_cursor_write_valid(INMEM_TREECURSOR *cursor) {
	int ret;

	ret = 0;
	if (!F_ISSET(cursor, VAN_WRITECURSOR))
		ret = VAN_INVALID;

	return (ret);
}

int _van_inmemtree_cursor_dup(INMEM_TREECURSOR *src,
    INMEM_TREECURSOR *dst) {
	int ret;

	ret = 0;
	/* What about the locks ? */
	dst->im_tree = src->im_tree;
	dst->table = src->table;
	dst->node = src->node;
	dst->indx = src->indx;
	dst->flags = src->flags;

	return (ret);
}

/* 
 * We allocate the cursor handle here, and it is the 
 * caller's duty to free the handle.
 */
int _van_inmemtree_cursor_allocdup(INMEM_TREECURSOR *src,
    INMEM_TREECURSOR **dstp) {
	int ret;
	INMEM_TREECURSOR *dst;

	ret = _van_calloc(NULL, 1, sizeof(*dst), &dst);
	if (ret != 0)
		return (ret);
	ret = _van_inmemtree_cursor_dup(src, dst);
	if (ret != 0) {
		_van_free(NULL, dst);
		return (ret);
	}
	*dstp = dst;

	return (ret);
}

int _van_inmemtree_cursor_cleanup(INMEM_TREECURSOR *src) {
	int ret;

	ret = 0;
	/* 
	 * We need to unlock the node.
	 */
	if (src->node != NULL)
		INMEM_RWLOCK_UNLOCK(src->node, rwlock, ret);

	ret = _van_inmemtree_cursor_cleanup_nounlock(src);

	return (ret);
}

int _van_inmemtree_cursor_cleanup_nounlock(INMEM_TREECURSOR *src) {
	int ret;

	ret = 0;

	/* 
	 * Not a temp cursor, so we need 
	 * to free its space.
	 */
	if (!F_ISSET(src, VAN_TEMPCURSOR)) {
		if (src->readata != NULL)
			_van_datum_free(src->readata);
		if (src->reakey != NULL)
			_van_datum_free(src->reakey);
		if (src->defretdata != NULL)
			_van_datum_free(src->defretdata);
		if (src->defretkey != NULL)
			_van_datum_free(src->defretkey);
	}
	_van_free(NULL, src);

	return (ret);
}

/* 
 * We should handle locks in each function.
 * Should only be called by cursor->get.
 */
int _van_inmemtree_cursor_locate(INMEM_TREECURSOR *cursor, VAN_DATUM *key,
    flags_t flags) {
	int ret, ret1;
	INMEM_TREE *im_tree;
	INMEM_CONFIG *config;
	flags_t tmpflags;

	ret = 0;
	im_tree = cursor->im_tree;
	config = im_tree->config;
	tmpflags = flags;

	switch (flags & VAN_MASKMODE) {
		case VAN_NEXT:
			if (cursor->node != NULL) {
				ret = _van_inmemtree_cursor_next(cursor);
				break;
			}
		case VAN_FIRST:
			ret = _van_inmemtree_cursor_first(cursor);
			break;
		case VAN_PREV:
			if (cursor->node != NULL) {
				ret = _van_inmemtree_cursor_prev(cursor);
				break;
			}
		case VAN_LAST:
			ret = _van_inmemtree_cursor_last(cursor);
			break;
		case VAN_SET:
		case VAN_SET_RANGE:
			if (F_ISSET(cursor, VAN_WRITECURSOR)) {
				tmpflags |= VAN_CURSOR_WRITE;
			}
			ret = _van_inmemtree_search(cursor,
			    key, config->base_level, tmpflags);
			/* Avoid mis-overwriting the values. */
			if (ret != 0) {
				/* 
				 * Also need to unlock the node.
				 * This will have some performance
				 * penalty when the next searching
				 * pair is in this node.
				 */
				if (cursor->node != NULL) {
					INMEM_RWLOCK_UNLOCK(cursor->node,
					    rwlock, ret1);
					SET_RET(ret1, ret);
				}
				cursor->node = NULL;
			}
			break;
		case VAN_CURRENT:
			ret = 0; /* do not change current cursor */
			break;
		default:
			ret = _van_error_path(
		"unknown flags passed to _van_inmemtree_cursor_locate!");
			break;
	}

	return (ret);
}

int _van_inmemtree_cursor_next(INMEM_TREECURSOR *cursor) {
	INMEM_TREE *im_tree;
	INMEM_CONFIG *config;
	int ret, ret1, found;
	INMEM_BLEAF *orig, *leaf, *cur;
	INMEM_KEY *key;
	size_t st, et, i;
	int lk_action;

	VAN_ASSERT(cursor->node != NULL);
	/* Set the lock */
	if (F_ISSET(cursor, VAN_WRITECURSOR))
		lk_action = RWLOCK_WRLOCK;
	else
		lk_action = RWLOCK_RDLOCK;
	ret = VAN_NOTFOUND;
	found = 0;
	im_tree = cursor->im_tree;
	config = im_tree->config;
	leaf = orig = (INMEM_BLEAF *)cursor->node;
	i = 0;
	
	while (leaf != NULL) {
		cur = leaf;
		st = (leaf == orig ? cursor->indx + 1 : 0);
		et = leaf->size;
		for (i = st; i < et; i++) {
			key = leaf->keys[i];
			if (F_ISSET(key, IM_DEL))
				continue;
			found = 1;
			break;
		}

		if (found)
			break;

		leaf = cur->next;
		/* Unlock the node */
		INMEM_RWLOCK_UNLOCK(cur, rwlock, ret1);
		/* TODO: error check */
		/* 
		 * The next node will not be changed,
		 * So there is no need to do back check.
		 */
		if (leaf != NULL) {
			ret1 = _van_inmemtree_node_rwlock_action(
			    (INMEM_BNODE *)leaf, lk_action);
			/* TODO: error check */
		}
	}

	/* 
	 * BUG: ? After moving out of the end, the cursor becomes
	 * unintialized.
	 */
	cursor->node = (INMEM_BNODE *)leaf;
	cursor->indx = i;
	if (found)
		ret = 0;

	return (ret);
}

int _van_inmemtree_cursor_prev(INMEM_TREECURSOR *cursor) {
	INMEM_TREE *im_tree;
	INMEM_CONFIG *config;
	INMEM_BLEAF *leaf, *orig, *cur;
	int ret, ret1, found;
	INMEM_KEY *key;
	size_t st, et, i;
	int lk_action;

	VAN_ASSERT(cursor->node != NULL);
	/* Set the lock */
	if (F_ISSET(cursor, VAN_WRITECURSOR))
		lk_action = RWLOCK_WRLOCK;
	else
		lk_action = RWLOCK_RDLOCK;
	ret = VAN_NOTFOUND;
	found = 0;
	im_tree = cursor->im_tree;
	config = im_tree->config;
	orig = leaf = (INMEM_BLEAF *)cursor->node;
	i = 1;

	while (leaf != NULL) {
		cur = leaf;
		st = (leaf == orig ? cursor->indx : leaf->size);
		et = 1;
		if (st > 0) {
			for (i = st; i >= et; i--) {
				key = leaf->keys[i-1];
				if (F_ISSET(key, IM_DEL))
					continue;
				found = 1;
				break;
			}
		}
		if (found)
			break;

		/* 
		 * Now we do the moving, we need to first unlock
		 * current page to avoid deadlock, and then we
		 * check if previous is still previous.
		 */
		INMEM_RWLOCK_UNLOCK(cur, rwlock, ret1);
recheck:
		leaf = cur->prev;
		/* TODO: check error */
		if (leaf != NULL) {
			ret1 = _van_inmemtree_node_rwlock_action(
			    (INMEM_BNODE *)leaf, lk_action);
			if (leaf->next != cur) {
				INMEM_RWLOCK_UNLOCK(leaf, rwlock, ret1);
				/* TODO: check error */
				goto recheck;
			}
		}

	}

	cursor->node = (INMEM_BNODE *)leaf;
	cursor->indx = i - 1;
	if (found)
		ret = 0;

	return (ret);
}

int _van_inmemtree_cursor_first(INMEM_TREECURSOR *cursor) {
	INMEM_TREE *im_tree;
	INMEM_CONFIG *config;
	INMEM_BLEAF *leaf;
	INMEM_KEY *key;
	int ret;
	int lk_action;

	/* TODO: Check if the cursor has page */

	/* Set the lock */
	if (F_ISSET(cursor, VAN_WRITECURSOR))
		lk_action = RWLOCK_WRLOCK;
	else
		lk_action = RWLOCK_RDLOCK;
	im_tree = cursor->im_tree;
	config = im_tree->config;
	leaf = im_tree->first;

	/* 
	 * The only possibility of size 0
	 * is that, there is only one node,
	 * and there is no data. Once split
	 * happens, no node can have size 0.
	 */
	if (leaf->size == 0)
		ret = VAN_NOTFOUND;
	else {
		/* 
		 * The first does not change, so mo need to 
		 * do re-check.
		 */
		ret = _van_inmemtree_node_rwlock_action(
		    (INMEM_BNODE *)leaf, lk_action);
		key = leaf->keys[0];
		cursor->node = (INMEM_BNODE *)leaf;
		cursor->indx = 0;
		if (!F_ISSET(key, IM_DEL))
			ret = 0;
		else
			ret = _van_inmemtree_cursor_next(cursor);
	}

	return (ret);
}

int _van_inmemtree_cursor_last(INMEM_TREECURSOR *cursor) {
	INMEM_TREE *im_tree;
	INMEM_CONFIG *config;
	INMEM_BLEAF *leaf;
	INMEM_KEY *key;
	int ret;
	size_t i;
	int lk_action;

	/* TODO: Check if the cursor has page */

	/* Set the lock */
	if (F_ISSET(cursor, VAN_WRITECURSOR))
		lk_action = RWLOCK_WRLOCK;
	else
		lk_action = RWLOCK_RDLOCK;
	ret = VAN_NOTFOUND;
	im_tree = cursor->im_tree;
	config = im_tree->config;

	/* 
	 * The only possibility of size 0
	 * is that, there is only one node,
	 * and there is no data. Once split
	 * happens, no node can have size 0.
	 */
	if (im_tree->last->size == 0) {
		ret = VAN_NOTFOUND;
	} else {
again:
		leaf = im_tree->last;
		ret = _van_inmemtree_node_rwlock_action(
		    (INMEM_BNODE *)leaf, lk_action);
		/* 
		 * Check the last node,
		 * sicne the last node can change.
		 */
		if (leaf != im_tree->last) {
			INMEM_RWLOCK_UNLOCK(leaf, rwlock, ret);
			goto again;
		}
		i = leaf->size - 1;
		key = leaf->keys[i];
		cursor->node = (INMEM_BNODE *)leaf;
		cursor->indx = i;
		if (!F_ISSET(key, IM_DEL))
			ret = 0;
		else 
			ret = _van_inmemtree_cursor_prev(cursor);
	}

	return (ret);
}

/* 
 * We should have the write lock on the node.
 */
int _van_inmemtree_cursor_insert(INMEM_TREECURSOR *cursor, 
    VAN_DATUM *key, VAN_DATUM *value) {
	INMEM_BLEAF *leaf;
	size_t indx;
	INMEM_KEY *ik;
	INMEM_DATA *id;
	int ret;

	/* 
	 * Check if the cursor is a write cursor.
	 * Although it not necessary currently, it
	 * does not harm much.
	 */
	ret = _van_inmemtree_cursor_write_valid(cursor);
	if (ret != 0)
		return (ret);

	leaf = (INMEM_BLEAF *)cursor->node;
	indx = cursor->indx;
	ik = NULL;
	id = NULL;

	/* Generate the INMEM_KEY and INMEM_DATA */
	ret = _van_inmemtree_gen_key(key, &ik);
	if (ret != 0)
		return (ret);
	ret = _van_inmemtree_gen_data(value, &id);
	if (ret != 0) {
		goto err;
	}

	/* 
	 * Whether we need to check here ?
	 * Using pre-split, if a split is needed,
	 * it should split first, so the code
	 * should not be here in that case.
	 */
	VAN_ASSERT(leaf->size < leaf->capacity);

	/* Move items after */
	if (leaf->size > 0 && indx < leaf->size) {
		memmove(&leaf->keys[indx + 1], &leaf->keys[indx],
		    (leaf->size - indx) * sizeof(INMEM_KEY *));
		memmove(&leaf->values[indx + 1], &leaf->values[indx],
		    (leaf->size - indx) * sizeof(INMEM_DATA *));
	}
	leaf->keys[indx] = ik;
	leaf->values[indx] = id;
	leaf->size += 1;

	/* 
	 * Whether we need to check split here ?
	 * This is a TODO item. But currently
	 * we assume to use pre-split. So an insert
	 * will not cause the node full.
	 */
	if (leaf->size == leaf->capacity) {
		/* TODO: Do split here ? */
	}

	if (0) { /* Free the necessary resources on error here */
err:		if (ik != NULL)
			_van_inmemtree_free_key(ik);
		if (id != NULL)
			_van_inmemtree_free_data(id);
	}

	return (ret);
}

/* 
 * We should have the write lock on the node.
 */
int _van_inmemtree_cursor_update(INMEM_TREECURSOR *cursor,
    VAN_DATUM *value, flags_t flags) {
	INMEM_BLEAF *leaf;
	size_t indx;
	INMEM_DATA *oid, *id;
	int ret;

	/* 
	 * Check if the cursor is a write cursor.
	 */
	ret = _van_inmemtree_cursor_write_valid(cursor);
	if (ret != 0)
		return (ret);

	leaf = (INMEM_BLEAF *)cursor->node;
	indx = cursor->indx;
	id = NULL;

	/* Check if the cursor is valid. "Valid" means
	 * that: 1 it points to a node, 2 it indx is within
	 * the scope.
	 */
	if (leaf == NULL || indx >= leaf->size)
		return (VAN_INVALID);

	/* 
	 * Currently, we do not use the key, but
	 * maybe we will use it later ?
	 * Since the two keys may equal to each other
	 * but they are different.
	 * key = NULL;
	 */

	/* Generate the INMEM_DATA */
	ret = _van_inmemtree_gen_data(value, &id);
	if (ret != 0)
		return (ret);

	/* Free the old */
	if (leaf->values[indx] != NULL)
		_van_inmemtree_free_data(leaf->values[indx]);
	leaf->values[indx] = id;
	/* Always clear the IM_DEL flag. */
	F_CLR(leaf->keys[indx], IM_DEL);

	if (0) { /* Free the necessary resources on error here */
err:		if (id != NULL)
			_van_inmemtree_free_data(id);
	}

	return (ret);
}

/*
 * Use when the key is not found in the tree, so we need to
 * add a tombstone item. 
 * We should have write lock on the node.
 */
int _van_inmemtree_cursor_putfordel(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, flags_t flags) {
	INMEM_BLEAF *leaf;
	size_t indx;
	INMEM_KEY *ik;
	int ret;

	/* 
	 * Check if the cursor is a write cursor.
	 * Although it not necessary currently, it
	 * does not harm much.
	 */
	ret = _van_inmemtree_cursor_write_valid(cursor);
	if (ret != 0)
		return (ret);

	leaf = (INMEM_BLEAF *)cursor->node;
	indx = cursor->indx;
	ik = NULL;

	/* Generate the INMEM_KEY and INMEM_DATA */
	ret = _van_inmemtree_gen_key(key, &ik);
	if (ret != 0)
		return (ret);
	F_SET(ik, IM_DEL);

	/* 
	 * Whether we need to check here ?
	 * Using pre-split, if a split is needed,
	 * it should split first, so the code
	 * should not be here in that case.
	 */
	VAN_ASSERT(leaf->size < leaf->capacity);

	/* Move items after */
	if (leaf->size > 0 && indx < leaf->size) {
		memmove(&leaf->keys[indx + 1], &leaf->keys[indx],
		    (leaf->size - indx) * sizeof(INMEM_KEY *));
		memmove(&leaf->values[indx + 1], &leaf->values[indx],
		    (leaf->size - indx) * sizeof(INMEM_DATA *));
	}
	leaf->keys[indx] = ik;
	leaf->values[indx] = NULL;
	leaf->size += 1;

	/* 
	 * Whether we need to check split here ?
	 * This is a TODO item. But currently
	 * we assume to use pre-split. So an insert
	 * will not cause the node full.
	 */
	if (leaf->size == leaf->capacity) {
		/* TODO: Do split here ? */
	}

	if (0) { /* Free the necessary resources on error here */
err:		if (ik != NULL)
			_van_inmemtree_free_key(ik);
	}

}

/* 
 * Used when the key is found in the tree. So we just need
 * to change the item.
 * We should have write lock on the node.
 */
int _van_inmemtree_cursor_justdel(INMEM_TREECURSOR *cursor, flags_t flags) {
	INMEM_BLEAF *leaf;
	int ret;
	size_t indx;

	/* 
	 * Check if the cursor is a write cursor.
	 */
	ret = _van_inmemtree_cursor_write_valid(cursor);
	if (ret != 0)
		return (ret);

	leaf = (INMEM_BLEAF *)cursor->node;
	indx = cursor->indx;

	/* Check if the cursor is valid. "Valid" means
	 * that: 1 it points to a node, 2 it indx is within
	 * the scope.
	 */
	if (leaf == NULL || indx >= leaf->size)
		return (VAN_INVALID);

	/* 
	 * Check if the item has been deleted.
	 * If it is, then it is good, just return.
	 */
	if (F_ISSET(leaf->keys[indx], IM_DEL))
		return (VAN_NOTFOUND);

	/* Set the delete flag and clear the data. */
	F_SET(leaf->keys[indx], IM_DEL);
	if (leaf->values[indx] != NULL) {
		_van_inmemtree_free_data(leaf->values[indx]);
		leaf->values[indx] = NULL;
	}

	return (ret);
}

/* Interface to get a key-data pair */
int _van_inmemtree_cursor_get(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, VAN_DATUM *data, flags_t flags) {
	int ret, ret1, ret2, key_set;
	INMEM_KEY *ik;
	INMEM_DATA *id;
	INMEM_BLEAF *leaf;
	size_t indx;

	ret1 = 0;
	ret2 = 0;
	key_set = 0;
	/* 
	 * Perform locks in this function
	 */
	ret = _van_inmemtree_cursor_locate(cursor, key, flags);
	if (ret != 0)
		return (ret);
	
	leaf = (INMEM_BLEAF *)cursor->node;
	indx = cursor->indx;
	ik = leaf->keys[indx];
	id = leaf->values[indx];

	/* If the delete flag is set, then means not NOT FOUND */
	if (F_ISSET(ik, IM_DEL))
		return (VAN_NOTFOUND);
	/* 
	 * If not using VAN_SET, we should return the
	 * key as well.
	 */
	if (flags != VAN_SET) {
		ret1 = _van_inmemtree_ret(cursor, ik->v, key, RET_KEY, 0);
		if (ret1 == 0)
			key_set = 1;
	}
	if (ret1 == 0)
		ret2 = _van_inmemtree_ret(cursor, id->v, data, RET_VALUE, 0);

	if (ret1 != 0 || ret2 != 0) {
		ret = (ret1 == 0 ? ret2 : ret1);
		if (key_set) {
			(void)_van_datum_copyfree(key);
		}
	}

	return (ret);
}

/* Interface to put a key-data pair */
int _van_inmemtree_cursor_put(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, VAN_DATUM *data, flags_t flags) {
	int ret;
	INMEM_CONFIG *im_config;

	/* 
	 * Check if the cursor is a write cursor.
	 */
	ret = _van_inmemtree_cursor_write_valid(cursor);
	if (ret != 0)
		return (ret);

	im_config = cursor->im_tree->config;
	ret = _van_inmemtree_search(cursor, key, im_config->base_level,
	    VAN_SET | VAN_CURSOR_WRITE);
	/* We only accept return values of 0 and VAN_NOTFOUND */
	if (ret != 0 && ret != VAN_NOTFOUND)
		return (ret);

	if (ret == 0)
		ret = _van_inmemtree_cursor_update(cursor,
		    data, 0);
	else
		ret = _van_inmemtree_cursor_insert(cursor,
		    key, data);

	return (ret);
}

/* Interface to del a key */
int _van_inmemtree_cursor_del(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, flags_t flags) {
	int ret;
	INMEM_CONFIG *im_config;

	/* 
	 * Check if the cursor is a write cursor.
	 */
	ret = _van_inmemtree_cursor_write_valid(cursor);
	if (ret != 0)
		return (ret);

	im_config = cursor->im_tree->config;
	ret = _van_inmemtree_search(cursor, key, im_config->base_level,
	    VAN_SET | VAN_CURSOR_WRITE);
	/* We only accept return values of 0 and VAN_NOTFOUND */
	if (ret != 0 && ret != VAN_NOTFOUND)
		return (ret);
	
	if (ret == 0)
		ret = _van_inmemtree_cursor_justdel(cursor, 0);
	else
		ret = _van_inmemtree_cursor_putfordel(cursor, key, 0);

	return (ret);
}

/*
 * The key is allocated, so we should not allocate it again.
 * We should have the write lock on the node, and it is
 * set automatically by split functions.
 */
int _van_inmemtree_cursor_insert_internal(INMEM_TREECURSOR *cursor, 
    VAN_DATUM *key, INMEM_BNODE *child) {
	INMEM_BINTERNAL *internal;
	size_t indx;
	INMEM_KEY *ik;
	INMEM_CHILD *iv;
	int ret;

	internal = (INMEM_BINTERNAL *)cursor->node;
	indx = cursor->indx;
	ik = NULL;

	/* Generate the INMEM_KEY */
	ret = _van_inmemtree_gen_key_nocopydatum(key, &ik);
	if (ret != 0)
		return (ret);

	ret = _van_calloc(NULL, 1, sizeof(INMEM_CHILD), &iv);
	if (ret != 0)
		goto err;
	/* 
	 * Whether we need to check here ?
	 * Using pre-split, if a split is needed,
	 * it should split first, so the code
	 * should not be here in that case.
	 */
	VAN_ASSERT(internal->size < internal->capacity);
	VAN_ASSERT(indx > 0);

	/* Move items after */
	if (internal->size > 0 && indx < internal->size) {
		memmove(&internal->keys[indx + 1], &internal->keys[indx],
		    (internal->size - indx) * sizeof(INMEM_KEY *));
		memmove(&internal->childs[indx + 1], &internal->childs[indx],
		    (internal->size - indx) * sizeof(INMEM_CHILD *));
	}

	internal->keys[indx] = ik;
	iv->gen = child->gen;
	iv->pointer = child;
	internal->childs[indx] = iv;
	internal->size += 1;
	/* internal->childs[indx - 1]->gen++; */

	/* 
	 * Whether we need to check split here ?
	 * This is a TODO item. But currently
	 * we assume to use pre-split. So an insert
	 * will not cause the node full.
	 */
	if (internal->size == internal->capacity) {
		/* TODO: Do split here ? */
	}

	if (0) { /* Free the necessary resources on error here */
err:		if (ik != NULL)
			_van_free(NULL, ik);
	}

	return (ret);
}
