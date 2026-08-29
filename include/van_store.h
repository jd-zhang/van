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
#ifndef _VAN_STORE_H
#define _VAN_STORE_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "van.h"

struct _van_store_idmap {
	uint32	tid;
	VAN_TABLE *table;
	int	nextfree;
};

#define PREALLOC_TIDNUM 100
struct _van_store {
	/* size_t cache_t_size; */
	char *store_dir;
	char *store_name; /* No real usage currently. */

	pthread_mutex_t tb_mtx;
	TAILQ_HEAD(_tables, _van_table) tables;

#define VAN_STORE_INITED	0x01
#define VAN_STORE_CACHE_INITED	0x02
#define	VAN_STORE_OPENED	0x04
	flags_t		flags;

	/* open_flags */
	flags_t		open_flags;

	size_t		cache_header_size;
	ONDISK_CACHE_HEADER	*cache_header;
	size_t		cache_size;
	size_t		ondisk_blksize;

	/* table ids */
	pthread_mutex_t tid_mtx;
	VAN_STORE_IDMAP  prealloc_tids[PREALLOC_TIDNUM];
	VAN_STORE_IDMAP *tids;
	size_t		tid_capacity;
	size_t		tid_cnt;
	int		tid_free;

	/* log */
	VAN_LOG		*log_handle;

	/* Transaction id allocation, store-wide (unique across tables) */
	atomic_uint	txnid;

	/* Recovery */
	VAN_DISTAB   recover_dtab;

	/* Functions handles */
	int (*open)(VAN_STORE *store, const char *dir, flags_t flags);
	int (*close)(VAN_STORE *store);
	int (*set_name)(VAN_STORE *store, const char *name);
	int (*set_flags)(VAN_STORE *store, flags_t flags);
	int (*get_flags)(VAN_STORE *store, flags_t *flagsp);
	int (*set_cache_headersize)(VAN_STORE *, size_t headersize);
	int (*get_cache_headersize)(VAN_STORE *, size_t *headersizep);
	int (*set_cachesize)(VAN_STORE *, size_t cachesize);
	int (*get_cachesize)(VAN_STORE *, size_t *cachesizep);
	int (*set_ondisk_blksize)(VAN_STORE *, size_t blksize);
	int (*get_ondisk_blksize)(VAN_STORE *, size_t *blksizep);

	/* Debug use only */
	/* ************** */
	/* ************** */
	int (*printinfo)(VAN_STORE *store, FILE* outf, flags_t flags);
};

#ifdef __cplusplus
}
#endif

#endif
