#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"

/* 
 * Search a key in a tree.
 * If a level different from base_level is specified,
 * we will stop at that level, otherwise, we will
 * down until the leaf level.
 *
 * For search above leaf level, we always points the
 * cursor at the item >= search_key, if there is no
 * such item, then points the cursor out of the nodes
 * index, this is the place where the search_key should
 * be put. This happens when we need to insert a separate
 * key after a lower level split.
 *
 * For search in leaf level, if the search_key is found,
 * we will just points the cursor at it, and then return 
 * VAN_KEYEXIST, otherwise, we will put cursor before the 
 * item >= search_key, since this is where the search_key
 * should be put. This is used for doing an insert.  If
 * not found, the return value depends on the flag:
 *     VAN_SET: return VAN_NOTFOUND
 *     VAN_SET_RANGE: return 0 (VAN_NOTFOUND, if out of indexes).
 */
int _van_inmemtree_search(INMEM_TREECURSOR *imc, VAN_DATUM *key,
    int dest_level, flags_t flags) {
	int ret, ret1;
	INMEM_TREE *im_tree;
	INMEM_CONFIG *im_config;
	INMEM_BINTERNAL *root;
	INMEM_BNODE *current, *prev;
	INMEM_KEY **keys;
	INMEM_DATA **values;
	INMEM_CHILD **childs;
	INMEM_BINTERNAL *internal;
	INMEM_BLEAF *leaf;
	size_t sz, sp, ep, findx, mid;
	size_t expgen, curgen;
	int cmp, clevel, wlock, use_old, to_retry;

	use_old = 0;
	to_retry = 0;
	if (imc->node != NULL) {
		current = imc->node;
		VAN_ASSERT(current->type == NT_IM_BLEAF);
		use_old = 1;
		goto have_node;
	}
again:
	ret = 0;
	im_tree = imc->im_tree;
	im_config = im_tree->config;
	root = im_tree->root;
	current = (INMEM_BNODE *)root;
	prev = NULL;
	expgen = im_tree->rootgen;

	VAN_ASSERT(dest_level <= root->level);
	VAN_ASSERT(dest_level >= im_config->base_level);

	for (;;) {
		/*
		 * Only do write if we are writing and
		 * we move to the right level.
		 */
		if (dest_level == current->level &&
		    LF_ISSET(VAN_CURSOR_WRITE))
			wlock = 1;
		else
			wlock = 0;

		/* 
		 * Try locking the item.
		 */
		if (wlock) {
			INMEM_RWLOCK_TRYWRLOCK(current, rwlock, ret);
		} else {
			INMEM_RWLOCK_TRYRDLOCK(current, rwlock, ret);
		}

		/* 
		 * Always free prev.
		 */
		if (prev != NULL) {
			INMEM_RWLOCK_UNLOCK(prev, rwlock, ret1);
		}
		if (ret == EAGAIN || ret == EBUSY || ret == EDEADLK) {
			/*
			 * Previous try does not succeed, so we need
			 * to wait here to get the lock.
			 */
			if (wlock) {
				INMEM_RWLOCK_WRLOCK(current, rwlock, ret);
			} else {
				INMEM_RWLOCK_RDLOCK(current, rwlock, ret);
			}
		} else if (ret != 0)
			goto err;

		if (ret != 0)
			goto err;

		if (expgen != current->gen) {
			/* 
			 * The node has been splitted, so 
			 * start from the root again.
			 */
			INMEM_RWLOCK_UNLOCK(current, rwlock, ret1);
			goto again;
		}
		
		/* Now we lock the node, and know the gen 
		 * does not changed, means no split.
		 */	

		/* 
		 * TODO:
		 * Check for split. Currently, split right away,
		 * but later, we may just mark it for split, and
		 * continue the search.  And the split_point makes
		 * actual sense later.
		 */
		if (current->size + im_config->split_point > 
		    current->capacity) {
			/* 
			 * Need to change our lock.
			 */
			if (wlock == 0) {
				INMEM_RWLOCK_UNLOCK(current, rwlock, ret1);
				INMEM_RWLOCK_WRLOCK(current, rwlock, ret);
				if (current->gen != expgen) {
					INMEM_RWLOCK_UNLOCK(current,
					    rwlock, ret1);
					goto again;
				}
			}

			ret = _van_inmemtree_split_node(im_tree, current);
			if (ret != 0) {
				/*
				 * TODO:
				 * we may check the returned value
				 * carefully later.
				 */
				goto err;
			}
			/* 
			 * The root may have been changed, so we must
			 * start from scratch. Also, the write lock on
			 * current node will be released in the split
			 * function.
			 */
			goto again;
		}

have_node:
		sz = current->size;
		clevel = current->level;
		if (current->type == NT_IM_BINTERNAL) {
			keys = ((INMEM_BINTERNAL *)current)->keys;
		} else {
			keys = ((INMEM_BLEAF *)current)->keys;
		}
		/* 
		 * Empty node should be consider later,
		 * so we first consider no empty nodes here.
		 */
		ep = sz - 1;
		sp = (current->type == NT_IM_BINTERNAL ? 1 : 0);
		/* 
		 * Check if current is empty.
		 * Check first and last
		 */
		if (sz == sp) {
			findx = sp;
			cmp = -1;
		} else if ((cmp = _van_inmemtree_key_docmp(imc, 
		    key, keys[sp])) <= 0) {
			findx = sp;
		} else if((cmp = _van_inmemtree_key_docmp(
		    imc, key, keys[ep])) >= 0) {
			findx = ep;
		} else {
			/* Binary search in the node */
			while (sp < ep) {
				mid = (sp + ep) / 2;
				cmp = _van_inmemtree_key_docmp(imc,
				    key, keys[mid]);
				if (cmp == 0) {
					findx = mid;
					break;
				} else if (cmp > 0) {
					sp = mid + 1;
				} else {
					ep = mid - 1;
				}

			}
			if (sp >= ep) {
				findx = sp;
				/* 
				 * We need to re-set the cmp value.
				 * Since the value is for cmparison to 'mid',
				 * may not be 'findx'.
				 */ 
				if (findx != mid)
					cmp = _van_inmemtree_key_docmp(imc,
					    key, keys[findx]);
			}
		}

		/* Check the level, to see if we should stop here */
		if (clevel == dest_level)
			break;

		/* 
		 * Otherwise, we are the internal nodes.
		 * The rule is that, the corresponding
		 * child is with keys > this_key, while the
		 * previous child is with keys <= this_key.
		 */
		prev = current;
		VAN_ASSERT(clevel > im_config->base_level);
		internal = (INMEM_BINTERNAL *)current;
		if (cmp <= 0) {
			current = internal->childs[findx - 1]->pointer;
			expgen = internal->childs[findx - 1]->gen;
		} else {
			current = internal->childs[findx]->pointer;
			expgen = internal->childs[findx]->gen;
		}
	}
	/* 
	 * Here, we get the findx and the cmp value. Depends on flags,
	 * we can do different things. 
	 * When we stop at a leaf node, we are returning the location
	 * of the data or the location to insert the data(the 
	 * place >= specified key), if the key is beyond all keys in
	 * the node, we return the one after largest, which is still
	 * the insert point.
	 *
	 * When we stop at an internal node, we are in the processing
	 * of inserting parent after a child split, here we also need 
	 * to return the insert point. The same 
	 */
	imc->node = current;
	if (cmp <= 0)
		imc->indx = findx;
	else
		imc->indx = findx + 1;
	
	switch (flags & VAN_MASKMODE) {
		case 0:
		case VAN_SET:
			if (cmp == 0)
				ret = 0;
			else
				ret = VAN_NOTFOUND;
			break;
		case VAN_SET_RANGE:
			if (imc->indx >= imc->node->size)
				ret = VAN_NOTFOUND;
			else if (imc->indx == 0 && cmp < 0 && use_old)
				ret = VAN_NOTFOUND;
			else
				ret = 0;
		default:
			ret = VAN_INVALID;
	}

	/* We only retry if we get the expected error */
	if (ret == VAN_NOTFOUND)
		to_retry = 1;

	if (ret != 0) {
err:
		if (use_old && imc->node != NULL) {
			/* TODO: special handling of failure */
			INMEM_RWLOCK_UNLOCK(imc->node, rwlock, ret1);
			imc->node = NULL;
		}
		if (use_old && to_retry) {
			use_old = 0;
			goto again;
		}
	}

	return (ret);
}
