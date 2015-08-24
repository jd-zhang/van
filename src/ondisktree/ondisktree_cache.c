#include "van_int.h"
#include "van.h"

#include "van_ondisk_tree.h"

int _van_ondisk_cache_init(VAN_TABLE *table) {
	int ret, ret1;
	size_t i;

	ret = _van_calloc(NULL, table->cache_header_size,
	    sizeof(ONDISK_CACHE_HEADER), &table->cache_header);
	if (ret != 0)
		return (ret);
	
	for (i = 0; i < table->cache_header_size; i++) {
		TAILQ_INIT(&table->cache_header[i].head);
		VAN_MUTEX_INIT(&table->cache_header[i], mtx_hash, ret1);
		SET_RET(ret1, ret);
	}

	return (ret);
};

int _van_ondisk_cache_destroy(VAN_TABLE *table) {
	int ret, ret1;
	ONDISK_CACHE_BLOCK *block;
	size_t i;

	ret = 0;
	for (i = 0; i < table->cache_header_size; i++) {
		while ((block = 
		    TAILQ_FIRST(&table->cache_header[i].head)) != NULL) {
			TAILQ_REMOVE(&table->cache_header[i].head,
			    block, links);
			ret1 = _van_ondisk_cache_block_destory(block);
			SET_RET(ret1, ret);
		}
		VAN_MUTEX_DESTROY(&table->cache_header[i],
		    mtx_hash, ret);
		SET_RET(ret1, ret);
	}
	_van_free(NULL, table->cache_header);
	
	return (ret);
}

int _van_ondisk_cache_get(ONDISK_TREE *tree, uint32 blkid,
    void *cachep) {
	int ret, ret1;
	VAN_TABLE *table;
	ONDISK_META *meta;
	uint32 indx;
	ONDISK_CACHE_BLOCK *block;
	size_t size;

	ret = 0;
	meta = tree->meta;
	table = tree->table;

	indx = _van_hash_func4(meta->fileid, OD_FILEID_LEN);
	indx ^= (blkid * 509);
	indx = indx % table->cache_header_size;

	VAN_MUTEX_LOCK(&table->cache_header[indx], mtx_hash, ret);
	TAILQ_FOREACH(block, &table->cache_header[indx].head, links) {
		if (memcmp(block->fileid, meta->fileid,
		    OD_FILEID_LEN) == 0 && block->blkid == blkid)
			break;
	}
	    
	if (block != NULL) {
		/* Found. */
		/* The cache has been read */
		block->refcount++;
		VAN_MUTEX_UNLOCK(&table->cache_header[indx],
		    mtx_hash, ret);
		CHK_ERR(ret);
		if (block->load)
			goto ready;

		/* it is been read, we just need to wait. */
		VAN_MUTEX_LOCK(block, mtx_load_cond, ret);
		CHK_ERR(ret);
		if (block->load) {
			VAN_MUTEX_LOCK(block, mtx_load_cond, ret);
			CHK_ERR(ret);
			goto ready;	
		}
		/* Need to wait */
		VAN_COND_WAIT(block, load_cond,
		    &block->mtx_load_cond, ret);
		/* Wait return, so ned to unlock the mutex */
		VAN_MUTEX_UNLOCK(block, mtx_load_cond, ret);
	} else {
		/* Not found, so we need to intialize one. */
		size = SSZA(ONDISK_CACHE_BLOCK, page) + meta->blksize;
		ret = _van_calloc(NULL, 1, size, &block);
		if (ret != 0)
			return (ret);
		VAN_MUTEX_INIT(block, mtx_load_cond, ret);
		VAN_COND_INIT(block, load_cond, ret);
		block->load = 0;
		block->hash_indx = indx;
		block->refcount = 1;
		memcpy(block->fileid, meta->fileid, OD_FILEID_LEN);
		block->blkid = blkid;
		TAILQ_INSERT_HEAD(&table->cache_header[indx].head,
		    block, links);

		/* All set up, so unlock to read the page */
		VAN_MUTEX_UNLOCK(&table->cache_header[indx],
		    mtx_hash, ret);
		
		ret = _van_file_readblk(tree->tree_fid,
		    tree->meta->blksize, blkid, 0, block->page);

		VAN_MUTEX_LOCK(block, mtx_load_cond, ret);
		block->load = 1;
		VAN_COND_BROADCAST(block, load_cond, ret);
		VAN_MUTEX_UNLOCK(block, mtx_load_cond, ret);
	}

ready:
	*(char **)cachep = block->page;

err:
	return (ret);
}

int _van_ondisk_cache_put(ONDISK_TREE *tree, void *cache) {
	int ret;
	VAN_TABLE *table;
	ONDISK_META *meta;
	ONDISK_CACHE_BLOCK *block;


	ret = 0;
	meta = tree->meta;
	table = tree->table;

	block = (ONDISK_CACHE_BLOCK *)((char *)cache - 
	    SSZA(ONDISK_CACHE_BLOCK, page));
	VAN_MUTEX_LOCK(&table->cache_header[block->hash_indx],
	    mtx_hash, ret);
	VAN_ASSERT(block->refcount > 0);
	block->refcount--;
	VAN_MUTEX_UNLOCK(&table->cache_header[block->hash_indx],
	    mtx_hash, ret);

	return (ret);
}

int _van_ondisk_cache_block_destory(ONDISK_CACHE_BLOCK *block) {
	int ret;
	
	ret = 0;
	if (block == NULL)
		return (ret);

	VAN_MUTEX_DESTROY(block, mtx_load_cond, ret);
	VAN_COND_DESTROY(block, load_cond, ret);
	_van_free(NULL, block);

	return (ret);
}
