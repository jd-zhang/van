#include "van_int.h"
#include "van.h"

#include "van_ondisk_tree.h"

int _van_ondisktree_cursor_init(ONDISK_TREE *tree,
    ONDISK_TREECURSOR **cursorp, flags_t flags) {
	ONDISK_TREECURSOR *cursor;
	int ret;

	VAN_ASSERT(cursorp != NULL);
	*cursorp = NULL;
	cursor = NULL;

	ret = _van_calloc(NULL, 1, sizeof(*cursor), &cursor);
	if (ret != 0)
		return (ret);
	cursor->tree = tree;
	cursor->table = tree->table;
	cursor->page = NULL;
	cursor->indx = INVALID_CURSOR_INDX;
	cursor->flags = flags;

	/* Only support get function */
	cursor->get = _van_ondisktree_cursor_get;

	/* 
	 * TEMP cursor is created by invoking an 
	 * operation using the tree handle. So
	 * we use the memory in the tree handle.
	 */
	if (LF_ISSET(VAN_TEMPCURSOR)) {
		cursor->defretkey = tree->defretkey;
		cursor->defretdata = tree->defretdata;
		cursor->reakey = tree->reakey;
		cursor->readata = tree->readata;
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
		(void)_van_ondisktree_cursor_cleanup(cursor);

	return (ret);
}

int _van_ondisktree_cursor_dup(ONDISK_TREECURSOR *src,
    ONDISK_TREECURSOR *dst) {
	int ret;

	ret = 0;
	/* What about the locks ? */
	dst->tree = src->tree;
	dst->table = src->table;
	dst->page = src->page;
	dst->indx = src->indx;
	dst->flags = src->flags;

	return (ret);
}

/* 
 * We allocate the cursor handle here, and it is the 
 * caller's duty to free the handle.
 */
int _van_ondisktree_cursor_allocdup(ONDISK_TREECURSOR *src,
    ONDISK_TREECURSOR **dstp) {
	int ret;
	ONDISK_TREECURSOR *dst;

	ret = _van_calloc(NULL, 1, sizeof(*dst), &dst);
	if (ret != 0)
		return (ret);
	ret = _van_ondisktree_cursor_dup(src, dst);
	if (ret != 0) {
		_van_free(NULL, dst);
		return (ret);
	}
	*dstp = dst;

	return (ret);
}

int _van_ondisktree_cursor_cleanup(ONDISK_TREECURSOR *src) {
	int ret;

	ret = 0;
	/* 
	 * We need to put the page.
	 */
	if (src->page != NULL) {
		ret = _van_ondisk_cache_put(src->tree, src->page);
		src->page = NULL;
	}

	ret = _van_ondisktree_cursor_cleanup_noput(src);

	return (ret);
}

int _van_ondisktree_cursor_cleanup_noput(ONDISK_TREECURSOR *src) {
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
int _van_ondisktree_cursor_locate(ONDISK_TREECURSOR *cursor, 
    VAN_DATUM *key, flags_t flags) {
	int ret, ret1;
	ONDISK_TREE *tree;

	ret = 0;
	tree = cursor->tree;

	switch (flags & VAN_MASKMODE) {
		case VAN_NEXT:
			if (cursor->page != NULL) {
				ret = _van_ondisktree_cursor_next(cursor);
				break;
			}
		case VAN_FIRST:
			ret = _van_ondisktree_cursor_first(cursor);
			break;
		case VAN_PREV:
			if (cursor->page != NULL) {
				ret = _van_ondisktree_cursor_prev(cursor);
				break;
			}
		case VAN_LAST:
			ret = _van_ondisktree_cursor_last(cursor);
			break;
		case VAN_SET:
		case VAN_SET_RANGE:
			ret = _van_ondisktree_search(cursor, key, flags);
			/* Avoid mis-overwriting the values. */
			if (ret != 0) {
				/* 
				 * Also need to put the cache.
				 * This will have some performance
				 * penalty when the next searching
				 * pair is in this node.
				 */
				if (cursor->page != NULL) {
					ret1 = _van_ondisk_cache_put(tree,
					    cursor->page);
					cursor->page = NULL;
					SET_RET(ret1, ret);
				}
				cursor->page = NULL;
			}
			break;
		case VAN_CURRENT:
			ret = 0; /* do not change current cursor */
			break;
		default:
			ret = _van_error_path(
		"unknown flags passed to _van_ondisktree_cursor_locate!");
			break;
	}

	return (ret);
}

int _van_ondisktree_cursor_next(ONDISK_TREECURSOR *cursor) {
	ONDISK_TREE *tree;
	int ret, ret1, found;
	ONDISK_HEADER *leaf;
	ONDISK_ITEM *key;
	size_t st, et, i;
	uint32 nxt_blkid, cur_blkid;

	VAN_ASSERT(cursor->page != NULL);
	ret = VAN_NOTFOUND;
	found = 0;
	tree = cursor->tree;
	leaf = cursor->page;
	cur_blkid = nxt_blkid = leaf->blkid;
	i = 0;
	
	while (IS_VALID_BLKID(nxt_blkid)) {
		st = (cur_blkid == nxt_blkid ? cursor->indx + 2 : 0);
		et = leaf->size;
		for (i = st; i < et; i += 2) {
			key = P_KEY(leaf, i);
			if (F_ISSET(key, IM_DEL))
				continue;
			found = 1;
			break;
		}

		if (found)
			break;

		nxt_blkid = leaf->next_blkid;
		/* Unlock the node, TODO erroe check */
		ret1 = _van_ondisk_cache_put(tree, leaf);
		cursor->page == NULL;
		leaf = NULL;
		/* TODO: error check */
		if (IS_VALID_BLKID(nxt_blkid)) {
			ret1 = _van_ondisk_cache_get(tree,
			    nxt_blkid, &leaf);
			/* TODO: error check */
		}
	}

	/* 
	 * BUG: ? After moving out of the end, the cursor becomes
	 * unintialized.
	 */
	cursor->page = leaf;
	cursor->indx = i;
	if (found)
		ret = 0;

	return (ret);
}

int _van_ondisktree_cursor_prev(ONDISK_TREECURSOR *cursor) {
	ONDISK_TREE *tree;
	ONDISK_HEADER *leaf;
	int ret, ret1, found;
	ONDISK_ITEM *key;
	size_t st, et, i;
	uint32 prev_blkid, cur_blkid;

	VAN_ASSERT(cursor->page != NULL);
	ret = VAN_NOTFOUND;
	found = 0;
	tree = cursor->tree;
	leaf = cursor->page;
	cur_blkid = prev_blkid = leaf->blkid;
	i = 2;

	while (IS_VALID_BLKID(prev_blkid)) {
		st = (cur_blkid == prev_blkid ? cursor->indx : leaf->size);
		et = 2;
		if (st >= 2) {
			for (i = st; i >= et; i -= 2) {
				key = P_KEY(leaf, i - 2);
				if (F_ISSET(key, IM_DEL))
					continue;
				found = 1;
				break;
			}
		} else {
			VAN_ASSERT(st == 0);
		}
		if (found)
			break;

		prev_blkid = leaf->prev_blkid;
		/* TODO: error check */
		ret1 = _van_ondisk_cache_put(tree, leaf);
		cursor->page == NULL;
		leaf = NULL;
		if (IS_VALID_BLKID(prev_blkid)) {
			/* TODO: error check */
			ret1 = _van_ondisk_cache_get(tree,
			    prev_blkid, &leaf);
		}
	}

	cursor->page = leaf;
	cursor->indx = i - 2;
	if (found)
		ret = 0;

	return (ret);
}

int _van_ondisktree_cursor_first(ONDISK_TREECURSOR *cursor) {
	ONDISK_TREE *tree;
	ONDISK_HEADER *leaf;
	ONDISK_ITEM *key;
	int ret;

	tree = cursor->tree;
	/* 
	 * The only possibility of size 0
	 * is that, there is only one node,
	 * and there is no data. Once split
	 * happens, no node can have size 0.
	 */
	if (tree->meta->nrecs == 0)
		ret = VAN_NOTFOUND;
	else {
		/* TODO: error check */
		if (cursor->page != NULL && 
		    cursor->page->blkid == tree->meta->first_blkid)
			leaf = cursor->page;
		else {
			/* TODO: error handling */
			if (cursor->page != NULL) {
				ret = _van_ondisk_cache_put(tree,
				    cursor->page);
				cursor->page = NULL;
			}
			ret = _van_ondisk_cache_get(tree,
			    tree->meta->first_blkid, &leaf);
		}
		key = P_KEY(leaf, 0);
		cursor->page = leaf;
		cursor->indx = 0;
		if (!F_ISSET(key, IM_DEL))
			ret = 0;
		else
			ret = _van_ondisktree_cursor_next(cursor);
	}

	return (ret);
}

int _van_ondisktree_cursor_last(ONDISK_TREECURSOR *cursor) {
	ONDISK_TREE *tree;
	ONDISK_HEADER *leaf;
	ONDISK_ITEM *key;
	int ret;
	size_t i;

	ret = VAN_NOTFOUND;
	tree = cursor->tree;

	if (tree->meta->nrecs == 0) {
		ret = VAN_NOTFOUND;
	} else {
		/* TODO: error check */
		if (cursor->page != NULL &&
		    cursor->page->blkid == tree->meta->last_blkid)
			leaf = cursor->page;
		else {
			if (cursor->page != NULL) {
				ret = _van_ondisk_cache_put(tree,
				    cursor->page);
				cursor->page = NULL;
			}
			ret = _van_ondisk_cache_get(tree,
			    tree->meta->last_blkid, &leaf);
		}
		i = leaf->size - 2;
		key = P_KEY(leaf, i);
		cursor->page = leaf;
		cursor->indx = i;
		if (!F_ISSET(key, IM_DEL))
			ret = 0;
		else 
			ret = _van_ondisktree_cursor_prev(cursor);
	}

	return (ret);
}

/* Interface to get a key-data pair */
int _van_ondisktree_cursor_get(ONDISK_TREECURSOR *cursor,
    VAN_DATUM *key, VAN_DATUM *data, flags_t flags) {
	int ret, ret1, ret2, key_set;
	ONDISK_ITEM *ik, *id;
	ONDISK_HEADER *leaf;
	size_t indx;

	ret1 = 0;
	ret2 = 0;
	key_set = 0;
	/* 
	 * Perform locks in this function
	 */
	ret = _van_ondisktree_cursor_locate(cursor, key, flags);
	if (ret != 0)
		return (ret);
	
	leaf = cursor->page;
	indx = cursor->indx;
	ik = P_KEY(leaf, indx);
	id = P_DATA(leaf, indx+1);

	/* If the delete flag is set, then means not NOT FOUND */
	if (F_ISSET(ik, IM_DEL))
		return (VAN_NOTFOUND);
	/* 
	 * If not using VAN_SET, we should return the
	 * key as well.
	 */
	if (flags != VAN_SET) {
		ret1 = _van_ondisktree_ret(cursor, ik, key, RET_KEY, 0);
		if (ret1 == 0)
			key_set = 1;
	}
	if (ret1 == 0)
		ret2 = _van_ondisktree_ret(cursor, id, data, RET_VALUE, 0);

	if (ret1 != 0 || ret2 != 0) {
		ret = (ret1 == 0 ? ret2 : ret1);
		if (key_set) {
			(void)_van_datum_copyfree(key);
		}
	}

	return (ret);
}
