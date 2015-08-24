#ifndef _VAN_DECLARE_H
#define _VAN_DECLARE_H

/* From van_datum.h */
typedef struct _van_datum VAN_DATUM;

/* From van_debug.h */
typedef struct _alloc_stat	ALLOC_STAT;

/* From van_inmem_tree.h */
typedef struct _inmem_config		INMEM_CONFIG;
typedef struct _inmem_tree		INMEM_TREE;
typedef struct _inmem_treecursor	INMEM_TREECURSOR;
typedef struct _inmem_treecursor_config	INMEM_TREECURSOR_CONFIG;
typedef struct _inmem_bnode		INMEM_BNODE;
typedef struct _inmem_binternal		INMEM_BINTERNAL;
typedef struct _inmem_bleaf		INMEM_BLEAF;
typedef struct _inmem_stat		INMEM_STAT;
typedef struct _inmem_key		INMEM_KEY;
typedef struct _inmem_data		INMEM_DATA;
typedef struct _inmem_child		INMEM_CHILD;
typedef struct _inmem_rwlock_stat	INMEM_RWLOCK_STAT;
typedef struct _inmem_mutex_stat	INMEM_MUTEX_STAT;

/* From van_mutex.h */
typedef struct _van_rwlock_stat		VAN_RWLOCK_STAT;
typedef struct _van_mutex_stat		VAN_MUTEX_STAT;
typedef struct _van_cond_stat		VAN_COND_STAT;

/* From van_ondisk_cache.h */
typedef struct _ondisk_cache_block	ONDISK_CACHE_BLOCK;
typedef struct _ondisk_cache_header	ONDISK_CACHE_HEADER;

/* From van_ondisk_tree.h */
typedef struct _ondisk_buildinfo	ONDISK_BUILDINFO;
typedef struct _ondisk_meta		ONDISK_META;
typedef struct _ondisk_meta_summary	ONDISK_META_SUMMARY;
typedef struct _ondisk_header		ONDISK_HEADER;
typedef struct _ondisk_item		ONDISK_ITEM;
typedef struct _ondisk_tree		ONDISK_TREE;
typedef struct _ondisk_treecursor	ONDISK_TREECURSOR;
/* From van_operation.h */
typedef struct _van_operation		VAN_OPERATION

/* From van_store.h */
typedef struct _van_store		VAN_STORE;

/* From van_table.h */
typedef struct _van_table		VAN_TABLE;
typedef struct _van_table_odt_item	VAN_TABLE_ODT_ITEM;
typedef struct _van_table_merge_config	VAN_TABLE_MERGE_CONFIG;
typedef struct _van_table_merge_job	VAN_TABLE_MERGE_JOB;
typedef struct _van_table_scan_job	VAN_TABLE_SCAN_JOB;
typedef struct _van_table_merge_join_job	VAN_TABLE_MERGE_JOIN_JOB;
#endif
