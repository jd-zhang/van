#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"
#include "van_ondisk_tree.h"

#define	SUMBUFSIZE	1024
int _van_ondisktree_create(VAN_TABLE *table, ONDISK_TREE **treep) {
	int ret;
	ONDISK_TREE *tree;
	
	VAN_ASSERT(treep != NULL);
	ret = _van_calloc(NULL, 1, sizeof(ONDISK_TREE), &tree);
	if (ret != 0)
		return (ret);
	tree->table = table;
	ret = _van_ondisktree_init(tree);
	if (ret == 0)
		*treep = tree;

	return (ret);
}

int _van_ondisktree_init(ONDISK_TREE *tree) {
	int ret;

	ret = 0;
	tree->meta = NULL;
	tree->meta_summary = NULL;
	tree->tree_fid = -1;
	tree->bloom_fid = -1;
	tree->metasize = sizeof(ONDISK_META);
	tree->blksize = OD_DEF_BLKSIZE;
	tree->blob_threshold = OD_BLOB_THRESHOLD;
	tree->blk_cachesize = OD_DEF_CACHESIZE;
	tree->omode = DEF_FILE_MODE;
	tree->blob_dir = NULL;
	tree->file_path = NULL;
	tree->bloom_path = NULL;
	tree->buildinfo = NULL;

	/*
	 * Need to consider:
	 * created_ticks, merged_ticks, table_indx, flags.
	 */
	tree->refcount = 0;
	VAN_MUTEX_INIT(tree, tree_mutex, ret);

	VAN_MUTEX_INIT(&tree->odfile, mtx_head, ret);
	TAILQ_INIT(&tree->odfile.head);

	tree->get = _van_ondisktree_get;
	tree->getraw = _van_ondisktree_getraw;

	return (ret);
}

int _van_ondisktree_init_cache(ONDISK_TREE *tree) {
	ONDISK_META *meta;
	int ret;
	size_t nblks;

	/* No real work here. */
	ret = 0;
	meta = tree->meta;
	nblks = meta->root_blkid + 1;

	return (ret);
}

int _van_ondisktree_open(ONDISK_TREE *tree, const char *file_path,
    flags_t flags) {
	ONDISK_META *meta;
	ONDISK_META_SUMMARY *summary;
	int ret, oflag;
	struct st;

	ret = 0;
	meta = NULL;
	summary = NULL;

	ret = _van_calloc(NULL, 1, tree->metasize, &meta);
	CHK_ERR(ret);
	tree->meta = meta;

	ret = _van_calloc(NULL, 1, sizeof(*summary), &summary);
	CHK_ERR(ret);
	tree->meta_summary = summary;

	ret =  _van_strdup(NULL, file_path, &tree->file_path);
	CHK_ERR(ret);

	oflag = O_RDWR;
	if (LF_ISSET(VAN_OPEN_RDONLY))
		oflag = O_RDONLY;
	if (LF_ISSET(VAN_OPEN_CREAT))
		oflag |= O_CREAT;
	if (LF_ISSET(VAN_OPEN_TRUNC))
		oflag |= O_TRUNC;
	if (LF_ISSET(VAN_OPEN_APPEND))
		oflag |= O_APPEND;
	/*
	if (LF_ISSET(VAN_OPEN_DIRECT))
		oflag = O_DIRECT;
	*/
	if (LF_ISSET(VAN_OPEN_EXCL))
		oflag |= O_EXCL;
	if (LF_ISSET(VAN_OPEN_NONBLOCK))
		oflag |= O_NONBLOCK;

	ret = _van_file_open(tree->file_path, oflag,
	    tree->omode, &tree->tree_fid);
	if (ret != 0)
		goto err;
	tree->odfile.fid = tree->tree_fid;

	if (!LF_ISSET(VAN_OPEN_TRUNC)) {
		ret = _van_ondisktree_read(tree);
		if (ret == 0)
			ret = _van_ondisktree_initwithmeta(tree);
		else if (ret == VAN_NOTFOUND)
			ret = _van_ondisktree_initmeta(tree);
		else
			goto err;
	} else
		ret = _van_ondisktree_initmeta(tree);
	/*TODO: check error here */

	/* The DATUM used for opeations */
	ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
	    &tree->defretkey);
	if (ret == 0) {
		tree->defretkey->ulen = DEF_KEY_ULEN;
		ret = _van_calloc(NULL, 1, DEF_KEY_ULEN,
		    &tree->defretkey->data);
	}
	if (ret == 0)
		ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
		    &tree->defretdata);
	if (ret == 0) {
		tree->defretdata->ulen = DEF_VAL_ULEN;
		ret = _van_calloc(NULL, 1, DEF_VAL_ULEN,
		    &tree->defretdata->data);
	}
	if (ret == 0)
		ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
		    &tree->reakey);
	if (ret == 0)
		ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
		    &tree->readata);

err:
	/*
	 * We can add some other processing here,
	 * instead of do the cleanup only after tree close,
	 * but, there is no strong need currently. The
	 * usual action after an error is to close the tree.
	 */
	return (ret);

}

int _van_ondisktree_close(ONDISK_TREE *tree, flags_t flags) {
	int ret, ret1;
	ONDISK_CACHE_BLOCK *block;
	size_t indx;
	VAN_STORE *store;

	ret = 0;
	if (tree == NULL)
		return (ret);

	store = tree->table->store;
	/* Put the cache */
	while ((block = TAILQ_FIRST(&tree->odfile.head)) != NULL) {
		TAILQ_REMOVE(&tree->odfile.head, block, tree_links); 
		indx = block->hash_indx;
		VAN_MUTEX_LOCK(&store->cache_header[indx], mtx_hash, ret);
		/* panic handing */
		VAN_ASSERT(ret == 0);
		TAILQ_REMOVE(&store->cache_header[indx].head, block, links);
		VAN_MUTEX_UNLOCK(&store->cache_header[indx], mtx_hash, ret);
		/* panic handing */
		VAN_ASSERT(ret == 0);
		ret = _van_ondisk_cache_block_destory(block);
		/* panic handing */
		VAN_ASSERT(ret == 0);
	}

	if (tree->tree_fid != -1) {
		ret1 = _van_file_close(tree->tree_fid);
		tree->tree_fid = -1;
		SET_RET(ret1, ret);
	}
	if (tree->bloom_fid != -1) {
		ret1 = _van_file_close(tree->bloom_fid);
		tree->bloom_fid = -1;
		SET_RET(ret1, ret);
	}
	/* TODO: Clear its cache when closing. */
	if (LF_ISSET(VAN_CLOSE_REMOVE) && tree->file_path != NULL) {
		ret = _van_file_unlink(tree->file_path);
		SET_RET(ret1, ret);
	}

	ret1 = _van_ondisktree_destory(tree);
	SET_RET(ret1, ret);

	return (ret);
}

int _van_ondisktree_destory(ONDISK_TREE *tree) {
	int ret;

	ret = 0;
	if (tree == NULL)
		return (ret);

	/* Free the memory */
	if (tree->readata != NULL)
		_van_datum_free(tree->readata);
	if (tree->reakey != NULL)
		_van_datum_free(tree->reakey);
	if (tree->defretdata != NULL)
		_van_datum_free(tree->defretdata);
	if (tree->defretkey != NULL)
		_van_datum_free(tree->defretkey);
	if (tree->blob_dir != NULL)
		_van_free(NULL, tree->blob_dir);
	if (tree->file_path != NULL)
		_van_free(NULL, tree->file_path);
	if (tree->bloom_path != NULL)
		_van_free(NULL, tree->bloom_path);
	if (tree->meta != NULL)
		_van_free(NULL, tree->meta);
	if (tree->meta_summary != NULL)
		_van_free(NULL, tree->meta_summary);
	VAN_MUTEX_DESTROY(tree, tree_mutex, ret);
	_van_free(NULL, tree);

	return (ret);
}

int _van_ondisktree_get(ONDISK_TREE *tree,
    VAN_DATUM *k, VAN_DATUM *v, flags_t flags) {
	ONDISK_TREECURSOR *cursor;
	int ret, ret1;

	cursor = NULL;

	ret = _van_ondisktree_cursor_init(tree, &cursor, VAN_TEMPCURSOR);
	if (ret != 0)
		goto err;
	ret = _van_ondisktree_cursor_get(cursor, k, v, VAN_SET);
err:
	if (cursor != NULL) {
		ret1 = _van_ondisktree_cursor_cleanup(cursor);
		SET_RET(ret1, ret);
	}
	return (ret);
}

int _van_ondisktree_getraw(ONDISK_TREE *tree,
    VAN_DATUM *k, VAN_DATUM *v, flags_t flags) {
	ONDISK_TREECURSOR *cursor;
	int ret, ret1;

	cursor = NULL;

	ret = _van_ondisktree_cursor_init(tree, &cursor, VAN_TEMPCURSOR);
	if (ret != 0)
		goto err;
	ret = _van_ondisktree_cursor_getraw(cursor, k, v, VAN_SET);
err:
	if (cursor != NULL) {
		ret1 = _van_ondisktree_cursor_cleanup(cursor);
		SET_RET(ret1, ret);
	}
	return (ret);
}

/*
 * The blksize and blob_threshold should use
 * the values from meta. But the paths in the meta
 * should be replaced
 *
 */
int _van_ondisktree_initwithmeta(ONDISK_TREE *tree) {
	int ret;
	ONDISK_META *meta;

	ret = 0;
	meta = tree->meta;
	tree->blksize = meta->blksize;
	tree->blob_threshold = meta->blob_threshold;
	tree->metasize = meta->metasize;

	return (ret);
}

int _van_ondisktree_initmeta(ONDISK_TREE *tree) {
	int ret;
	ONDISK_META *meta;
	ONDISK_META_SUMMARY *meta_summary;

	ret = 0;

	meta_summary = tree->meta_summary;
	meta_summary->metaversion = META_SUMMARY_VERSION;
	meta_summary->metasize = sizeof(ONDISK_META);

	meta = tree->meta;
	meta->metasize = tree->metasize;
	meta->blob_threshold = tree->blob_threshold;
	meta->blksize = tree->blksize;
	ret = _van_fid(tree->tree_fid, (uint8 *)meta->fileid);
	if (tree->blob_dir != NULL)
		sprintf(meta->blob_dir, "%s", tree->blob_dir);
	if (tree->file_path != NULL)
		sprintf(meta->file_path, "%s", tree->file_path);
	if (tree->bloom_path != NULL)
		sprintf(meta->bloom_path, "%s", tree->bloom_path);
	/* TODO: meta->fileid */

err:
	return ret;
}

