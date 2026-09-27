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
#ifndef _VAN_DECLARE_H
#define _VAN_DECLARE_H

#if defined(__cplusplus)
extern "C" {
#endif

/* From van_common.h */
typedef struct _van_string_array VAN_STRING_ARRAY;

/* From van_datum.h */
typedef struct _van_datum VAN_DATUM;

/* From van_debug.h */
typedef struct _alloc_stat	ALLOC_STAT;

/* From van_inmem_tree.h */
typedef struct _inmem_config		INMEM_CONFIG;
typedef struct _inmem_tree		INMEM_TREE;
typedef struct _inmem_treecursor	INMEM_TREECURSOR;
typedef struct _inmem_bnode		INMEM_BNODE;
typedef struct _inmem_binternal		INMEM_BINTERNAL;
typedef struct _inmem_bleaf		INMEM_BLEAF;
typedef struct _inmem_stat		INMEM_STAT;
typedef struct _inmem_key		INMEM_KEY;
typedef struct _inmem_data		INMEM_DATA;
typedef struct _inmem_child		INMEM_CHILD;

/* From van_io.h */
typedef struct _van_iofile	VAN_IOFILE;

/* From van_lock.h */
typedef struct _van_lockobj	VAN_LOCKOBJECT;
typedef struct _van_locker	VAN_LOCKER;
typedef struct _van_lock	VAN_LOCK;

/* From van_log.h */
typedef struct _van_logfile_header	VAN_LOGFILE_HEADER;
typedef struct _van_logrec_header	VAN_LOGREC_HEADER;
typedef struct _van_logrec_common	VAN_LOGREC_COMMON;
typedef struct _van_log			VAN_LOG;
typedef struct _van_logfile_info	VAN_LOGFILE_INFO;
typedef struct _van_log_cursor		VAN_LOG_CURSOR;
typedef struct _van_log_recspec		VAN_LOG_RECSPEC;

/* From van_mutex.h */
typedef struct _van_rwlock_stat		VAN_RWLOCK_STAT;
typedef struct _van_mutex_stat		VAN_MUTEX_STAT;
typedef struct _van_cond_stat		VAN_COND_STAT;

/* From van_ondisk_cache.h */
typedef struct _ondisk_cache_block	ONDISK_CACHE_BLOCK;
typedef struct _ondisk_cache_header	ONDISK_CACHE_HEADER;
typedef struct _ondisk_cache_lru	ONDISK_CACHE_LRU;

/* From van_ondisk_tree.h */
typedef struct _ondisk_buildinfo	ONDISK_BUILDINFO;
typedef struct _ondisk_meta		ONDISK_META;
typedef struct _ondisk_meta_summary	ONDISK_META_SUMMARY;
typedef struct _ondisk_header		ONDISK_HEADER;
typedef struct _ondisk_item		ONDISK_ITEM;
typedef struct _ondisk_file		ONDISK_FILE;
typedef struct _ondisk_tree		ONDISK_TREE;
typedef struct _ondisk_treecursor	ONDISK_TREECURSOR;
/* From van_operation.h */
typedef struct _van_operation		VAN_OPERATION;

/* From van_store.h */
typedef struct _van_store		VAN_STORE;
typedef struct _van_store_idmap		VAN_STORE_IDMAP;

/* From van_table.h */
typedef struct _van_table		VAN_TABLE;
typedef struct _van_table_odt_item	VAN_TABLE_ODT_ITEM;
typedef struct _van_table_merge_config	VAN_TABLE_MERGE_CONFIG;
typedef struct _van_table_merge_job	VAN_TABLE_MERGE_JOB;
typedef struct _van_table_scan_job	VAN_TABLE_SCAN_JOB;
typedef struct _van_table_merge_join_job	VAN_TABLE_MERGE_JOIN_JOB;
typedef struct _van_table_dump_job	VAN_TABLE_DUMP_JOB;
typedef struct _van_table_dump_join_job	VAN_TABLE_DUMP_JOIN_JOB;
typedef struct _van_table_frozen_free_job	VAN_TABLE_FROZEN_FREE_JOB;

/* From van_txn.h */
typedef struct _van_txn			VAN_TXN;

#ifdef __cplusplus
}
#endif

#endif
