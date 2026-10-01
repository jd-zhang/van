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
#ifndef _VAN_ONDISK_CACHE_H
#define _VAN_ONDISK_CACHE_H

#if defined(__cplusplus)
extern "C" {
#endif

#define NCACHE_PER_HEADER 10	/* TODO: Use it later */
#define DEF_CACHE_HEADER_SIZE 64	/* default hash table size. */
#define DEF_CACHE_NBLOCK 128  /* Default 128 blocks as cache size */

/*
 * Share of the cache that the protected segment of the segmented LRU is
 * allowed to occupy.  The rest is available to probation, which is where
 * freshly read blocks (and full scans) live.
 */
#define OD_LRU_PROTECTED_PCT	50

struct _ondisk_cache_block {
	TAILQ_ENTRY(_ondisk_cache_block)	links;
	TAILQ_ENTRY(_ondisk_cache_block)	lru_links;
	pthread_mutex_t		mtx_load_cond;
	pthread_cond_t		load_cond;
	int			load;
	size_t		hash_indx;
	atomic_uint		refcount;
	char		fileid[OD_FILEID_LEN];
	uint32		blkid;
	/*
	 * Size of the page in bytes.  It has to be recorded per block:
	 * trees created before this change keep the block size stored in
	 * their own meta data, so blocks of different sizes coexist in
	 * one cache and the size cannot be derived from the tree that
	 * happens to be evicting.
	 */
	uint32		blksize;
	/* 1 when the block sits on the protected list, 0 for probation. */
	int		in_protected;
	char		page[1];
};

struct _ondisk_cache_header {
	pthread_mutex_t	mtx_hash;
	TAILQ_HEAD(_odch, _ondisk_cache_block) head;
};

/*
 * Segmented LRU state.
 *
 * The hash table above is per bucket, this state is global: the
 * replacement policy has to consider every block in the store, not just
 * the ones that happen to collide in one bucket.
 *
 * Lock order (always, in every path):
 *	store->cache_lru.mtx_lru -> store->cache_header[i].mtx_hash
 *
 * A block is reachable only through its hash bucket, so dropping one
 * never leaves a stale pointer behind in another list.
 */
struct _ondisk_cache_lru {
	pthread_mutex_t	mtx_lru;
	/* Eviction candidates, tail is the least recently used. */
	TAILQ_HEAD(_odprob, _ondisk_cache_block) probation;
	/* Blocks seen at least twice, tail is demoted first. */
	TAILQ_HEAD(_odprot, _ondisk_cache_block) protected;

	/* Bytes currently held, probation and protected combined. */
	atomic_size_t	bytes_total;
	/* Bytes currently held by the protected list alone. */
	atomic_size_t	bytes_prot;

	/* Diagnostics, also used by the regression tests. */
	size_t		stat_hits;
	size_t		stat_misses;
	size_t		stat_evictions;
	size_t		stat_promotions;
	size_t		stat_demotions;
};

/*
 * Total bytes occupied by a block.  This mirrors what _van_calloc() is
 * asked for in _van_ondisk_cache_get(), the two have to stay in step.
 */
#define	CACHE_BLOCK_BYTES(b)						\
	(SSZA(ONDISK_CACHE_BLOCK, page) + (size_t)(b)->blksize)

#ifdef __cplusplus
}
#endif

#endif
