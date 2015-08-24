#ifndef _VAN_TABLE_H
#define _VAN_TABLE_H

#define MAX_ONDISK_CNT	1000
#define DEFAULT_SCAN_INTERVAL	300

struct _van_table {
	TAILQ_ENTRY(_van_table)	links;
	VAN_STORE	*store;

	INMEM_TREE	*active;
	INMEM_TREE	*frozen;
	const char	*table_name;
	const char	*table_dir;
	const char	*table_odlist;
	const char	*table_odlist_tmp;

	pthread_mutex_t	ticks_mtx;
	ticks_t	cur_ticks;

	pthread_mutex_t	odt_mtx;
	VAN_TABLE_ODT_ITEM	*odt_items;
	size_t odt_top;

	pthread_mutex_t odtpath_mtx;
	char **odtpaths;
	size_t odtpath_cnt;

	/* 
	 * The latest is inserted into tail, we
	 * should notice that.
	 */
	pthread_mutex_t op_mtx;
	TAILQ_HEAD(_ops, _van_operation) operations;
	/* We do not use a separate mute currently. */
	ticks_t	min_op_ticks; 

	VAN_TABLE_SCAN_JOB	*scan_job;
	size_t			nsleep;
	VAN_TABLE_MERGE_JOIN_JOB	*merge_join_job;
	pthread_mutex_t merge_mutex;
	pthread_cond_t	merge_cond;
	TAILQ_HEAD(_merge_jobs, _van_table_merge_job)	merge_jobs;
	TAILQ_HEAD(_merge_jobs, _van_table_merge_job)	free_merge_jobs;
	/* 
	 * The frozen in-memory trees, that has been
	 * dumped, but still being used by some threads.
	 * They can be moved to 'free_tree' when they
	 * are not referenced by any threads.
	 */
	pthread_mutex_t im_dump_mtx;
	TAILQ_HEAD(_im_dump_trees, _inmem_tree)	im_dump_trees;

	/* 
	 * The freed trees, the nodes of which have been
	 * freed, and only leaving the structure, in case
	 * some threads may access it. They can be freed
	 * when all cursors have ticks >= dumped_ticks.
	 */
	pthread_mutex_t im_free_mtx;
	TAILQ_HEAD(_im_free_trees, _inmem_tree)	im_free_trees;

	pthread_mutex_t od_merge_mtx;
	TAILQ_HEAD(_od_merge_trees, _ondisk_tree)	od_merge_trees;	

	pthread_mutex_t od_free_mtx;
	TAILQ_HEAD(_od_free_trees, _ondisk_tree)	od_free_trees;

#define VAN_TABLE_INITED	0x01
#define	VAN_TABLE_OPENED	0x02
	flags_t		flags;
	int	closed;

	size_t		cache_header_size;
	ONDISK_CACHE_HEADER	*cache_header;

	VAN_TABLE_MERGE_CONFIG	merge_cfg;

	int (*close)(VAN_TABLE *table, flags_t flags);
	/* function handles */
	int (*get)(VAN_TABLE *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*put)(VAN_TABLE *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*del)(VAN_TABLE *, VAN_DATUM *, flags_t);
};

struct _van_table_odt_item {
	VAN_ONDISK_TREE *trees[2];

#define	VAN_ITEM_MERGING	0x01
#define VAN_ITEM_MERGED		0x02
#define VAN_ITEM_UNUSED		0x04
#define VAN_ITEM_INUSE		0x08
	flags_t	flags;
	int	ref;
	size_t	table_indx;
	size_t  table_ticks;
	size_t	merged_indx;
	ticks_t	merged_ticks;
	pthread_mutex_t mutex;
};

struct _van_table_merge_config {
	size_t	merge_min;
	size_t	merge_max;
};

struct _van_table_merge_job {
	TAILQ_ENTRY(_van_table_merge_job)	links;
	size_t	low;
	size_t	high;
	size_t	cnt;
	ticks_t	min_ticks;
	ticks_t	max_ticks;
#define MERGE_JOG_RUNNING 0x01
#define MERGE_JOB_FINISHED 0x02
	flags_t	flags;
	pthread_t	thread_id;
	VAN_TABLE	*table;
};

struct _van_table_scan_job {
	size_t nsleep;
	pthread_t thread_id;
	VAN_TABLE *table;
}

/* No other fields currently */
struct _van_table_merge_join_job {
	VAN_TABLE	*table;
	pthread_t	thread_id;
};
#endif
