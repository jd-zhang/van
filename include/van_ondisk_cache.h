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

#define NCACHE_PER_HEADER 10	/* TODO: Use it later */
#define DEF_CACHE_HEADER_SIZE 64	/* default hash table size. */
#define DEF_CACHE_NBLOCK 128  /* Default 128 blocks as cache size */
struct _ondisk_cache_block {
	TAILQ_ENTRY(_ondisk_cache_block)	links;
	TAILQ_ENTRY(_ondisk_cache_block)	tree_links;
	pthread_mutex_t		mtx_load_cond;
	pthread_cond_t		load_cond;
	int			load;
	size_t		hash_indx;
	uint32		refcount;
	char		fileid[OD_FILEID_LEN];
	uint32		blkid;
	char		page[1];
};

struct _ondisk_cache_header {
	pthread_mutex_t	mtx_hash;
	TAILQ_HEAD(_odch, _ondisk_cache_block) head;
};


#endif
