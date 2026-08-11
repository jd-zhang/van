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
	table->open = _van_table_open;
	table->close = _van_table_close;
	table->cmp_func = _van_table_datum_defcmp;

	table->active = NULL;
	table->frozen = NULL;
	table->table_name = NULL;
	table->table_dir = NULL;
	VAN_MUTEX_INIT(table, ticks_mtx, ret);
	CHK_ERR(ret);
	table->cur_ticks = 0;

	VAN_MUTEX_INIT(table, mergecnt_mtx, ret);
	table->mergecnt = 0;

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

	VAN_MUTEX_INIT(table, odtpath_mtx, ret);
	CHK_ERR(ret);
	ret = _van_calloc(NULL, odt_cnt, sizeof(char *), &table->odtpaths);
	table->odtpath_capacity = odt_cnt;

	table->table_odlist = NULL;
	table->table_odlist_tmp = NULL;

	VAN_MUTEX_INIT(table, op_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->operations);
	table->min_op_ticks = MAX_TICKS;

	table->nsleep = DEFAULT_SCAN_INTERVAL;
	table->scan_job = NULL;

	VAN_MUTEX_INIT(table, merge_mutex, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->merge_jobs);
	/* Use default temporarily. */
	table->merge_cfg.merge_min = VAN_TABLE_MERGE_MIN;
	table->merge_cfg.merge_max = VAN_TABLE_MERGE_MAX;

	/* INIT THE UNUSED */
	VAN_COND_INIT(table, merge_join_cond, ret);
	TAILQ_INIT(&table->free_merge_jobs);
	table->merge_join_job = NULL;

	VAN_MUTEX_INIT(table, dump_mutex, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->dump_jobs);
	VAN_COND_INIT(table, dump_join_cond, ret);
	CHK_ERR(ret);
	table->dump_join_job = NULL;

	/* Notice the following ! */
	VAN_MUTEX_INIT(table, im_dump_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->im_dump_trees);
	table->frozen_free_job = NULL;

	/* Not used currently */
	/*
	VAN_MUTEX_INIT(table, im_free_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->im_free_trees);

	VAN_MUTEX_INIT(table, od_merge_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->od_merge_trees);

	VAN_MUTEX_INIT(table, od_free_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INIT(&table->od_free_trees);
	*/

	F_SET(table, VAN_TABLE_INITED);
	table->closed = 0;

	/* The cache header is not inited here */
	/* The merge configuration should be added later. */
err:
	return (ret);
}

int _van_table_open(VAN_TABLE *table, const char *tb_name, flags_t flags) {
	int ret;
	VAN_STORE *store;
	flags_t orig_oflags;
	char buf[VAN_MAXPATHLEN];

	store = table->store;
	orig_oflags = flags;

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
	ret = _van_table_read_odt_paths(table);
	CHK_ERR(ret);

	F_SET(table, VAN_TABLE_OPENED);
	table->closed = 0;

	/* TODO: Create the scan job */
	if (!LF_ISSET(VAN_TABLE_SKIP_THREAD_START)) {
		ret = _van_table_merge_scan_init(table);
		CHK_ERR(ret);

		ret = _van_table_dump_join_init(table);
		CHK_ERR(ret);

		ret = _van_table_frozen_free_init(table);
		CHK_ERR(ret);

		/* NOT USED CURRENTLY */
		/*
		ret = _van_table_merge_join_init(table);
		CHK_ERR(ret);
		 */
	}

	table->open_flags = orig_oflags;

	/* Now add it to the store's table list. */
	VAN_MUTEX_LOCK(store, tb_mtx, ret);
	CHK_ERR(ret);
	TAILQ_INSERT_TAIL(&store->tables, table, links);
	VAN_MUTEX_UNLOCK(store, tb_mtx, ret);

	ret = _van_store_id_bind(store, table);
err:
	return (ret);
}

int _van_table_close(VAN_TABLE *table, flags_t flags) {
	int ret, ret1;
	void *rptr;
	size_t i;
	VAN_TABLE_ODT_ITEM *item;
	ONDISK_TREE *odtree;
	VAN_STORE *store;

	store = table->store;
	/* fprintf(stderr, "table->closed=1\n"); */
	table->closed = 1;
	ret = 0;

	/* 
	 * It is always NULL currently, so leave it does not
	 * have any effect.
	 */

	VAN_COND_SIGNAL(table, dump_join_cond, ret);
	VAN_COND_SIGNAL(table, merge_join_cond, ret);

	rptr = NULL;
	if (table->merge_join_job != NULL) {
		pthread_join(table->merge_join_job->thread_id, &rptr);
		ret1 = (int)(uintmax_t)rptr;
		SET_RET(ret1, ret);
		_van_free(NULL, table->merge_join_job);
		table->merge_join_job = NULL;
	}

	rptr = NULL;
	if (table->scan_job != NULL) {
		pthread_join(table->scan_job->thread_id, &rptr);
		ret1 = (int)(uintmax_t)rptr;
		SET_RET(ret1, ret);
		_van_free(NULL, table->scan_job);
		table->scan_job = NULL;
	}

	rptr = NULL;
	if (table->dump_join_job != NULL) {
		pthread_join(table->dump_join_job->thread_id, &rptr);
		ret1 = (int)(uintmax_t)rptr;
		SET_RET(ret1, ret);
		_van_free(NULL, table->dump_join_job);
		table->dump_join_job = NULL;
	}

	rptr = NULL;
	if (table->frozen_free_job != NULL) {
		pthread_join(table->frozen_free_job->thread_id, &rptr);
		ret1 = (int)(uintmax_t)rptr;
		SET_RET(ret1, ret);
		_van_free(NULL, table->frozen_free_job);
		table->frozen_free_job = NULL;
	}

	/* 
	 * This is unnecessary now, since the merge_join_job
	 * will only return after graping all the merge jobs.
	 */
	/*
	ret1 = _van_table_merge_clean(table);
	SET_RET(ret1, ret);
	*/

	/* handle the table->active and table->frozen */
	ret1 = _van_table_dump_inmem(table);
	SET_RET(ret1, ret);
	
	ret1 = _van_table_write_odt_paths(table);
	SET_RET(ret1, ret);

	for (i = 0; i < table->odt_top; i++) {
		item = &table->odt_items[i];
		odtree = item->trees[0];
		item->trees[0] = NULL;
		if (odtree != NULL) {
			ret1 = _van_ondisktree_close(odtree, 0);
			SET_RET(ret1, ret);
		}
	}

	/* TODO: handle the ondisktree close */

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

	VAN_MUTEX_DESTROY(table, mergecnt_mtx, ret1);
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

	/*
	 * TODO: check if table->merge_jobs and
	 * table->free_merge_jobs is empty.
	 * */
	VAN_MUTEX_DESTROY(table, merge_mutex, ret1);
	SET_RET(ret1, ret);
	VAN_COND_DESTROY(table, merge_join_cond, ret1);
	SET_RET(ret1, ret);

	VAN_MUTEX_DESTROY(table, dump_mutex, ret1);
	SET_RET(ret1, ret);
	VAN_COND_DESTROY(table, dump_join_cond, ret1);
	SET_RET(ret1, ret);

	/* TODO: check if table->im_dump_trees is empty */
	VAN_MUTEX_DESTROY(table, im_dump_mtx, ret1);
	SET_RET(ret1, ret);

	/* TODO: check if table->im_free_trees is empty */
	/* TODO: check if table->od_merge_trees is empty */
	/* TODO: check if table->od_free_trees is empty */
	/* Not used currently */
	/*
	VAN_MUTEX_DESTROY(table, im_free_mtx, ret1);
	SET_RET(ret1, ret);
	VAN_MUTEX_DESTROY(table, od_merge_mtx, ret1);
	SET_RET(ret1, ret);
	VAN_MUTEX_DESTROY(table, od_free_mtx, ret1);
	SET_RET(ret1, ret);
	*/

	F_CLR(table, VAN_TABLE_INITED | VAN_TABLE_OPENED);

	/* Now add it to the store's table list. */
	VAN_MUTEX_LOCK(store, tb_mtx, ret);
	CHK_ERR(ret);
	TAILQ_REMOVE(&store->tables, table, links);
	VAN_MUTEX_UNLOCK(store, tb_mtx, ret);

	ret = _van_store_id_unbind(store, table);
	_van_free(NULL, table);
err:
	return ret;
}

int _van_table_start_threads(VAN_TABLE *table) {
	int ret;

	if (table->scan_job == NULL) {
		ret = _van_table_merge_scan_init(table);
		CHK_ERR(ret);
	}

	if (table->dump_join_job == NULL) {
		ret = _van_table_dump_join_init(table);
		CHK_ERR(ret);
	}

	if (table->frozen_free_job == NULL) {
		ret = _van_table_frozen_free_init(table);
		CHK_ERR(ret);
	}

	/* NOT USED CURRENTLY */
	/*
	if (table->merge_join_job == NULL) {
		ret = _van_table_merge_join_init(table);
		CHK_ERR(ret);
	}
	*/
err:
	return ret;

}

int _van_table_set_flags(VAN_TABLE *table, flags_t flags) {
	int ret;
	ret = 0;

	table->flags |= flags;

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
int _van_table_read_odt_paths(VAN_TABLE *table) {
	FILE *f;
	int ret;
	char buf[VAN_MAXPATHLEN], *odtlist;
	size_t cindx;
	ONDISK_TREE *odtree;
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
	curticks = table->odtpath_capacity;
	curticks = -curticks;

	ret = _van_file_fopen(odtlist, "r", &f);
	CHK_ERR(ret);

	while ((ret = _van_file_freadline(f, buf, VAN_MAXPATHLEN)) == 0) {
		if (cindx >= table->odtpath_capacity) {
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
		odtree = NULL;
		/* Now we set the flags. */
		/* citem->mutex has been already initialized. */
		citem->flags = 0;
		F_SET(citem, VAN_ITEM_INUSE);
		citem->ref = 0;
		citem->indx = cindx;
		citem->ticks = curticks; /* Can be read by any. */
		/* merged_ticks and merged_indx should not be set. */
		curticks++;
		cindx++;
	}
	ret = _van_file_fclose(f);
	CHK_ERR(ret);
	table->odtpath_cnt = cindx;
	table->odt_top = cindx;
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
int _van_table_write_odt_paths(VAN_TABLE *table) {
	int i, ret, ret1, locked;
	FILE *f;
	char *odtlist, *tmplist;

	locked = 0;
	odtlist = NULL;
	tmplist = NULL;
	ret = _van_table_path(table, table->table_odlist, &odtlist);
	CHK_ERR(ret);
	ret = _van_table_path(table, table->table_odlist_tmp, &tmplist);
	CHK_ERR(ret);

	VAN_MUTEX_LOCK(table, odtpath_mtx, ret);
	CHK_ERR(ret);
	locked = 1;

	ret = _van_file_fopen(tmplist, "w", &f);
	CHK_ERR(ret);

	for (i = 0; i < table->odtpath_cnt; i++) {
		ret = _van_file_fwriteline(f, table->odtpaths[i], 0);
		CHK_ERR(ret);
	}
	ret = _van_file_fclose(f);
	CHK_ERR(ret);
	ret = _van_file_rename(tmplist, odtlist);
	CHK_ERR(ret);

err:
	if (locked) {
		VAN_MUTEX_UNLOCK(table, odtpath_mtx, ret1);
		SET_RET(ret1, ret);
	}
	if (odtlist != NULL)
		_van_free(NULL, odtlist);
	if (tmplist != NULL)
		_van_free(NULL, tmplist);

	return ret;
	
}

int _van_table_put_record(VAN_TABLE *table, VAN_DATUM *key,
    VAN_DATUM *data, flags_t flags) {
	int ret, ret1;
	VAN_OPERATION *op;

	ret = _van_table_op_create(table, &op, 0);
	CHK_ERR(ret);

	ret = _van_table_op_put(op, key, data, flags);
	ret1 = _van_table_op_close(op);
	SET_RET(ret1, ret);
err:
	return (ret);
}

int _van_table_get_record(VAN_TABLE *table, VAN_DATUM *key,
    VAN_DATUM *data, flags_t flags) {
	int ret, ret1;
	VAN_OPERATION *op;

	ret = _van_table_op_create(table, &op, 0);
	CHK_ERR(ret);

	ret = _van_table_op_get(op, key, data, flags);
	ret1 = _van_table_op_close(op);
	SET_RET(ret1, ret);
	if (ret == VAN_DELETED)
		ret = VAN_NOTFOUND;
err:	
	return (ret);

}

int _van_table_del_record(VAN_TABLE *table, VAN_DATUM *key, flags_t flags) {
	int ret, ret1;
	VAN_OPERATION *op;

	ret = _van_table_op_create(table, &op, 0);
	CHK_ERR(ret);

	ret = _van_table_op_del(op, key, flags);
	ret1 = _van_table_op_close(op);
	SET_RET(ret1, ret);
	if (ret == VAN_DELETED)
		ret = VAN_NOTFOUND;
err:
	return (ret);
}

int _van_table_new_odfilename(VAN_TABLE *table, char *buf, size_t buflen) {
	static time_t pretime = 0;
	static int preind = 0;
	time_t curtime;
	int ret, ind, num;

	ret = 0;
	curtime = time(NULL);
	ind = 0;

	if (curtime == pretime) {
		preind++;
		ind = preind;
	} else {
		preind = 0;
		pretime = curtime;
	}

	num = snprintf(buf, buflen, "%s.ondisk.%lu.%d", table->table_name,
	   (unsigned long)curtime, ind);
	if (num < 0) {
		ret = _van_getsys_error();
	}

	return ret;
}

int _van_table_add_odtpath(VAN_TABLE *table, ONDISK_TREE *odtree) {
	int ret, locked, ret1;
	size_t indx;

	locked = 0;
	VAN_MUTEX_LOCK(table, odtpath_mtx, ret);
	CHK_ERR(ret);
	locked = 1;

	indx = table->odtpath_cnt;
	ret = _van_strdup(NULL, odtree->file_path, &table->odtpaths[indx]);
	CHK_ERR(ret);
	table->odtpath_cnt++;
err:
	if (locked) {
		VAN_MUTEX_UNLOCK(table, odtpath_mtx, ret1);
		SET_RET(ret1, ret);
	}
	return (ret);
}

int _van_table_replace_odtpath(VAN_TABLE *table, ONDISK_TREE *odlow,
    ONDISK_TREE *odhigh, ONDISK_TREE *odnew) {
	int ret, ret1, i, locked;
	const char *pathlow, *pathhigh, *pathnew;
	size_t idxlow, idxhigh, idxtop, ni, nj;
	
	ret = 0;
	ret1 = 0;
	locked = 0;
	idxlow = -1;
	idxhigh = -1;
	VAN_ASSERT(table->odtpath_cnt > 0);
	idxtop = table->odtpath_cnt - 1;
	pathlow = odlow->file_path;
	pathhigh = odhigh->file_path;
	pathnew = odnew->file_path;

	VAN_MUTEX_LOCK(table, odtpath_mtx, ret);
	CHK_ERR(ret);
	locked = 1;

	/* Find the low index */
	for (i = 0; i <= idxtop; i++) {
		if (strcmp(pathlow, table->odtpaths[i]) == 0) {
			idxlow = i;
			break;
		}
	}
	VAN_ASSERT(idxlow != -1);

	/* Find the high index */
	for (; i <= idxtop; i++) {
		if (strcmp(pathhigh, table->odtpaths[i]) == 0) {
			idxhigh = i;
			break;
		}		
	}
	VAN_ASSERT(idxhigh != -1);

	for (i = idxlow; i <= idxhigh; i++) {
		_van_free(NULL, table->odtpaths[i]);
		table->odtpaths[i] = NULL;
	}
	ret = _van_strdup(NULL, pathnew, &table->odtpaths[idxlow]);
	CHK_ERR(ret);
	/* Do movement */
	if (idxhigh != idxlow && idxhigh != idxtop) {
		ni = idxlow + 1;
		nj = idxhigh + 1;
		for (i = nj; i <= idxtop; i++) {
			table->odtpaths[ni] = table->odtpaths[nj];
			table->odtpaths[nj] = NULL;
			ni++;
			nj++;
		}
	}
	table->odtpath_cnt = idxtop + 1 + idxlow - idxhigh;

err:
	if (locked) {
		VAN_MUTEX_UNLOCK(table, odtpath_mtx, ret1);
		SET_RET(ret1, ret);
	}
	return (ret);
}

int _van_table_clear_odtitem(VAN_TABLE *table, VAN_TABLE_ODT_ITEM *item) {
	int ret;
	ret = 0;

	item->trees[0] = NULL;
	item->trees[1] = NULL;
	item->flags = 0;
	F_SET(item, VAN_ITEM_UNUSED);

	/*
	 * Not used:
	 * ref, indx, ticks, merged_indx, merged_ticks
	 */
	item->ref = 0;
	item->merged_indx = 0;
	item->merged_ticks = 0;
	item->indx = 0;
	item->ticks = 0;

	return (ret);
}

int _van_table_copy_odtitem(VAN_TABLE *table, VAN_TABLE_ODT_ITEM *src,
    VAN_TABLE_ODT_ITEM *dst) {
	int ret;

	ret = 0;

	dst->trees[0] = src->trees[0];
	dst->trees[1] = src->trees[1];
	dst->flags = src->flags;
	dst->merged_indx = src->merged_indx;
	dst->merged_ticks = src->merged_ticks;
	dst->ticks = src->ticks;

	/* Not used: */
	dst->ref = src->ref;
	dst->indx = src->indx;

	return (ret);
}

/* 
 * Currently, the moving is from high to low, so always lock srcitem first,
 * and then the dstitem.
 */
int _van_table_move_odtitem(VAN_TABLE *table, VAN_TABLE_ODT_ITEM *src,
    VAN_TABLE_ODT_ITEM *dst) {
	int ret, ret1, ret2, srclocked, dstlocked;

	ret = 0;
	srclocked = 0;
	dstlocked = 0;
	
	if (src == dst)
		return (ret);

	VAN_MUTEX_LOCK(src, mutex, ret);
	CHK_ERR(ret);
	srclocked = 1;
	VAN_MUTEX_LOCK(dst, mutex, ret);
	CHK_ERR(ret);
	dstlocked = 1;

	/* 
	 * We must make sure that, the source is 
	 * not empty item, while the destination must
	 * be empty item.
	 */
	VAN_ASSERT(!F_ISSET(src, VAN_ITEM_UNUSED));
	VAN_ASSERT(F_ISSET(dst, VAN_ITEM_UNUSED));

	(void)_van_table_copy_odtitem(table, src, dst);
	(void)_van_table_clear_odtitem(table, src);

err:
	if (srclocked) {
		VAN_MUTEX_UNLOCK(src, mutex, ret1);
		SET_RET(ret1, ret);
	}
	if (dstlocked) {
		VAN_MUTEX_UNLOCK(dst, mutex, ret2);
		SET_RET(ret2, ret);
	}

	return (ret);
}

int _van_table_compact_odtiems(VAN_TABLE *table) {
	int ret, ret1, i, curpos, locked;
	size_t odt_top;
	VAN_TABLE_ODT_ITEM *item1, *item2;

	/* TODO: Mutex handling carefully. */

	ret = 0;
	locked = 0;
	VAN_MUTEX_LOCK(table, odt_mtx, ret);
	CHK_ERR(ret);
	odt_top = table->odt_top;
	VAN_MUTEX_UNLOCK(table, odt_mtx, ret);
	CHK_ERR(ret);

	curpos = 0;
	while (curpos < odt_top) {
		item1 = &table->odt_items[curpos];
		if (F_ISSET(item1, VAN_ITEM_UNUSED))
			break;
		curpos++;
	}
	i = curpos + 1;

retry:
	for (; i < (int)odt_top; i++) {
		item2 = &table->odt_items[i];
		if (!F_ISSET(item2, VAN_ITEM_UNUSED)) {
			/* We see non-empty item here. */
			ret = _van_table_move_odtitem(table, item2, item1);
			curpos++;
			item1 = &table->odt_items[curpos];
		}
	}

	if (!locked) {
		VAN_MUTEX_LOCK(table, odt_mtx, ret);
		CHK_ERR(ret);
	} else {
		VAN_ASSERT(table->odt_top == odt_top);
	}

	if (odt_top == table->odt_top) {
		table->odt_top = curpos;
	} else {
		odt_top = table->odt_top;
		locked = 1;
		goto retry;
	}

	VAN_MUTEX_UNLOCK(table, odt_mtx, ret);
	CHK_ERR(ret);

err:
	return (ret);
}

int _van_table_new_dumpfile(VAN_TABLE *table,
    VAN_TABLE_ODT_ITEM **itemp, ONDISK_TREE **odtreep) {
	int ret, ret1;
	size_t indx;
	VAN_TABLE_ODT_ITEM *item;
	char filepath[VAN_MAXPATHLEN];
	ONDISK_TREE *odtree;
	flags_t oflags;

	/* TODO: Mutex handling carefully. */
	/* TODO: Release the locker earlier */

	VAN_MUTEX_LOCK(table, odt_mtx, ret);
	if (table->odt_top == table->odtpath_capacity) {
		VAN_MUTEX_UNLOCK(table, odt_mtx, ret);
		return (VAN_OVERFLOW);
	}

	indx = table->odt_top;
	item = &table->odt_items[indx];
	(void)_van_table_clear_odtitem(table, item);	
	F_SET(item, VAN_ITEM_DUMPING);
	F_CLR(item, VAN_ITEM_UNUSED);

	odtree = NULL;
	memset(filepath, 0, sizeof(filepath));
	oflags = VAN_OPEN_CREAT | VAN_OPEN_TRUNC;
	ret = _van_table_new_odfilename(table, filepath, VAN_MAXPATHLEN);
	CHK_ERR(ret);
	ret = _van_ondisktree_create(table, &odtree);
	CHK_ERR(ret);
	ret = _van_ondisktree_open(odtree, filepath, oflags);
	CHK_ERR(ret);
	*itemp = item;
	*odtreep = odtree;
	table->odt_top++;
	
	if (0) {
err:
		if (odtree != NULL)
			ret1 = _van_ondisktree_close(odtree, VAN_CLOSE_REMOVE);
	}
	VAN_MUTEX_UNLOCK(table, odt_mtx, ret);
ret:
	return ret;
}

int _van_table_dump_frozen(VAN_TABLE *table, ONDISK_TREE **odtreep) {
	VAN_TABLE_ODT_ITEM *item;
	ONDISK_TREE *odtree;
	int ret;
	
	/* TODO: Mutex handling carefully. */

	if (table->frozen == NULL)
		return 0;

	ret = _van_table_new_dumpfile(table, &item, &odtree);
	CHK_ERR(ret);
	VAN_MUTEX_LOCK(item, mutex, ret);
	ret = _van_ondisktree_store_inmem(odtree, table->frozen);
	CHK_ERR(ret);
	item->trees[0] = odtree;
	F_CLR(item, VAN_ITEM_DUMPING);
	F_SET(item, VAN_ITEM_INUSE);
	ret =  _van_table_curticks(table, &item->ticks);
	CHK_ERR(ret);
	*odtreep = odtree;
	VAN_MUTEX_UNLOCK(item, mutex, ret);
err:
	return (ret);

}

/* For testing use currently */
/* ************************* */
/* ************************* */

int _van_table_dump_inmem(VAN_TABLE *table) {
	int ret;
	INMEM_TREE *active, *frozen;
	ONDISK_TREE *odtree;

	if (table->active == NULL) {
		ret = _van_inmemtree_create(table, &active);
		table->active = active;
		return (ret);
	} else if (table->active->size == 0)
		return (0);

	F_SET(table->active, INMEM_TREE_FROZEN);
	frozen = table->active;
	table->frozen = frozen;

	ret = _van_inmemtree_create(table, &active);
	CHK_ERR(ret);
	table->active = active;

	ret = _van_table_dump_frozen(table, &odtree);
	CHK_ERR(ret);
	ret = _van_table_add_odtpath(table, odtree);
	CHK_ERR(ret);
	F_SET(frozen, INMEM_TREE_DUMPED);
	table->frozen = NULL;
	ret = _van_inmemtree_destory(frozen);

err:
	return (ret);
}

int _van_table_replace_odt_items(VAN_TABLE *table, size_t st, size_t et,
    ONDISK_TREE *newodtree) {
	int i, ret;
	size_t cnt, capacity, indx;
	ticks_t max_ticks;
	VAN_TABLE_ODT_ITEM *item;
	ONDISK_TREE **odtrees;

	ret = 0;
	max_ticks = -MAX_TICKS;
	VAN_ASSERT(et >= st);
	capacity = et + 1 - st;
	ret = _van_calloc(NULL, capacity, sizeof(ONDISK_TREE *), &odtrees);
	if (ret != 0)
		return (ret);
	indx = 0;

	VAN_MUTEX_LOCK(table, odt_mtx, ret);
	/* Get the max_ticks */
	for (i = st; i <= et; i++) {
		item = &table->odt_items[i];
		if (F_ISSET(item, VAN_ITEM_UNUSED))
			continue;
		VAN_ASSERT(item->trees[0] != NULL);
		odtrees[indx++] = item->trees[0];
		VAN_ASSERT(item->ticks > max_ticks);
		max_ticks = item->ticks;
	}
	cnt = indx;

	item = &table->odt_items[st];
	item->trees[1] = newodtree;
	/* Set the merge_ticks */
	for (i = (int)et; i >= (int)st; i--) {
		item = &table->odt_items[i];
		if (F_ISSET(item, VAN_ITEM_UNUSED))
			continue;
		VAN_MUTEX_LOCK(item, mutex, ret);
		item->merged_indx = st;
		item->merged_ticks = max_ticks;
		F_CLR(item, VAN_ITEM_MERGING);
		F_SET(item, VAN_ITEM_MERGED);
		VAN_MUTEX_UNLOCK(item, mutex, ret);
	}

	/* Close the odtrees */
	for (i = (int)et; i >= (int)st; i--) {
		item = &table->odt_items[i];
		if (F_ISSET(item, VAN_ITEM_UNUSED) && i != st)
			continue;
		VAN_MUTEX_LOCK(item, mutex, ret);
		if (i != st)
			item->trees[0] = NULL;
		else
			item->trees[0] = newodtree;
		item->trees[1] = NULL;
		F_CLR(item, VAN_ITEM_MERGED);
		if (i == st)
			F_SET(item, VAN_ITEM_INUSE);
		else
			F_SET(item, VAN_ITEM_UNUSED);
		VAN_MUTEX_UNLOCK(item, mutex, ret);
	}
	VAN_MUTEX_UNLOCK(table, odt_mtx, ret);

	VAN_ASSERT(cnt > 0);
	ret = _van_table_replace_odtpath(table,
	    odtrees[0], odtrees[cnt - 1], newodtree);
	CHK_ERR(ret);
	ret = _van_table_write_odt_paths(table);
	CHK_ERR(ret);
	for (i = 0; i < cnt; i++) {
		ret = _van_ondisktree_close(odtrees[i], VAN_CLOSE_REMOVE);
		CHK_ERR(ret);
	}	

err:
	return (ret);
}
