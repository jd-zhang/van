#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"
#include "van_ondisk_tree.h"

int _van_table_create(VAN_STORE *store, VAN_TABLE **tablep) {
	int ret;
	VAN_TABLE *table;

	VAN_ASSERT(tablep != NULL);
	table = NULL;
	ret = _van_calloc(NULL, 1, sizeof(VAN_TABLE), &table);
	CHK_ERR(ret);

	table->store = store;
	ret = _van_table_init(table);
	if (ret == 0)
		*tablep = table;
err:
	return (ret);
}

int _van_table_init(VAN_TABLE *table) {
	int ret;
	size_t i, odt_cnt;
	VAN_TABLE_ODT_ITEM *item;

	ret = 0;
	odt_cnt = MAX_ONDISK_CNT;

	table->put = _van_table_put_record;
	table->get = _van_table_get_record;
	table->del = _van_table_del_record;
	table->close = _van_table_close;

	table->active = NULL;
	table->frozen = NULL;
	table->table_name = NULL;
	table->table_dir = NULL;
	VAN_MUTEX_INIT(table, ticks_mtx, ret);
	CHK_ERR(ret);
	table->cur_ticks = 0;

	VAN_MUTEX_INIT(table, odt_mtx, ret);
	CHK_ERR(ret);
	ret = _van_calloc(NULL, odt_cnt, 
	    sizeof(VAN_TABLE_ODT_ITEM), &table->odt_items);
	CHK_ERR(ret);
	for (i = 0; i < odt_cnt; i++) {
		item = &table->odt_items[i];
		VAN_MUTEX_INIT(item, mutex, ret);
		CHK_ERR(ret);
		F_SET(item, VAN_ITEM_UNUSED);
	}

	table->odtpath_mtx = 0;
	ret = _van_calloc(NULL, odt_cnt, sizeof(char *), &table->odtpaths);

	VAN_MUTEX_INIT(table, op_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->operations);
	table->min_op_ticks = MAX_TICKS;

	table->nsleep = DEFAULT_SCAN_INTERVAL;
	table->scan_job = NULL;
	table->merge_free_job = NULL;
	VAN_MUTEX_INIT(table, merge_mutex, ret);
	CHK_ERR(ret);
	VAN_COND_INIT(table, merge_cond, ret);
	TAILQ_INIT(&table->merge_jobs);
	TAILQ_INIT(&table->free_merge_jobs);

	VAN_MUTEX_INIT(table, im_dump_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->im_dump_trees);

	VAN_MUTEX_INIT(table, im_free_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->im_free_trees);

	VAN_MUTEX_INIT(table, od_merge_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->od_merge_trees);

	VAN_MUTEX_INIT(table, od_free_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->od_free_trees);

	F_SET(table, VAN_TABLE_INITED);
	table->closed = 1;

	/* The cache header is not inited here */
	/* The merge configuration should be added later. */
err:
	return (ret);
}

int _van_table_open(VAN_TABLE *table, const char *tb_name, flags_t flags) {
	int ret;
	VAN_STORE *store;
	char buf[VAN_MAXPATHLEN];

	store = table->store;
	/* TODO: error handling. */
	ret = _van_strdup(NULL, tb_name, &table->table_name);
	CHK_ERR(ret);

	(void)sprintf(buf, "%s.odlist", tb_name);
	ret = _van_strdup(NULL, buf, &table->table_odlist);
	CHK_ERR(ret);

	(void)sprintf(buf, "%s.odlist.temp", tb_name);
	ret = _van_strdup(NULL, buf, &table->table_odlist_tmp);
	CHK_ERR(ret);

	table->frozen = NULL;
	ret = _van_inmemtree_create(table, &table->active);
	CHK_ERR(ret);
	ret = _van_table_read_odt_items(table);
	CHK_ERR(ret);

	F_SET(table, VAN_TABLE_OPENED);
	table->closed = 0;

	/* TODO: Create the scan job */
	ret = _van_table_merge_scan_init(table);
	CHK_ERR(ret);
	ret = _van_table_merge_join_init(table);
	CHK_ERR(ret);

	/* Now add it to the store's table list. */
	VAN_MUTEX_LOCK(store, tb_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INSERT_TAIL(&store->tables, table, links);
	VAN_MUTEX_UNLOCK(store, tb_mtx, ret);
err:
	return (ret);
}

int _van_table_close(VAN_TABLE *table, flags_t flags) {
	int ret, ret1;
	void *rptr;

	rptr = NULL;
	table->closed = 1;
	/* TODO: Wait for threads to finish. */
	pthread_join(table->merge_free_job->thread_id, &rptr);
	ret1 = (int)rptr;
	SET_RET(ret1, ret);
	_van_free(NULL, table->merge_free_job);
	table->merge_free_job = NULL;

	rptr = NULL;
	pthread_join(table->scan_job->thread_id, &rptr);
	ret1 = (int)rptr;
	SET_RET(ret1, ret);
	_van_free(NULL, table->scan_job);
	table->scan_job = NULL;

	ret1 = _van_table_merge_clean(table);
	SET_RET(ret1, ret);

	if (table->table_name != NULL) {
		_van_free(NULL, table->table_name);
		table->table_name = NULL;
	}

	if (table->table_dir != NULL) {
		_van_free(NULL, table->table_dir);
		table->table_dir = NULL;
	}

	if (table->table_odlist != NULL) {
		_van_free(NULL, table->table_odlist);
		table->table_odlist = NULL;
	}

	if (table->table_odlist_tmp != NULL) {
		_van_free(NULL, table->table_odlist_tmp);
		table->table_odlist_tmp = NULL;
	}

	table->cur_ticks = 0;
	VAN_MUTEX_DESTROY(table, ticks_mtx, ret1);
	SET_RET(ret1, ret);

	_van_free(NULL, table->odt_items);
	table->odt_items = NULL;
	table->odt_top = 0;
	VAN_MUTEX_DESTROY(table, odt_mtx, ret1);
	SET_RET(ret1, ret);

	_van_free(NULL, table->odtpaths);
	table->odtpath_cnt = 0;
	VAN_MUTEX_DESTROY(table, odtpath_mtx, ret1);
	SET_RET(ret1, ret);

	/* TODO: check if table->operations is empty */
	VAN_MUTEX_DESTROY(table, op_mtx, ret1);
	SET_RET(ret1, ret);
	table->min_op_ticks = MAX_TICKS;

	/* TODO: check if table->merge_jobs is empty */
	VAN_MUTEX_DESTROY(table, merge_mutex, ret1);
	SET_RET(ret1, ret);
	VAN_COND_DESTROY(table, merge_cond, ret1);
	SET_RET(ret1, ret);

	/* TODO: check if table->im_dump_trees is empty */
	VAN_MUTEX_DESTROY(table, im_dump_mtx, ret1);
	SET_RET(ret1, ret);

	/* TODO: check if table->im_free_trees is empty */
	VAN_MUTEX_DESTROY(table, im_free_mtx, ret1);
	SET_RET(ret1, ret);

	/* TODO: check if table->od_merge_trees is empty */
	VAN_MUTEX_DESTROY(table, od_merge_mtx, ret1);
	SET_RET(ret1, ret);

	/* TODO: check if table->od_free_trees is empty */
	VAN_MUTEX_DESTROY(table, od_free_mtx, ret1);
	SET_RET(ret1, ret);

	F_CLR(table, VAN_TABLE_INITED | VAN_TABLE_OPENED);

	return ret;
}

int _van_table_curticks(VAN_TABLE *table, ticks_t *tkp) {
	int ret;
	ticks_t tk;

	VAN_ASSERT(tkp != NULL);
	ret = 0;

	VAN_MUTEX_LOCK(table, ticks_mtx, ret);
	CHK_ERR(ret);
	tk = table->cur_ticks;
	table->cur_ticks++;
	VAN_MUTEX_UNLOCK(table, ticks_mtx, ret);
	if (ret == 0)
		*tkp = tk;

err:
	return ret;
}

/* 
 * Current the file is with lines of the ondisk 
 * tree files. From lowest ticks to highest ticks.
 */
int _van_table_read_odt_items(VAN_TABLE *table) {
	FILE *f;
	int ret;
	char buf[VAN_MAXPATHLEN], *odtlist;
	size_t cindx;
	VAN_ONDISK_TREE *odtree;
	VAN_TABLE_ODT_ITEM *citem;
	ticks_t curticks;

	/* 
	 * Read table->tb_name line by line to
	 * get the ondisk tree files, and use
	 * the files to initialize the ondisk
	 * tree structures.
	 */
	odtlist = NULL;
	ret = 0;
	cindx = 0;
	odtree = NULL;
	f = NULL;

	ret = _van_table_path(table, table->table_odlist, &odtlist);
	if (ret != 0 || odtlist == NULL)
		goto err;
	curticks = MAX_ONDISK_CNT;
	curticks = -curticks;

	ret = _van_file_fopen(odtlist, "r", &f);
	CHK_ERR(ret);

	while ((ret = _van_file_freadline(f, buf, VAN_MAXPATHLEN)) == 0) {
		if (cindx >= MAX_ONDISK_CNT) {
			ret = VAN_OVERFLOW;
			goto err;
		}
		citem = &table->odt_items[cindx];
		ret = _van_ondisktree_create(table, &odtree);
		CHK_ERR(ret);
		ret = _van_ondisktree_open(odtree, buf, 0);
		CHK_ERR(ret);
		ret = _van_strdup(NULL, buf, &table->odtpaths[cindx]);

		citem->trees[0] = odtree;
		/* Now we set the flags. */
		F_SET(citem, VAN_ITEM_INUSE);
		citem->ref = 0;
		citem->table_indx = cindx;
		citem->merged_indx = 0;
		citem->merged_ticks = curticks; /* Can be read by any. */
		curticks++;
		cindx++;
	}
	ret = _van_file_close(f);
	CHK_ERR(ret);
	table->odt_top = cindx;
	table->odtpath_cnt = cindx;
err:
	if (odtlist != NULL)
		_van_free(NULL, odtlist);
	if (ret == ENOENT || ret == VAN_NOTFOUND)
		ret = 0;
	return ret;
}

/* 
 * Used to store the file after a compaction finished.
 */
int _van_table_write_odt_items(VAN_TABLE *table) {
	int i, ret;
	FILE *f;
	char *odtlist, *tmplist;

	ret = _van_table_path(table, table->table_odlist, &odtlist);
	CHK_ERR(ret);
	ret = _van_table_path(table, table->table_odlist_tmp, &tmplist);
	CHK_ERR(ret);

	ret = _van_file_fopen(tmplist, "w", &f);
	CHK_ERR(ret);

	VAN_MUTEX_LOCK(table, odtpath_mtx, ret);
	for (i = 0; i <= table->odtpath_cnt; i++) {
		ret = _van_file_fwriteline(f, table->odtpaths[i], 0);
		CHK_ERR(ret);
	}
	ret = _van_file_fclose(f);
	CHK_ERR(ret);
	ret = _van_file_rename(tmplist, odtlist);
	CHK_ERR(ret);
	VAN_MUTEX_UNLOCK(table, odtpath_mtx, ret);
err:
	return ret;
	
}

int _van_table_put_record(VAN_TABLE *table, VAN_DATUM *key, 
    VAN_DATUM *data, flags_t flags) {
	int ret;
	VAN_OPERATION *op;

	ret = _van_table_op_create(table, &op, 0);
	CHK_ERR(ret);

	ret = _van_table_op_put(op, key, data, flags);
	CHK_ERR(ret);

	ret = _van_table_op_close(op);

	return (ret);
}

int _van_table_get_record(VAN_TABLE *table, VAN_DATUM *key,
    VAN_DATUM *data, flags_t flags) {
	int ret;
	VAN_OPERATION *op;

	ret = _van_table_op_create(table, &op, 0);
	CHK_ERR(ret);

	ret = _van_table_op_get(op, key, data, flags);
	CHK_ERR(ret);

	ret = _van_table_op_close(op);

	return (ret);

}

int _van_table_del_record(VAN_TABLE *table, VAN_DATUM *key, flags_t flags) {
	int ret;
	VAN_OPERATION *op;

	ret = _van_table_op_create(table, &op, 0);
	CHK_ERR(ret);

	ret = _van_table_op_del(op, key, flags);
	CHK_ERR(ret);

	ret = _van_table_op_close(op);

	return (ret);
}
