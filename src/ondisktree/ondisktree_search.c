#include "van_int.h"
#include "van.h"

#include "van_ondisk_tree.h"

/* 
 * Search a key in a tree.
 * We always search to the leaf node, since ondisktree
 * is static data(readonly, so no change for a put
 * and split).
 *
 * For search above leaf level, we always points the
 * cursor at the item >= search_key, if there is no
 * such item, then points the cursor out of the nodes
 * index, this is the place where VAN_SET_RANGE should
 * be pointed.
 *
 * For search in leaf level, if the search_key is found,
 * we will just points the cursor at it, and then return 
 * VAN_KEYEXIST, otherwise, we will put cursor before the 
 * item >= search_key, the return value depends on the flag:
 *     VAN_SET: return VAN_NOTFOUND
 *     VAN_SET_RANGE: return 0 (VAN_NOTFOUND, if out of indexes).
 */
int _van_ondisktree_search(ONDISK_TREECURSOR *cursor, VAN_DATUM *key,
    flags_t flags) {
	int ret, ret1;
	ONDISK_TREE *tree;
	ONDISK_META *meta;
	ONDISK_HEADER *current;
	uint32 cur_blkid, blkid;
	size_t sz, sp, ep, findx, mid;
	int cmp, use_old, to_retry;

	use_old = 0;
	to_retry = 0;
	if (cursor->page != NULL) {
		/* Check current page */
		current = cursor->page;
		cursor->page = NULL;
		/* It must be a leaf page */
		VAN_ASSERT(current->type == NT_OD_BLEAF);
		use_old = 1;
		goto have_page;
	}

start:
	ret = 0;
	tree = cursor->tree;
	meta = tree->meta;
	cur_blkid = meta->root_blkid;
	current = NULL;

	for (;;) {
		ret  = _van_ondisk_cache_get(tree, cur_blkid, &current);
		if (ret != 0)
			goto err;
have_page:
		sz = current->size;
		/* 
		 * Empty node should be consider later,
		 * so we first consider no empty nodes here.
		 */
		ep = sz - 2; /* The last indx is data or block id */
		sp = (current->type == NT_OD_BINTERNAL ? 2 : 0);
		/* 
		 * Check if current is empty.
		 * Check first and last
		 */
		if (sz == sp) {
			findx = sp;
			cmp = -1;
		} else if ((ret = _van_ondisktree_key_docmp(cursor, 
		    key, P_KEY(current, sp), &cmp)) != 0)
			goto err;
		else if (cmp <= 0)
			findx = sp;
		else if ((ret = _van_ondisktree_key_docmp(
		    cursor, key, P_KEY(current, ep), &cmp)) != 0)
			goto err;
		else if (cmp >= 0)
			findx = ep;
		else {
			/* Binary search in the node */
			while (sp < ep) {
				mid = (sp + ep) / 2;
				mid = (mid / 2) * 2; /* Always even value. */
				ret = _van_ondisktree_key_docmp(cursor,
				    key, P_KEY(current, mid), &cmp);
				if (ret != 0)
					goto err;
				if (cmp == 0) {
					findx = mid;
					break;
				} else if (cmp > 0) {
					sp = mid + 2;
				} else {
					ep = mid - 2;
				}
			}
			if (sp >= ep) {
				findx = sp;
				/* 
				 * We need to re-set the cmp value.
				 * Since the value is for cmparison to 'mid',
				 * may not be 'findx'.
				 */ 
				if (findx != mid) {
					ret = _van_ondisktree_key_docmp(cursor,
					    key, P_KEY(current, findx), &cmp);
					if (ret != 0)
						goto err;
				}
			}
		}

		/* Check the type, We can stop at the leaf */
		if (current->type == NT_OD_BLEAF)
			break;

		/* 
		 * Otherwise, we are the internal nodes.
		 * The rule is that, the corresponding
		 * child is with keys > this_key, while the
		 * previous child is with keys <= this_key.
		 */
		VAN_ASSERT(current->type == NT_OD_BINTERNAL);
		if (cmp <= 0) {
			cur_blkid = P_BLKID(current, findx - 1);
		} else {
			cur_blkid = P_BLKID(current, findx + 1);
		}
		ret = _van_ondisk_cache_put(tree, current);
		current = NULL;
		if (ret != 0)
			goto err;
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
	cursor->page = current;
	if (cmp <= 0)
		cursor->indx = findx;
	else
		cursor->indx = findx + 2;
	
	switch (flags & VAN_MASKMODE) {
		case 0:
		case VAN_SET:
			if (cmp == 0)
				ret = 0;
			else
				ret = VAN_NOTFOUND;
			break;
		case VAN_SET_RANGE:
			if (cursor->indx >= cursor->page->size)
				ret = VAN_NOTFOUND;
			else if (cursor->indx == 0 && cmp < 0 && use_old)
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
		if (cursor->page != NULL) {
			/* Special handling of failure */
			ret1 = _van_ondisk_cache_put(tree, cursor->page);
		}
		if (current != NULL && current != cursor->page) {
			/* Special handling of failure */
			ret1 = _van_ondisk_cache_put(tree, current);
		}
		cursor->page = NULL;
		current = NULL;
		if (use_old && to_retry) {
			use_old = 0;
			goto start;
		}
	}

	return (ret);
}
