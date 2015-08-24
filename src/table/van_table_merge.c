#include "van_int.h"
#include "van.h"

#include "van_ondisk_tree.h"

int _van_table_merge_scan_init(VAN_TABLE *table) {
	int ret;
	VAN_TABLE_SCAN_JOB *jobarg;

	ret = _van_calloc(NULL, 1, sizeof(VAN_TABLE_SCAN_JOB), &jobarg);
	CHK_ERR(ret);

	jobarg->table = table;
	jobarg->nsleep = table->nsleep;
	ret = pthread_create(&jobarg->thread_id, NULL,
	    _van_table_merge_scan_run, jobarg);

	table->scan_job = jobarg;
err:
	return (ret);
}

void *_van_table_merge_scan_run(void *arg) {
	int ret;
	VAN_TABLE_SCAN_JOB *jobarg;

	jobarg = (VAN_TABLE_SCAN_JOB *)arg;
	while (!table->closed) {
		ret = _van_table_merge_scan(jobarg->table);
		if (ret != 0)
			break;
		_van_sleep(jobarg->nsleep, 0);
	}

	return (void *)ret;
}

int _van_table_merge_join_init(VAN_TABLE *table) {
	int ret;
	VAN_TABLE_MERGE_JOIN_JOB *jobarg;

	ret = _van_calloc(NULL, 1, sizeof(VAN_TABLE_MERGE_JOIN_JOB), &jobarg);
	CHK_ERR(ret);

	jobarg->table = table;
	ret = pthread_create(&jobarg->thread_id, NULL,
	    _van_table_merge_join_run, jobarg);

	table->merge_join_job = jobarg;
err:
	return (ret);
}

void *_van_table_merge_join_run(void *arg) {
	int ret, ret1;
	VAN_TABLE_MERGE_JOIN_JOB *taskarg;
	VAN_TABLE *table;
	VAN_TABLE_MERGE_JOB *job;
	void *rptr;

	ret = 0;
	taskarg = (VAN_TABLE_MERGE_JOIN_JOB *)arg
	table = taskarg->table;

	VAN_MUTEX_LOCK(table, merge_mutex, ret);
	CHK_ERR(ret);
	while ((job = TAILQ_FIRST(&table->free_merge_jobs)) != NULL) {
		rptr = NULL;
		pthread_join(job->thread_id, &rptr);
		ret1 = (int)rptr;
		SET_RET(ret1, ret);
		TAILQ_REMOVE(&table->free_merge_jobs, job, links);
		_van_free(NULL, job);
	}
	if (table->closed)
		goto unlock;
	else {
		VAN_COND_WAIT(table, merge_cond, &table->merge_mutex, ret);
		if (ret != 0)
			goto unlock;
	}
unlock:
	VAN_MUTEX_UNLOCK(table, merge_mutex, ret);
err:
	return (void *)ret;
}

int _van_table_merge_clean(VAN_TABLE *table) {
	int ret, ret1;
	VAN_TABLE_MERGE_JOB *job;
	void *rptr;

	ret = 0;
	VAN_MUTEX_LOCK(table, merge_mutex, ret);
	CHK_ERR(ret);
	while ((job = TAILQ_FIRST(&table->merge_jobs)) != NULL) {
		rptr = NULL;
		pthread_join(job->thread_id, &rptr);
		ret1 = (int)rptr;
		SET_RET(ret1, ret);
		TAILQ_REMOVE(&table->merge_jobs, job, links);
		_van_free(NULL, job);
	}
	VAN_MUTEX_UNLOCK(table, merge_mutex, ret);

err:
	return ret;
}

int _van_table_merge_scan(VAN_TABLE *table) {
	VAN_TABLE_MERGE_CONFIG *merge_cfg;
	int cur_indx, ret,clear, reset, domerge;
	size_t odt_top, low, high, cnt, i;
	VAN_TABLE_ODT_ITEM *item;

	merge_cfg = &table->merge_cfg;
	VAN_MUTEX_LOCK(table, odt_mtx, ret);
	odt_top = table->odt_top;
	VAN_MUTEX_UNLOCK(table, odt_mtx, ret);

	cur_indx = odt_top;
	high = 0;
	low = 0;
	cnt = 0;

	while (cur_indx >= 0 && !table->closed) {
		domerge = 0;
		reset = 0;
		clear = 0;
		item = &table->odt_items[cur_indx];
		VAN_MUTEX_LOCK(item, mutex, ret);
		if (F_ISSET(item, VAN_ITEM_MERGING | VAN_ITEM_MERGED)) {
			/* See a item that is not a candidate. */
			if (cnt >= merge_cfg->merge_min)
				domerge = 1;
			else if (cnt > 0)
				clear = 1; 
		} else {
			low = cur_indx;
			/* The item can be used */
			if (high == 0)
				high = cur_indx;

			/* The item is in use. */
			if (!F_ISSET(item, VAN_ITEM_UNUSED) ||
			    F_ISSET(item, VAN_ITEM_INUSE)) {
				F_SET(item, VAN_ITEM_MERGING);
				cnt++;
			}

			if (cnt >= merge_cfg->merge_max)
				domerge = 1;
			else if (cur_indx == 0) {
				if (cnt >= merge_cfg->merge_min)
					domerge = 1;
				else if (cnt > 0)
					clear = 1;
			}
		}
		VAN_MUTEX_UNLOCK(item, mutex, ret);
		if (domerge) {
			reset = 1;
			ret = _van_table_new_merge_job(table, low,
			    high, cnt);
		} else if (clear) {
			reset = 1;
			ret = _van_table_clear_merging(table, low, high);
		}

		if (reset) {
			low = 0;
			high = 0;
			cnt = 0;
		}
		cur_indx--;
	}

	return (ret);
}

/*
 * Clear the merge flags of VAN_ITEM_MERGING.
 */
int _van_table_clear_merging(VAN_TABLE *table, size_t low, size_t high) {
	VAN_TABLE_ODT_ITEM *item;
	size_t i;
	int ret;

	for (i = low; i <= high; i++) {
		item = &table->odt_items[cur_indx];
		VAN_MUTEX_LOCK(item, mutex, ret);
		if (F_ISSET(item, VAN_ITEM_MERGING))
			F_CLR(item, VAN_ITEM_MERGING);
		VAN_MUTEX_UNLOCK(item, mutex, ret);
	}

	return ret;
}

/*
 * Change merge flags from VAN_ITEM_MERGING to VAN_ITEM_MERGED,
 * also set the merged_indx and merged_ticks.
 */
int _van_table_update_merge(VAN_TABLE *table,
    const VAN_TABLE_MERGE_JOB *jobarg) {
	VAN_TABLE_ODT_ITEM *item;
	size_t i;
	int ret;

	for (i = jobarg->low; i <= jobarg->high; i++) {
		item = &table->odt_items[cur_indx];
		VAN_MUTEX_LOCK(item, mutex, ret);
		if (F_ISSET(item, VAN_ITEM_MERGING)) {
			F_SET(item, VAN_ITEM_MERGED);
			F_CLR(item, VAN_ITEM_MERGING);
			item->merged_indx = jobarg->low;
			item->merged_ticks = jobarg->max_ticks;
		}
		VAN_MUTEX_UNLOCK(item, mutex, ret);
	}

	return ret;
}

/*
 * Change merge flags from VAN_ITEM_MERGED to VAN_ITEM_UNUSED.
 */
int _van_table_clear_merged(VAN_TABLE *table, size_t low, size_t high) {
	VAN_TABLE_ODT_ITEM *item;
	size_t i;
	int ret;

	for (i = low; i <= high; i++) {
		item = &table->odt_items[cur_indx];
		ret = _van_table_clear_merged_one(table, item);
	}
	
	return (ret);
}

int _van_table_clear_merged_one(VAN_TABLE *table, VAN_TABLE_ODT_ITEM *item) {
	int ret;

	VAN_MUTEX_LOCK(item, mutex, ret);
	if (F_ISSET(item, VAN_ITEM_MERGED)) {
		F_SET(item, VAN_ITEM_UNUSED);
		F_CLR(item, VAN_ITEM_MERGED);
	}
	VAN_MUTEX_UNLOCK(item, mutex, ret);

	return ret;
}	

int _van_table_new_merge_job(VAN_TABLE *table, size_t low,
    size_t high, size_t cnt) {
	VAN_TABLE_MERGE_JOB *jobarg;
	int ret;
	
	jobarg = NULL;
	ret = _van_calloc(NULL, 1, sizeof(VAN_TABLE_MERGE_JOB), &jobarg);
	if (ret != 0)
		return (ret);

	jobarg->low = low;
	jobarg->high = high;
	jobarg->cnt = cnt;
	jobarg->table = table;
	F_SET(job, MERGE_JOG_RUNNING);

	ret = pthread_create(&jobarg->thread_id, NULL,
	    _van_table_merge_job_run, jobarg);

	/* Add the job into the list */
	VAN_MUTEX_LOCK(table, merge_mutex, ret);
	TALQ_INSERT_TAIL(&table->merge_jobs, jobarg, links);
	VAN_MUTEX_UNLOCK(table, merge_mutex, ret);

	return ret;
}

/* 
 * Job skeleton.
 * We set flags of MERGE_JOB_FINISHED just before quiting.
 */
void *_van_table_merge_job_run(void *arg) {
	VAN_TABLE_MERGE_JOB *job;
	VAN_TABLE *table;
	VAN_TABLE_ODT_ITEM *item;
	VAN_ONDISK_TREE *tree, *lowtree, **trees, *restree;
	int i, idx;
	ticks_t minopticks;

	table = job->table;
	if (!table->closed)
		return (NULL);
	/*
	 * Put the merge logic here.
	 */
	trees = _van_calloc(NULL, (job->high - job->low + 1),
	    sizeof(ONDISK_TREE *), &trees);
	if (ret != 0)
		return (void *)ret;
	idx = 0;
	for (i = job->low; i <= job->high; i++) {
		item = &table->odt_items[i];
		if (item->trees[0] != NULL) {
			trees[idx++] = item->trees[0];
		}
	}
	VAN_ASSERT(idx == job->cnt);
	ret = _van_ondisktree_merge(trees, job->cnt, &restree);

	/* Set the merged ticks to guide the search */
	for (i = job->low; i<= job->high; i++) {
		item = &table->odt_items[i];
		VAN_MUTEX_LOCK(item, mutex, ret);
		item->merge_ticks = job->max_ticks;
		item->merge_indx = job->low;
		F_SET(item, VAN_ITEM_MERGED);
		F_CLR(item, VAN_ITEM_MERGING);
		VAN_MUTEX_LOCK(item, mutex, ret);
	}

	/* Now we check if the trees can be freed */
	while (1) {
		VAN_MUTEX_LOCK(table, op_mtx, ret);
		minopticks = table->min_op_ticks;
		VAN_MUTEX_UNLOCK(table, op_mtx, ret);
		if (minopticks > job->max_ticks) {
			/* Is this atomic ? */
			item = &table->odt_items[job->low];
			lowtree = item->trees[0];
			VAN_MUTEX_LOCK(item, mutex, ret);
			item->trees[0] = item->trees[1];
			item->trees[1] = NULL;
			VAN_MUTEX_UNLOCK(item, mutex, ret);
			for (i = job->high; i >= job->low; i--) {
				item = table->odt_items[i];
				VAN_MUTEX_LOCK(item, mutex, ret);
				F_CLR(item, VAN_ITEM_MERGED);
				item->trees[1] = NULL;
				item->merged_indx = 0;
				item->merge_ticks = 0;
				if (i > job->low) {
					if (item->trees[0] != NULL)
						_van_ondisktree_close(item->trees[0]);
					item->trees[0] = NULL;
					F_CLR(item, VAN_ITEM_INUSE);
					F_SET(item, VAN_ITEM_UNUSED);
				} else {
					if (lowtree != NULL)
						_van_ondisktree_close(lowtree);
					F_SET(item, VAN_ITEM_INUSE);
				}	
				VAN_MUTEX_UNLOCK(item, mutex, ret);
			}
			/* Now we need to write current  */

			break;
		} else {
			/* 
			 * Sleep for 2 seconds
			 * TODO: should be configured. 
			 */
			_van_sleep(2, 0);
		}
	}

	/* Set finished */
	F_SET(job, MERGE_JOB_FINISHED);
	F_CLR(job, MERGE_JOG_RUNNING);
	VAN_MUTEX_LOCK(table, merge_mutex, ret);
	CHK_ERR(ret);
	if (!table->closed) {
		TAILQ_REMOVE(&table->merge_jobs, job, links);
		TAILQ_INSERT_HEAD(&table->free_merge_jobs, job, links);
		VAN_COND_SIGNAL(table, merge_cond, ret);
		CHK_ERR(ret);
	}
	VAN_MUTEX_UNLOCK(table, merge_mutex, ret);

err:
	return (void *)ret;
}
