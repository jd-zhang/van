#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"
#include "van_ondisk_tree.h"

int _van_ondisktree_store(ONDISK_TREE *od_tree, INMEM_TREE *im_tree) {
	int ret;
	size_t i, toplevel, sumsize, sumtotal;
	ONDISK_BUILDINFO *bi;
	ONDISK_HEADER *page;
	ONDISK_META *meta;
	struct stat st;
	char *sumbuf;

	INMEM_BLEAF *leaf;
	INMEM_KEY **keys;
	INMEM_DATA **values;

	bi = NULL;
	ret = 0;
	meta = od_tree->meta;

	VAN_ASSERT(od_tree->buildinfo == NULL);
	ret = _van_calloc(NULL, 1, sizeof(ONDISK_BUILDINFO),
	    &od_tree->buildinfo);
	if (ret != 0)
		return (ret);
	bi = od_tree->buildinfo; 
	ret = _van_calloc(NULL, OD_MAX_LEVEL + 1,
	    sizeof(odheadptr_arr2), &bi->nodes);
	if (ret != 0)
		goto clean;
	ret = _van_calloc(NULL, OD_MAX_LEVEL + 1,
	    sizeof(ONDISK_ITEM *), &bi->reserved_keys);
	if (ret != 0)
		goto clean;

	/*
	ret = _van_inmemtree_traverse_forard(im_tree, 
	    _van_ondisktree_addpair, od_tree);
	*/
	leaf = im_tree->first;
	while (leaf != NULL) {
		keys = leaf->keys;
		values = leaf->values;
		for (i = 0; i < leaf->size; i++) {
			ret = _van_ondisktree_addpair(od_tree,
			    keys[i], values[i]);
			/* TODO: change to error handling */
			VAN_ASSERT(ret == 0);
		}
		leaf = leaf->next;
	}

	/* Now Add the left pages */
	for (i = 0; i <= bi->toplevel; i++) {
		VAN_ASSERT(bi->nodes[i][1] == NULL);
		page = bi->nodes[i][0];
		VAN_ASSERT(page != NULL);
		if (i != 0) {
			page->blkid = bi->next_blkid;
			bi->next_blkid++;
		}
		if (i == bi->toplevel)
			bi->root_blkid = page->blkid;
		ret = _van_file_writeblk(od_tree->tree_fid, meta->blksize,
		    page->blkid, 0, page);
		/* TODO: change to error handling */
		VAN_ASSERT(ret == 0);
		if (i < bi->toplevel) {
			ret = _van_ondisktree_addto_internal(od_tree,
			    NULL, page->blkid, i+1);
			/* TODO: change to error handling */
			VAN_ASSERT(ret == 0);
		}
		_van_free(NULL, page);
		bi->nodes[i][0] = NULL;
	}

	/* Now write the meta */
	meta->nlevel = bi->toplevel + 1;
	meta->nrecs = bi->nrecs;
	meta->root_blkid = bi->root_blkid;
	meta->first_blkid = bi->first_blkid;
	meta->last_blkid = bi->last_blkid;

	/* 
	 * The final part is:
	 * meta|align|meta_summary
	 * the total size is aligned with 1024(1K).
	 * So when reading, we just need to read the
	 * last 1K to get the summary and also may
	 * get the whole metadata.
	 */
	ret = _van_file_stat(od_tree->tree_fid, &st);
	od_tree->meta_summary->metaoff = st.st_size;
	sumsize = sizeof(ONDISK_META_SUMMARY);
	sumtotal = sumsize + meta->metasize;
	sumtotal = VAN_ALIGN(sumtotal, 1024);
	ret = _van_calloc(NULL, 1, sumtotal, &sumbuf);
	memcpy(sumbuf, meta, meta->metasize);
	memcpy(sumbuf + sumtotal - sumsize,
	    od_tree->meta_summary, sumsize);

	ret = _van_file_append(od_tree->tree_fid, sumbuf, sumtotal);
	/* TODO: change to error handling */
	VAN_ASSERT(ret == 0);
	ret = _van_file_fsync(od_tree->tree_fid);
	/* TODO: change to error handling */
	VAN_ASSERT(ret == 0);
clean:
	if (bi != NULL) {
		/* Clean the bi here */
		for (i = 0; i <= OD_MAX_LEVEL; i++) {
			if (bi->reserved_keys[i] != NULL)
				_van_free(NULL, bi->reserved_keys[i]);
		}
		_van_free(NULL, bi->reserved_keys);
		for (i = 0; i <= OD_MAX_LEVEL; i++) {
			if (bi->nodes[i][0] != NULL)
				_van_free(NULL, bi->nodes[i][0]);
			if (bi->nodes[i][1] != NULL)
				_van_free(NULL, bi->nodes[i][1]);
		}
		_van_free(NULL, bi->nodes);
		_van_free(NULL, bi);
		od_tree->buildinfo = NULL;
	}

	return (ret);
}

int _van_ondisktree_addpair(void *obj, const INMEM_KEY *key,
    const INMEM_DATA *value) {
	int ret;
	ONDISK_TREE *tree;
	ONDISK_META *meta;
	ONDISK_BUILDINFO *bi;
	ONDISK_HEADER *page, *newpage, *wpage;
	ONDISK_ITEM *upitem, *kitem, *ditem, tmpitem;
	int deleted, i, alloc_d;
	size_t total;

	ret = 0;
	deleted = 0;
	alloc_d = 0;
	tree = (ONDISK_TREE *)obj;
	meta = tree->meta;
	bi = tree->buildinfo;
	newpage = page = NULL;
	upitem = kitem = ditem = NULL;
	memset(&tmpitem, 0, sizeof(ONDISK_ITEM));
	page = bi->nodes[0][0];
	VAN_ASSERT(bi->nodes[0][1] == NULL);
	bi->nrecs++;

	if (page == NULL) {
		/* The first page */
		ret = _van_calloc(NULL, 1, meta->blksize, &page);
		if (ret != 0)
			return (ret);
		ret = _van_ondisktree_init_header(meta, page,
		    0, NT_OD_BLEAF);
		/* TODO: change to error handling */
		VAN_ASSERT(ret == 0);
		bi->nodes[0][0] = page;
		bi->root = page;
		page->blkid = bi->next_blkid;
		bi->next_blkid++;
		bi->first_blkid = page->blkid;
		bi->last_blkid = page->blkid;
	}

	ret = _van_ondiskitem_from_inmemkey(tree, key, &kitem);
	if (ret != 0)
		goto clean;
	if (F_ISSET(key, IM_DEL)) {
		deleted = 1;
		ditem = &tmpitem;
	} else {
		ret = _van_ondiskitem_from_inmemdata(tree, value, &ditem);
		if (ret != 0)
			goto clean;
		alloc_d = 1;
	}
	total = 2 * sizeof(uint32) + P_ITEMSIZE(kitem->len);
	if (!deleted)
		total += P_ITEMSIZE(ditem->len);

	if (total <= page->left) {
		/* Just add it to the page */
		ret = _van_ondisktree_add_lpage(page, kitem, ditem);
		/* TODO: change to error handling */
		VAN_ASSERT(ret == 0);
	} else {
		/* Full, need to add new page and add to uplayer */
		
		/* Allocate the new page */
		ret = _van_calloc(NULL, 1, meta->blksize, &newpage);
		if (ret != 0)
			return (ret);
		ret = _van_ondisktree_init_header(meta, newpage,
		    0, NT_OD_BLEAF);
		/* TODO: change to error handling */
		VAN_ASSERT(ret == 0);
		bi->nodes[0][1] = newpage;
		ret = _van_ondisktree_add_lpage(newpage, kitem, ditem);
		/* TODO: change to error handling */
		VAN_ASSERT(ret == 0);

		/* Add to uplayer */
		ret = _van_ondiskitem_alloccopy(
		    P_KEY(page, page->size - 2), &upitem);
		if (ret != 0)
			goto clean;
		/* 
		 * Always clear the delete flag, since
		 * it does not make sense for internal node.
		 */
		upitem->flags = 0;
		ret = _van_ondisktree_addto_internal(tree, 
		    upitem, page->blkid, 1);

		newpage->blkid = bi->next_blkid;
		bi->next_blkid++;
		page->next_blkid = newpage->blkid;
		newpage->prev_blkid = page->blkid;
		bi->last_blkid = newpage->blkid;
	}
	
	/* Now, write the page if necessary */
	for (i = 0; i <= OD_MAX_LEVEL; i++) {
		/* No page to write */
		if (bi->nodes[i][1] == NULL)
			break;
		wpage = bi->nodes[i][0];
		VAN_ASSERT(wpage != NULL);
		ret = _van_file_writeblk(tree->tree_fid, 
		    meta->blksize, wpage->blkid, 0, wpage);
		VAN_ASSERT(ret == 0);
		_van_free(NULL, wpage);
		bi->nodes[i][0] = bi->nodes[i][1];
		bi->nodes[i][1] = NULL;
	}

clean:
	_van_free(NULL, kitem);
	if (alloc_d)
		_van_free(NULL, ditem);
	return (ret);

}

int _van_ondisktree_addto_internal(ONDISK_TREE *tree, 
    ONDISK_ITEM *upitem, uint32 blkid, uint32 level) {
	int ret;
	ONDISK_META *meta;
	ONDISK_BUILDINFO *bi;
	ONDISK_HEADER *page, *newpage;
	ONDISK_ITEM *preitem;
	size_t total;

	ret = 0;
	meta = tree->meta;
	bi = tree->buildinfo;
	newpage = page = NULL;
	
	page = bi->nodes[level][0];
	VAN_ASSERT(bi->nodes[level][1] == NULL);

	/* The level is not created yet, means a new root and level */
	if (page == NULL) {
		ret = _van_calloc(NULL, 1, meta->blksize, &page);
		if (ret != 0)
			return (ret);
		ret = _van_ondisktree_init_header(meta, page,
		    level, NT_OD_BINTERNAL);
		/* TODO: change to error handling */
		VAN_ASSERT(ret == 0);
		bi->nodes[level][0] = page;
		bi->root = page;
		bi->toplevel++;
		ret = _van_ondisktree_add_ipage(tree, page,
		    upitem, blkid, 1);
		/* TODO: Replace it with error handling */
		VAN_ASSERT(ret == 0);
		goto clean;
	}

	/* Check space */
	preitem = bi->reserved_keys[level];
	VAN_ASSERT(preitem != NULL);
	total = P_ITEMSIZE(preitem->len) + P_BLKSIZE + 2 * sizeof(uint32);
	if (total <= page->left) {
		/* Can add the page */
		ret = _van_ondisktree_add_ipage(tree, page,
		    upitem, blkid, 0);
		/* TODO: change to error handling */
		VAN_ASSERT(ret == 0);
	} else {
		/* Not enough page */
		page->blkid = bi->next_blkid;
		bi->next_blkid++;

		bi->reserved_keys[level] = NULL;

		/* The new page */
		ret = _van_calloc(NULL, 1, meta->blksize, &newpage);
		if (ret != 0)
			return (ret);
		ret = _van_ondisktree_init_header(meta, newpage,
		    level, NT_OD_BINTERNAL);
		/* TODO: change to error handling */
		VAN_ASSERT(ret == 0);
		bi->nodes[level][1] = newpage;
		ret = _van_ondisktree_add_ipage(tree, newpage,
		    upitem, blkid, 1);

		/* Get the largest key.  */
		ret = _van_ondisktree_addto_internal(tree, preitem,
		    page->blkid, level + 1);
	}
clean:
	return (ret);
}

int _van_ondisktree_add_ipage(ONDISK_TREE *tree, ONDISK_HEADER *page,
    ONDISK_ITEM *upitem, uint32 blkid, int isfirst) {
	int ret;
	ONDISK_BUILDINFO *bi;
	ONDISK_ITEM *preitem;
	uint32 indx, newoff;

	ret = 0;
	bi = tree->buildinfo;

	/* We can add the item */
	if (isfirst) {
		newoff = page->offset;
		preitem = bi->reserved_keys[page->level];
		VAN_ASSERT(preitem == NULL);
	} else {
		preitem = bi->reserved_keys[page->level];
		VAN_ASSERT(preitem != NULL);
		newoff = page->offset - P_ITEMSIZE(preitem->len);
		memcpy(P_PTR(page, newoff), preitem,
		    P_ITEMSIZE(preitem->len));
	}
	indx = page->size;
	P_INDX(page, indx) = newoff;
	newoff -= sizeof(uint32);
	memcpy(P_PTR(page, newoff), &blkid, sizeof(uint32));
	indx++;
	P_INDX(page, indx) = newoff;

	if (isfirst) {
		VAN_ASSERT(sizeof(uint32) == 
		    (page->offset - newoff));
	} else {
		VAN_ASSERT((page->offset - newoff) == 
		    (sizeof(uint32) + P_ITEMSIZE(preitem->len)));
	}

	/* The item is added, so we can free it now */
	if (preitem != NULL) {
		_van_free(NULL, preitem);
	}
	bi->reserved_keys[page->level] = upitem;

	page->left -= 2 * sizeof(uint32) + (page->offset - newoff);
	page->offset = newoff;
	page->size += 2;

	return (ret);
}

int _van_ondisktree_add_lpage(ONDISK_HEADER *page, 
    const ONDISK_ITEM *key, const ONDISK_ITEM *data) {
	int ret;
	uint32 indx, newoff;

	ret = 0;

	indx = page->size;
	newoff = page->offset - P_ITEMSIZE(key->len);
	memcpy(P_PTR(page, newoff), key, P_ITEMSIZE(key->len));
	P_INDX(page, indx) = newoff;

	/* Special handling if the item is deleted */
	indx++;
	if (F_ISSET(key, IM_DEL)) {
		P_INDX(page, indx) = newoff;
		VAN_ASSERT((page->offset - newoff) == 
		    P_ITEMSIZE(key->len));
	} else {
		newoff = newoff - P_ITEMSIZE(data->len);
		memcpy(P_PTR(page, newoff), data, P_ITEMSIZE(data->len));
		P_INDX(page, indx) = newoff;
		VAN_ASSERT((page->offset - newoff) ==
		    (P_ITEMSIZE(key->len) + P_ITEMSIZE(data->len)));
	}
	
	page->left -= 2 * sizeof(uint32) + (page->offset - newoff);
	page->offset = newoff;
	page->size += 2;

	return (ret);
}

int _van_ondisktree_init_header(ONDISK_META *meta,
    ONDISK_HEADER *header, uint32 level, uint32 type) {
	int ret;
	
	ret = 0;
	memset(header, 0, meta->blksize);
	header->level = level;
	header->type = type;
	header->size = 0;
	header->capacity = meta->blksize;
	header->left = header->capacity - P_OVERHEAD;
	header->blkid = header->next_blkid = 
	    header->prev_blkid = INVALID_BLKID;
	header->offset = meta->blksize;

	return (ret);
}

int _van_ondisktree_merge(ONDISK_TREE *trees, size_t srccnt,
    ONDISK_TREE **restree) {
	

}

int _van_ondisktree_read(ONDISK_TREE *tree) {
	int ret;
	struct stat st;
	ONDISK_META *meta;
	char sumbuf[SUMBUFSIZE];
	size_t sumtotal, sumsize;
	ONDISK_META_SUMMARY summary;

	sumtotal = sizeof(sumbuf);
	sumsize = sizeof(summary);
	meta = tree->meta;

	ret = _van_file_stat(tree->tree_fid, &st);
	if (ret != 0)
		goto err;
	if (st.st_size == 0) {
		ret = VAN_NOTFOUND;
		goto err;
	} else if (st.st_size < sumtotal) {
		ret = VAN_INVALID;
		goto err;
	}
	
	/* Read the meta data */
	ret = _van_file_read(tree->tree_fid, buf, sumtotal,
	    st.st_size - sumtotal);
	if (ret != 0)
		goto err;
	mecpy(&summary, sumbuf - sumsize, sumsize);
	/* Currently just raw copy */
	memcpy(tree->meta_summary, &summary, sumsize);
	sumtotal = VAN_ALIGN((sumsize + summary.metasize), SUMBUFSIZE);
	/* 
	 * TODO: For upgrade, we need to change this logic
	 * and following copy way.
	 */
	if ((st.st_size != (sumtotal + summary.metaoff)) ||
	    summary->metasize != sizeof(ONDISK_META)) {
		ret = VAN_VALID;
		goto err;
	}

	/* Now read the meta */
	if (sumtotal == SUMBUFSIZE) {
		memcpy(tree->meta, sumbuf, summary->metasize);
	} else {
		ret = _van_file_read(tree->tree_fid, meta,
		    summary->metasize, summary->metaoff);
		if (ret != 0)
			goto err;
	}

	/* Simple post check. */
	if (meta->metasize != summary->metasize ||
	    (st.st_size - meta->metasize) % meta->blksize != 0) {
		ret = VAN_INVALID;
	}
err:
	return (ret);
}
