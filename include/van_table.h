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
#ifndef _VAN_TABLE_H
#define _VAN_TABLE_H

#if defined(__cplusplus)
extern "C" {
#endif

#define MAX_ONDISK_CNT	1000
#define DEFAULT_SCAN_INTERVAL	10

#define VAN_TABLE_MERGE_MIN 4
#define VAN_TABLE_MERGE_MAX 10

struct _van_table_merge_config {
	size_t	merge_min;
	size_t	merge_max;
};

struct _van_table {
	TAILQ_ENTRY(_van_table)	links;
	VAN_STORE	*store;
	uint32		tid;

	INMEM_TREE	*active;
	INMEM_TREE	*frozen;
	char	*table_name;
	char	*table_dir;

	pthread_mutex_t	ticks_mtx;
	ticks_t	cur_ticks;


	pthread_mutex_t mergecnt_mtx;
	int mergecnt;

	pthread_mutex_t	odt_mtx;
	VAN_TABLE_ODT_ITEM	*odt_items;
	size_t odt_top;

	char	*table_odlist;
	char	*table_odlist_tmp;

	pthread_mutex_t odtpath_mtx;
	char **odtpaths;
	size_t odtpath_cnt;
	size_t odtpath_capacity;

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

	pthread_mutex_t merge_mutex;
	TAILQ_HEAD(_merge_jobs, _van_table_merge_job)	merge_jobs;
	VAN_TABLE_MERGE_CONFIG	merge_cfg;
	/* NOT USED CURRENTLY */
	VAN_TABLE_MERGE_JOIN_JOB	*merge_join_job;
	pthread_cond_t	merge_join_cond;
	TAILQ_HEAD(_free_merge_jobs, _van_table_merge_job)	free_merge_jobs;

	pthread_mutex_t	dump_mutex;
	TAILQ_HEAD(_dump_jobs, _van_table_dump_job)	dump_jobs;
	pthread_cond_t	dump_join_cond;
	VAN_TABLE_DUMP_JOIN_JOB	*dump_join_job;

	/*
	 * The frozen in-memory trees, that has been
	 * dumped, but still being used by some threads.
	 * They can be moved to 'free_tree' when they
	 * are not referenced by any threads.
	 */
	pthread_mutex_t im_dump_mtx;
	TAILQ_HEAD(_im_dump_trees, _inmem_tree)	im_dump_trees;
	VAN_TABLE_FROZEN_FREE_JOB *frozen_free_job;

	/*
	 * The freed trees, the nodes of which have been
	 * freed, and only leaving the structure, in case
	 * some threads may access it. They can be freed
	 * when all cursors have ticks >= dumped_ticks.
	 */

	/* Not used currently */
	/*
	pthread_mutex_t im_free_mtx;
	TAILQ_HEAD(_im_free_trees, _inmem_tree)	im_free_trees;

	pthread_mutex_t od_merge_mtx;
	TAILQ_HEAD(_od_merge_trees, _ondisk_tree)	od_merge_trees;

	pthread_mutex_t od_free_mtx;
	TAILQ_HEAD(_od_free_trees, _ondisk_tree)	od_free_trees;
	*/

#define VAN_TABLE_INITED	0x01
#define	VAN_TABLE_OPENED	0x02
	flags_t		flags;
	int	closed;

#define VAN_TABLE_SKIP_THREAD_START 0x01
	flags_t		open_flags;

	VAN_TABLE_CMPFUNC cmp_func;
	int (*open)(VAN_TABLE *, const char *, flags_t);
	int (*close)(VAN_TABLE *, flags_t);
	/* function handles */
	int (*get)(VAN_TABLE *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*put)(VAN_TABLE *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*del)(VAN_TABLE *, VAN_DATUM *, flags_t);
};

struct _van_table_odt_item {
	ONDISK_TREE *trees[2];

#define	VAN_ITEM_MERGING	0x01
#define VAN_ITEM_MERGED		0x02
#define VAN_ITEM_UNUSED		0x04
#define VAN_ITEM_INUSE		0x08
#define VAN_ITEM_DUMPING	0x10
	flags_t	flags;
	int	ref;
	size_t	indx;
	ticks_t	ticks;
	size_t	merged_indx;
	ticks_t	merged_ticks;
	pthread_mutex_t mutex;
};

struct _van_table_merge_job {
	TAILQ_ENTRY(_van_table_merge_job)	links;
	size_t	low;
	size_t	high;
	size_t	cnt;
	ticks_t	min_ticks;
	ticks_t	max_ticks;
	pthread_t	thread_id;
	VAN_TABLE	*table;
	flags_t	flags;
	int finished;
};

struct _van_table_scan_job {
	size_t		nsleep;
	pthread_t	thread_id;
	VAN_TABLE	*table;
	flags_t		flags;
	int		finished;
};

/* No other fields currently */
struct _van_table_merge_join_job {
	VAN_TABLE	*table;
	pthread_t	thread_id;
	flags_t		flags;
	int		finished;
};

struct _van_table_dump_job {
	TAILQ_ENTRY(_van_table_dump_job)	links;
	VAN_TABLE	*table;
	pthread_t	thread_id;
	flags_t		flags;
	int		finished;
};

struct _van_table_dump_join_job {
	VAN_TABLE *table;
	pthread_t	thread_id;
	flags_t		flags;
	int		finished;
};

struct _van_table_frozen_free_job {
	VAN_TABLE *table;
	pthread_t thread_id;
	flags_t flags;
	int finished;
};

#ifdef __cplusplus
}
#endif

#endif
