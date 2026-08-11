/*
 * Copyright (c) 2026 VanDB Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "van_int.h"
#include "van.h"

#include "van_ondisk_tree.h"

/* TODO: Use the cache size.*/

int _van_ondisk_cache_init(VAN_STORE *store) {
	int ret, ret1;
	size_t i;

	ret = _van_calloc(NULL, store->cache_header_size,
	    sizeof(ONDISK_CACHE_HEADER), &store->cache_header);
	if (ret != 0)
		return (ret);
	
	for (i = 0; i < store->cache_header_size; i++) {
		TAILQ_INIT(&store->cache_header[i].head);
		VAN_MUTEX_INIT(&store->cache_header[i], mtx_hash, ret1);
		SET_RET(ret1, ret);
	}

	return (ret);
};

int _van_ondisk_cache_destroy(VAN_STORE *store) {
	int ret, ret1;
	ONDISK_CACHE_BLOCK *block;
	size_t i;

	ret = 0;
	for (i = 0; i < store->cache_header_size; i++) {
		while ((block =
		    TAILQ_FIRST(&store->cache_header[i].head)) != NULL) {
			TAILQ_REMOVE(&store->cache_header[i].head,
			    block, links);
			ret1 = _van_ondisk_cache_block_destory(block);
			SET_RET(ret1, ret);
		}
		VAN_MUTEX_DESTROY(&store->cache_header[i],
		    mtx_hash, ret);
		SET_RET(ret1, ret);
	}
	_van_free(NULL, store->cache_header);
	
	return (ret);
}

int _van_ondisk_cache_get(ONDISK_TREE *tree, uint32 blkid,
    void *cachep) {
	int ret, ret1;
	VAN_STORE *store;
	ONDISK_META *meta;
	uint32 indx;
	ONDISK_CACHE_BLOCK *block;
	size_t size;

	ret = 0;
	meta = tree->meta;
	store = tree->table->store;

	indx = _van_hash_func4(meta->fileid, OD_FILEID_LEN);
	indx ^= (blkid * 509);
	indx = indx % store->cache_header_size;

	VAN_MUTEX_LOCK(&store->cache_header[indx], mtx_hash, ret);
	TAILQ_FOREACH(block, &store->cache_header[indx].head, links) {
		if (memcmp(block->fileid, meta->fileid,
		    OD_FILEID_LEN) == 0 && block->blkid == blkid)
			break;
	}
	
	if (block != NULL) {
		/* Found. */
		/* The cache has been read */
		block->refcount++;
		VAN_MUTEX_UNLOCK(&store->cache_header[indx],
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
		TAILQ_INSERT_HEAD(&store->cache_header[indx].head,
		    block, links);

		/* All set up, so unlock to read the page */
		VAN_MUTEX_UNLOCK(&store->cache_header[indx],
		    mtx_hash, ret);
		
		fprintf(stderr, "Read block(%lu) from (%d)%s\n",
		    (unsigned long)blkid, tree->tree_fid, tree->file_path);
		ret = _van_file_readblk(tree->tree_fid,
		    tree->meta->blksize, blkid, 0, block->page);
		CHK_ERR(ret);

		VAN_MUTEX_LOCK(&tree->odfile, mtx_head, ret);
		TAILQ_INSERT_HEAD(&tree->odfile.head, block, tree_links);
		VAN_MUTEX_UNLOCK(&tree->odfile, mtx_head, ret);

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
	VAN_STORE *store;
	ONDISK_META *meta;
	ONDISK_CACHE_BLOCK *block;


	ret = 0;
	meta = tree->meta;
	store = tree->table->store;

	block = (ONDISK_CACHE_BLOCK *)((char *)cache -
	    SSZA(ONDISK_CACHE_BLOCK, page));
	VAN_MUTEX_LOCK(&store->cache_header[block->hash_indx],
	    mtx_hash, ret);
	VAN_ASSERT(block->refcount > 0);
	block->refcount--;
	VAN_MUTEX_UNLOCK(&store->cache_header[block->hash_indx],
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
