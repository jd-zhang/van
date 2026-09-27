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

/*
 * The on disk block cache.
 *
 * Replacement is a segmented LRU (SLRU): blocks are read into the
 * probation list and only move to the protected list on their second
 * access.  A block reached once, which is what a full scan does, never
 * leaves probation and is therefore evicted before it can push out the
 * working set.  The cache is read only, evicting a block just drops it.
 *
 * Lock order, followed by every path in this file:
 *
 *	tree->odfile.mtx_head
 *	  -> store->cache_lru.mtx_lru
 *	    -> store->cache_header[i].mtx_hash
 *
 * Reference counting: refcount is atomic so that it can be observed
 * without a lock, but every modification happens under the bucket lock
 * and the decision to evict is always re-made under that same lock.
 *
 * Disk I/O is never performed while any of these locks is held.
 */

/*
 * Detach a block from whichever LRU list it is on and fix up the byte
 * counters.  The caller must hold store->cache_lru.mtx_lru.
 */
static void cache_lru_remove(VAN_STORE *store, ONDISK_CACHE_BLOCK *block) {

	if (block->in_protected) {
		TAILQ_REMOVE(&store->cache_lru.protected, block, lru_links);
		atomic_fetch_sub(&store->cache_lru.bytes_prot,
		    CACHE_BLOCK_BYTES(block));
		block->in_protected = 0;
	} else
		TAILQ_REMOVE(&store->cache_lru.probation, block, lru_links);

	atomic_fetch_sub(&store->cache_lru.bytes_total,
	    CACHE_BLOCK_BYTES(block));
}

/*
 * Record an access.  First access promotes probation -> protected, later
 * accesses just move the block to the MRU end of protected.
 * The caller must hold store->cache_lru.mtx_lru.
 */
static void cache_lru_promote(VAN_STORE *store, ONDISK_CACHE_BLOCK *block) {

	if (block->in_protected) {
		TAILQ_REMOVE(&store->cache_lru.protected, block, lru_links);
		TAILQ_INSERT_HEAD(&store->cache_lru.protected, block,
		    lru_links);
		return;
	}

	TAILQ_REMOVE(&store->cache_lru.probation, block, lru_links);
	TAILQ_INSERT_HEAD(&store->cache_lru.protected, block, lru_links);
	block->in_protected = 1;
	atomic_fetch_add(&store->cache_lru.bytes_prot,
	    CACHE_BLOCK_BYTES(block));
	store->cache_lru.stat_promotions++;
}

/* The least recently used block of the protected list, NULL when empty. */
static ONDISK_CACHE_BLOCK *cache_prot_tail(VAN_STORE *store) {
	ONDISK_CACHE_BLOCK *block, *last;

	last = NULL;
	TAILQ_FOREACH(block, &store->cache_lru.protected, lru_links)
		last = block;

	return (last);
}

/*
 * Keep the protected list within its share of the cache.  Overflowing
 * blocks are demoted, not discarded: they stay cached, they only lose
 * their protection.
 * The caller must hold store->cache_lru.mtx_lru.
 */
static void cache_lru_cap_protected(VAN_STORE *store) {
	ONDISK_CACHE_BLOCK *block;
	size_t cap;

	cap = (store->cache_size * OD_LRU_PROTECTED_PCT) / 100;
	while (atomic_load(&store->cache_lru.bytes_prot) > cap) {
		block = cache_prot_tail(store);
		if (block == NULL)
			break;
		TAILQ_REMOVE(&store->cache_lru.protected, block, lru_links);
		block->in_protected = 0;
		atomic_fetch_sub(&store->cache_lru.bytes_prot,
		    CACHE_BLOCK_BYTES(block));
		TAILQ_INSERT_HEAD(&store->cache_lru.probation, block,
		    lru_links);
		store->cache_lru.stat_demotions++;
	}
}

/*
 * Free blocks until 'needed' bytes fit into the cache again.
 *
 * The caller must hold store->cache_lru.mtx_lru.  Candidates are taken
 * from the probation tail first; when probation has nothing usable the
 * protected tail is demoted and reconsidered.  Blocks that are still
 * referenced are skipped.
 *
 * This is best effort.  When every candidate is pinned by a reader the
 * cache is allowed to grow past store->cache_size: refusing to serve a
 * get is far worse than using a little more memory, and the overshoot is
 * reclaimed by later puts.
 */
static int cache_evict(VAN_STORE *store, size_t needed) {
	ONDISK_CACHE_BLOCK *block, *cand;
	size_t cur, goal;
	int ret, ret1, demoted;

	ret = 0;
	goal = store->cache_size;
	demoted = 0;

	for (;;) {
		cur = atomic_load(&store->cache_lru.bytes_total);
		if (cur + needed <= goal)
			break;

		/* Walk probation from the LRU end for a free candidate. */
		block = NULL;
		TAILQ_FOREACH_REVERSE(cand, &store->cache_lru.probation,
		    _odprob, lru_links) {
			if (atomic_load(&cand->refcount) == 0) {
				block = cand;
				break;
			}
		}

		/*
		 * Nothing usable in probation: demote the protected tail
		 * once, then look again before giving up.
		 */
		if (block == NULL && !demoted) {
			cache_lru_cap_protected(store);
			demoted = 1;
			continue;
		}
		if (block == NULL)
			break;	/* Best effort, tolerate the overshoot. */

		/*
		 * Lock order: LRU -> bucket.  The reference count is
		 * re-checked under the bucket lock because a reader may
		 * have taken a reference since the unlocked scan above.
		 */
		VAN_MUTEX_LOCK(&store->cache_header[block->hash_indx],
		    mtx_hash, ret);
		CHK_ERR(ret);
		if (atomic_load(&block->refcount) == 0) {
			TAILQ_REMOVE(&store->cache_header[block->hash_indx].
			    head, block, links);
			cache_lru_remove(store, block);
			VAN_MUTEX_UNLOCK(&store->cache_header[block->
			    hash_indx], mtx_hash, ret1);
			SET_RET(ret1, ret);
			ret1 = _van_ondisk_cache_block_destory(block);
			SET_RET(ret1, ret);
			store->cache_lru.stat_evictions++;
			demoted = 0;
			continue;
		}
		/* Lost the race with a reader, stop for now. */
		VAN_MUTEX_UNLOCK(&store->cache_header[block->hash_indx],
		    mtx_hash, ret1);
		SET_RET(ret1, ret);
		break;
	}

err:
	return (ret);
}

int _van_ondisk_cache_init(VAN_STORE *store) {
	int ret, ret1;
	int i;

	ret = _van_calloc(NULL, (size_t)store->cache_header_size,
	    sizeof(ONDISK_CACHE_HEADER), &store->cache_header);
	if (ret != 0)
		return (ret);

	for (i = 0; i < store->cache_header_size; i++) {
		TAILQ_INIT(&store->cache_header[i].head);
		VAN_MUTEX_INIT(&store->cache_header[i], mtx_hash, ret1);
		SET_RET(ret1, ret);
	}

	TAILQ_INIT(&store->cache_lru.probation);
	TAILQ_INIT(&store->cache_lru.protected);
	atomic_store(&store->cache_lru.bytes_total, 0);
	atomic_store(&store->cache_lru.bytes_prot, 0);
	store->cache_lru.stat_hits = 0;
	store->cache_lru.stat_misses = 0;
	store->cache_lru.stat_evictions = 0;
	store->cache_lru.stat_promotions = 0;
	store->cache_lru.stat_demotions = 0;
	VAN_MUTEX_INIT(&store->cache_lru, mtx_lru, ret1);
	SET_RET(ret1, ret);

	return (ret);
};

int _van_ondisk_cache_destroy(VAN_STORE *store) {
	int ret, ret1;
	ONDISK_CACHE_BLOCK *block;
	int i;

	ret = 0;
	for (i = 0; i < store->cache_header_size; i++) {
		/*
		 * Lock order: LRU -> bucket.  Both lists have to be
		 * cleaned here, otherwise the LRU lists are left holding
		 * pointers to freed blocks.
		 */
		VAN_MUTEX_LOCK(&store->cache_lru, mtx_lru, ret1);
		SET_RET(ret1, ret);
		VAN_MUTEX_LOCK(&store->cache_header[i], mtx_hash, ret1);
		SET_RET(ret1, ret);
		while ((block =
		    TAILQ_FIRST(&store->cache_header[i].head)) != NULL) {
			TAILQ_REMOVE(&store->cache_header[i].head,
			    block, links);
			cache_lru_remove(store, block);
			ret1 = _van_ondisk_cache_block_destory(block);
			SET_RET(ret1, ret);
		}
		VAN_MUTEX_UNLOCK(&store->cache_header[i], mtx_hash, ret1);
		SET_RET(ret1, ret);
		VAN_MUTEX_UNLOCK(&store->cache_lru, mtx_lru, ret1);
		SET_RET(ret1, ret);

		VAN_MUTEX_DESTROY(&store->cache_header[i],
		    mtx_hash, ret1);
		SET_RET(ret1, ret);
	}
	VAN_MUTEX_DESTROY(&store->cache_lru, mtx_lru, ret1);
	SET_RET(ret1, ret);
	_van_free(NULL, store->cache_header);

	return (ret);
}

int _van_ondisk_cache_get(ONDISK_TREE *tree, uint32 blkid,
    void *cachep) {
	int ret, ret1;
	VAN_STORE *store;
	ONDISK_META *meta;
	uint32 indx;
	ONDISK_CACHE_BLOCK *block, *found;
	size_t size;
	int owned, on_lru;

	ret = 0;
	block = NULL;
	found = NULL;
	/*
	 * Cleanup bookkeeping for the miss path.  These two flags decide
	 * what the err block below is allowed to do with 'block':
	 *
	 *   owned   'block' is the one we allocated, so destroying it is
	 *           our responsibility.  Cleared the moment the block
	 *           becomes somebody else's: either we lost the race and
	 *           switched to the winner's block, or we published ours
	 *           in the hash table where other threads can find it.
	 *
	 *   on_lru  'block' has been linked onto the probation list, so
	 *           removing it also means unlinking it and subtracting
	 *           its bytes from the counters.
	 *
	 * Rules for anyone touching this function:
	 *   - never destroy a block with owned == 0, another thread may
	 *     hold a reference to it;
	 *   - never call cache_lru_remove() on a block with on_lru == 0,
	 *     it is not on any list;
	 *   - set owned = 0 before reassigning 'block' to found.
	 */
	owned = 0;
	on_lru = 0;
	meta = tree->meta;
	store = tree->table->store;

	indx = _van_hash_func4(meta->fileid, OD_FILEID_LEN);
	indx ^= (blkid * 509);
	indx = indx % store->cache_header_size;

	/* ---- Look the block up, bucket lock only. ---- */
	VAN_MUTEX_LOCK(&store->cache_header[indx], mtx_hash, ret);
	CHK_ERR(ret);
	TAILQ_FOREACH(block, &store->cache_header[indx].head, links) {
		if (memcmp(block->fileid, meta->fileid,
		    OD_FILEID_LEN) == 0 && block->blkid == blkid)
			break;
	}

	if (block != NULL) {
		/* Hit. */
		atomic_fetch_add(&block->refcount, 1);
		VAN_MUTEX_UNLOCK(&store->cache_header[indx], mtx_hash, ret1);
		SET_RET(ret1, ret);
		CHK_ERR(ret);

		/*
		 * Lock order: LRU -> bucket, never the reverse.  The
		 * bucket lock is released before the LRU lock is taken,
		 * the two are never held at the same time here.
		 *
		 * That is safe because the reference taken above keeps
		 * the block alive: cache_evict() skips blocks with a
		 * non zero reference count, so the block cannot be freed
		 * in the window between the two locks.
		 */
		VAN_MUTEX_LOCK(&store->cache_lru, mtx_lru, ret);
		CHK_ERR(ret);
		cache_lru_promote(store, block);
		cache_lru_cap_protected(store);
		store->cache_lru.stat_hits++;
		VAN_MUTEX_UNLOCK(&store->cache_lru, mtx_lru, ret1);
		SET_RET(ret1, ret);
		CHK_ERR(ret);
	} else {
		/*
		 * Miss.  The bucket lock is released before allocating
		 * and before the disk read, so neither calloc nor I/O
		 * can strand it (and a calloc failure cannot leak it).
		 */
		VAN_MUTEX_UNLOCK(&store->cache_header[indx], mtx_hash, ret1);
		SET_RET(ret1, ret);
		CHK_ERR(ret);

		size = SSZA(ONDISK_CACHE_BLOCK, page) + meta->blksize;
		ret = _van_calloc(NULL, 1, size, &block);
		CHK_ERR(ret);
		owned = 1;
		VAN_MUTEX_INIT(block, mtx_load_cond, ret);
		CHK_ERR(ret);
		VAN_COND_INIT(block, load_cond, ret);
		CHK_ERR(ret);
		block->load = 0;
		block->hash_indx = indx;
		atomic_store(&block->refcount, 1);
		block->blksize = meta->blksize;
		block->in_protected = 0;
		memcpy(block->fileid, meta->fileid, OD_FILEID_LEN);
		block->blkid = blkid;

		/* Reserve room, then publish on the probation list. */
		VAN_MUTEX_LOCK(&store->cache_lru, mtx_lru, ret);
		CHK_ERR(ret);
		ret1 = cache_evict(store, CACHE_BLOCK_BYTES(block));
		SET_RET(ret1, ret);
		TAILQ_INSERT_HEAD(&store->cache_lru.probation, block,
		    lru_links);
		on_lru = 1;
		atomic_fetch_add(&store->cache_lru.bytes_total,
		    CACHE_BLOCK_BYTES(block));
		store->cache_lru.stat_misses++;
		VAN_MUTEX_UNLOCK(&store->cache_lru, mtx_lru, ret1);
		SET_RET(ret1, ret);
		CHK_ERR(ret);

		/* No lock is held across the read. */
		ret = _van_file_readblk(tree->tree_fid, block->blksize,
		    blkid, 0, block->page);
		CHK_ERR(ret);

		/*
		 * Publish the block.  Another thread may have created
		 * the same block while we were reading; in that case
		 * ours is dropped and theirs is used.
		 */
		VAN_MUTEX_LOCK(&store->cache_lru, mtx_lru, ret);
		CHK_ERR(ret);
		VAN_MUTEX_LOCK(&store->cache_header[indx], mtx_hash, ret1);
		SET_RET(ret1, ret);
		CHK_ERR(ret);
		TAILQ_FOREACH(found, &store->cache_header[indx].head, links) {
			if (memcmp(found->fileid, meta->fileid,
			    OD_FILEID_LEN) == 0 && found->blkid == blkid)
				break;
		}
		if (found != NULL) {
			cache_lru_remove(store, block);
			on_lru = 0;
			atomic_fetch_add(&found->refcount, 1);
		} else {
			TAILQ_INSERT_HEAD(&store->cache_header[indx].head,
			    block, links);
		}
		VAN_MUTEX_UNLOCK(&store->cache_header[indx], mtx_hash, ret1);
		SET_RET(ret1, ret);
		VAN_MUTEX_UNLOCK(&store->cache_lru, mtx_lru, ret1);
		SET_RET(ret1, ret);
		CHK_ERR(ret);

		if (found != NULL) {
			/*
			 * Someone beat us to it, drop our own block.
			 * 'block' now refers to theirs, which we must not
			 * clean up even if a later step fails.
			 */
			_van_ondisk_cache_block_destory(block);
			owned = 0;
			block = found;
		} else {
			/*
			 * Lock order: the tree lock is outermost, it is
			 * taken alone here, nothing else is held.
			 */
			VAN_MUTEX_LOCK(&tree->odfile, mtx_head, ret);
			CHK_ERR(ret);
			TAILQ_INSERT_HEAD(&tree->odfile.head, block,
			    tree_links);
			VAN_MUTEX_UNLOCK(&tree->odfile, mtx_head, ret1);
			SET_RET(ret1, ret);
			CHK_ERR(ret);

			VAN_MUTEX_LOCK(block, mtx_load_cond, ret);
			CHK_ERR(ret);
			block->load = 1;
			VAN_COND_BROADCAST(block, load_cond, ret1);
			SET_RET(ret1, ret);
			VAN_MUTEX_UNLOCK(block, mtx_load_cond, ret1);
			SET_RET(ret1, ret);
			CHK_ERR(ret);
			/*
			 * The block is now reachable from the hash table,
			 * other threads may hold references to it.  It is
			 * no longer ours to destroy on a later failure.
			 */
			owned = 0;
		}
	}

	/*
	 * The block is now fully read, or another thread is reading it.
	 * Only publish the page once that read has finished.
	 */
	if (!block->load) {
		VAN_MUTEX_LOCK(block, mtx_load_cond, ret);
		CHK_ERR(ret);
		while (!block->load) {
			VAN_COND_WAIT(block, load_cond,
			    &block->mtx_load_cond, ret1);
			SET_RET(ret1, ret);
			if (ret != 0)
				break;
		}
		VAN_MUTEX_UNLOCK(block, mtx_load_cond, ret1);
		SET_RET(ret1, ret);
		CHK_ERR(ret);
	}

	*(char **)cachep = block->page;

err:
	if (ret != 0 && owned) {
		/*
		 * Our own block, never published.  Unlink it from the
		 * probation list when it got that far, then free it.
		 * A block that came from another thread (owned == 0) is
		 * left alone, we only hold a reference on it.
		 */
		if (on_lru) {
			VAN_MUTEX_LOCK(&store->cache_lru, mtx_lru, ret1);
			cache_lru_remove(store, block);
			VAN_MUTEX_UNLOCK(&store->cache_lru, mtx_lru, ret1);
		}
		(void)_van_ondisk_cache_block_destory(block);
	}

	return (ret);
}

int _van_ondisk_cache_put(ONDISK_TREE *tree, void *cache) {
	int ret, ret1;
	VAN_STORE *store;
	ONDISK_CACHE_BLOCK *block;

	ret = 0;
	store = tree->table->store;

	block = (ONDISK_CACHE_BLOCK *)((char *)cache -
	    SSZA(ONDISK_CACHE_BLOCK, page));

	VAN_MUTEX_LOCK(&store->cache_header[block->hash_indx],
	    mtx_hash, ret);
	CHK_ERR(ret);
	VAN_ASSERT(atomic_load(&block->refcount) > 0);
	atomic_fetch_sub(&block->refcount, 1);
	VAN_MUTEX_UNLOCK(&store->cache_header[block->hash_indx],
	    mtx_hash, ret1);
	SET_RET(ret1, ret);
	CHK_ERR(ret);

	/*
	 * Reclaim only when the cache is over its budget.  Taking the
	 * global LRU lock on every put would serialize all readers for
	 * no gain in the common case.
	 */
	if (atomic_load(&store->cache_lru.bytes_total) > store->cache_size) {
		VAN_MUTEX_LOCK(&store->cache_lru, mtx_lru, ret);
		CHK_ERR(ret);
		ret1 = cache_evict(store, 0);
		SET_RET(ret1, ret);
		VAN_MUTEX_UNLOCK(&store->cache_lru, mtx_lru, ret1);
		SET_RET(ret1, ret);
	}

err:
	return (ret);
}

/*
 * Drop one block: remove it from the hash table and from the LRU lists,
 * then destroy it.  Used when a tree is closed, the caller has already
 * detached the block from the tree list and may hold the tree lock.
 *
 * Lock order: LRU -> bucket (the tree lock, if held, is outermost).
 */
int _van_ondisk_cache_discard(VAN_STORE *store, ONDISK_CACHE_BLOCK *block) {
	int ret, ret1;
	size_t indx;

	ret = 0;
	if (block == NULL)
		return (ret);
	indx = block->hash_indx;

	VAN_MUTEX_LOCK(&store->cache_lru, mtx_lru, ret);
	CHK_ERR(ret);
	VAN_MUTEX_LOCK(&store->cache_header[indx], mtx_hash, ret1);
	SET_RET(ret1, ret);
	if (ret == 0) {
		TAILQ_REMOVE(&store->cache_header[indx].head, block, links);
		cache_lru_remove(store, block);
	}
	VAN_MUTEX_UNLOCK(&store->cache_header[indx], mtx_hash, ret1);
	SET_RET(ret1, ret);
	VAN_MUTEX_UNLOCK(&store->cache_lru, mtx_lru, ret1);
	SET_RET(ret1, ret);
	CHK_ERR(ret);

	ret = _van_ondisk_cache_block_destory(block);

err:
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
