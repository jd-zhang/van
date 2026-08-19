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

#include "van_ondisk_tree.h"

int _van_table_merge_scan_init(VAN_TABLE *table) {
	int ret;
	VAN_TABLE_SCAN_JOB *jobarg;

	ret = _van_calloc(NULL, 1, sizeof(VAN_TABLE_SCAN_JOB), &jobarg);
	CHK_ERR(ret);

	jobarg->table = table;
	jobarg->nsleep = table->nsleep;
	jobarg->finished = VAN_FALSE;

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

	while (!jobarg->table->closed) {
		ret = _van_table_merge_scan(jobarg->table);
		if (ret != 0)
			break;
		if (!jobarg->table->closed)
			_van_sleep(jobarg->nsleep, 0);
	}

	jobarg->finished = VAN_TRUE;

	return (void *)(uintmax_t)ret;
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

	cur_indx = (int)odt_top - 1;
	high = 0;
	low = 0;
	cnt = 0;

	fprintf(stderr, "_van_table_merge_scan\n");
	VAN_ASSERT(table->mergecnt == 0);
	while (cur_indx >= 0 && !table->closed) {
		domerge = 0;
		reset = 0;
		clear = 0;
		item = &table->odt_items[cur_indx];
		VAN_MUTEX_LOCK(item, mutex, ret);
		if (F_ISSET(item,
		    VAN_ITEM_MERGING | VAN_ITEM_MERGED | VAN_ITEM_DUMPING)) {
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
			if (!F_ISSET(item, VAN_ITEM_UNUSED) &&
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

			VAN_MUTEX_LOCK(table, mergecnt_mtx, ret);
			/* TODO: panic check */
			table->mergecnt++;
			VAN_MUTEX_UNLOCK(table, mergecnt_mtx, ret);
			/* TODO: panic check */

			ret = _van_table_new_merge_job(table, low,
			    high, cnt);
			/* TODO: error check */
		} else if (clear) {
			reset = 1;
			ret = _van_table_clear_merging(table, low, high);
			/* TODO: error check */
		}

		if (reset) {
			low = 0;
			high = 0;
			cnt = 0;
		}
		cur_indx--;
	}

	/*
	 * Sleep for 2 seconds
	 * TODO: should be configured.
	 */
	while (table->mergecnt > 0) {
		_van_sleep(2, 0);
	}
	VAN_ASSERT(table->mergecnt == 0);

	/* Grape all the threads */
	ret = _van_table_merge_clean(table);
	CHK_ERR(ret);

	/* Now compact the items */
	ret = _van_table_compact_odtiems(table);
	CHK_ERR(ret);

err:
	return (ret);
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
		ret1 = (int)(uintmax_t)rptr;
		SET_RET(ret1, ret);
		TAILQ_REMOVE(&table->merge_jobs, job, links);
		_van_free(NULL, job);
	}
	VAN_MUTEX_UNLOCK(table, merge_mutex, ret);

err:
	return ret;
}

/*
 * Clear the merge flags of VAN_ITEM_MERGING.
 */
int _van_table_clear_merging(VAN_TABLE *table, size_t low, size_t high) {
	VAN_TABLE_ODT_ITEM *item;
	size_t i;
	int ret;

	for (i = low; i <= high; i++) {
		item = &table->odt_items[i];
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
		item = &table->odt_items[i];
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
		item = &table->odt_items[i];
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
	jobarg->finished = VAN_FALSE;

	/* Add the job into the list */
	VAN_MUTEX_LOCK(table, merge_mutex, ret);
	TAILQ_INSERT_TAIL(&table->merge_jobs, jobarg, links);
	VAN_MUTEX_UNLOCK(table, merge_mutex, ret);

	ret = pthread_create(&jobarg->thread_id, NULL,
	    _van_table_merge_job_run, jobarg);

	return ret;
}

/*
 * Job skeleton.
 * We job->finished just before quiting.
 */
void *_van_table_merge_job_run(void *arg) {
	VAN_TABLE_MERGE_JOB *job;
	VAN_TABLE *table;
	VAN_TABLE_ODT_ITEM *item;
	ONDISK_TREE *tree, *lowtree, *ctree, **trees, *restree;
	int i, ret, idx;
	ticks_t minopticks, merged_ticks;

	ret = 0;
	job = (VAN_TABLE_MERGE_JOB *)arg;
	table = job->table;

	if (table->closed) {
		goto out;
	}

	fprintf(stderr, "_van_table_merge_job_run\n");
	/*
	 * Put the merge logic here.
	 */
	ret = _van_calloc(NULL, (job->high - job->low + 1),
	    sizeof(ONDISK_TREE *), &trees);
	if (ret != 0)
		return (void *)(uintmax_t)ret;

	idx = 0;
	for (i = job->low; i <= job->high; i++) {
		item = &table->odt_items[i];
		if (!F_ISSET(item, VAN_ITEM_MERGING))
			continue;
		VAN_ASSERT(item->trees[0] != NULL);
		VAN_ASSERT(item->trees[1] == NULL);
		trees[idx++] = item->trees[0];
		if (job->max_ticks < item->ticks) {
			job->max_ticks = item->ticks;
		}
	}
	VAN_ASSERT(idx == job->cnt);

	ret = _van_ondisktree_merge(table, trees, job->cnt, &restree);
	/* TODO: check error ret */
	item = &table->odt_items[job->low];
	item->trees[1] = restree;

	/* Set the merged ticks to guide the search */
	for (i = job->low; i<= job->high; i++) {
		item = &table->odt_items[i];
		if (!F_ISSET(item, VAN_ITEM_MERGING))
			continue;
		VAN_MUTEX_LOCK(item, mutex, ret);
		item->merged_ticks = job->max_ticks;
		item->merged_indx = job->low;
		F_SET(item, VAN_ITEM_MERGED);
		F_CLR(item, VAN_ITEM_MERGING);
		VAN_MUTEX_UNLOCK(item, mutex, ret);
	}

	ret = _van_table_curticks(table, &merged_ticks);

	/* Now we check if the trees can be freed */
	while (1) {
		VAN_MUTEX_LOCK(table, op_mtx, ret);
		minopticks = table->min_op_ticks;
		VAN_MUTEX_UNLOCK(table, op_mtx, ret);
		/*
		 * All operations after merged flag is set
		 * will not access the trees used for merging.
		 */
		if (minopticks > merged_ticks) {
			/* Is this atomic ? */
			item = &table->odt_items[job->low];
			lowtree = item->trees[0];
			VAN_MUTEX_LOCK(item, mutex, ret);
			item->trees[0] = item->trees[1];
			item->trees[1] = NULL;
			VAN_MUTEX_UNLOCK(item, mutex, ret);
			for (i = (int)job->high; i >= (int)job->low; i--) {
				item = &table->odt_items[i];
				if (!F_ISSET(item, VAN_ITEM_MERGED))
					continue;
				ctree = NULL;
				VAN_MUTEX_LOCK(item, mutex, ret);
				item->trees[1] = NULL;
				item->merged_indx = 0;
				item->merged_ticks = 0;
				if (i > job->low) {
					VAN_ASSERT(item->trees[0] != NULL);
					ctree = item->trees[0];
					item->trees[0] = NULL;
					F_CLR(item, VAN_ITEM_INUSE);
					F_SET(item, VAN_ITEM_UNUSED);
				} else {
					if (lowtree != NULL)
						ctree = lowtree;
					F_SET(item, VAN_ITEM_INUSE);
				}
				F_CLR(item, VAN_ITEM_MERGED);
				VAN_MUTEX_UNLOCK(item, mutex, ret);
				/* We put off the closing later. */
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

	/* Now we can remove the trees merged. */
	ret = _van_table_replace_odtpath(table,
	    trees[0], trees[job->cnt - 1], restree);
	/* TODO: check error ret */
	CHK_ERR(ret);
	ret = _van_table_write_odt_paths(table);
	/* TODO: check error ret */
	CHK_ERR(ret);

	/* Now close and remove the files */
	for (i = 0; i < (int)job->cnt; i++) {
		ret = _van_ondisktree_close(trees[i], VAN_CLOSE_REMOVE);
		CHK_ERR(ret);
	}

out:
	/* Set finished */
	job->finished = VAN_TRUE;

	VAN_MUTEX_LOCK(table, mergecnt_mtx, ret);
	/* TODO: panic check */
	table->mergecnt--;
	VAN_MUTEX_UNLOCK(table, mergecnt_mtx, ret);
	/* TODO: panic check */

err:
	return (void *)(uintmax_t)ret;
}


/* ####################################################### */
/* The following functions are not used currently. */
/* ####################################################### */

/* NOT USED CURRENTLY */
int _van_table_merge_join_init(VAN_TABLE *table) {
	int ret;
	VAN_TABLE_MERGE_JOIN_JOB *jobarg;

	ret = _van_calloc(NULL, 1, sizeof(VAN_TABLE_MERGE_JOIN_JOB), &jobarg);
	CHK_ERR(ret);

	jobarg->table = table;
	jobarg->finished = VAN_FALSE;

	ret = pthread_create(&jobarg->thread_id, NULL,
	    _van_table_merge_join_run, jobarg);

	table->merge_join_job = jobarg;
err:
	return (ret);
}

/* NOT USED CURRENTLY */
void *_van_table_merge_join_run(void *arg) {
	int ret, ret1;
	VAN_TABLE_MERGE_JOIN_JOB *taskarg;
	VAN_TABLE *table;
	VAN_TABLE_MERGE_JOB *job;
	void *rptr;

	ret = 0;
	taskarg = (VAN_TABLE_MERGE_JOIN_JOB *)arg;
	table = taskarg->table;

	VAN_MUTEX_LOCK(table, merge_mutex, ret);
	CHK_ERR(ret);
check:
	/* TODO: decrease lock time before/after pthread_join */
	while ((job = TAILQ_FIRST(&table->free_merge_jobs)) != NULL) {
		rptr = NULL;
		pthread_join(job->thread_id, &rptr);
		ret1 = (int)(uintmax_t)rptr;
		SET_RET(ret1, ret);
		TAILQ_REMOVE(&table->free_merge_jobs, job, links);
		_van_free(NULL, job);
	}
	if (table->closed)
		goto unlock;
	else {
		VAN_COND_WAIT(table, merge_join_cond, &table->merge_mutex, ret);
		if (ret != 0)
			goto unlock;
		else
			goto check;
	}
unlock:
	VAN_MUTEX_UNLOCK(table, merge_mutex, ret);
err:
	taskarg->finished = VAN_TRUE;
	return (void *)(uintmax_t)ret;
}


