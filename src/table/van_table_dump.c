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

/* TODO: Check if this function will run concurrently ? */
int _van_table_freeze(VAN_TABLE *table, unsigned long checkgen) {
	int ret;
	INMEM_TREE *im_tree, *active;

	ret = 0;
	active = NULL;

	if (atomic_fetch_add(&table->dumpgen, 1) != checkgen)
		return (VAN_STALE);

	/* There is no active tree */
	if (table->active == NULL) {
		return (ret);
	}
	/* There are no records in the tree. */
	im_tree = table->active;
	table->active = NULL;
	ret = _van_inmemtree_create(table, &table->active);
	CHK_ERR(ret);
	table->active->threshold = table->inmem_threshold;

	if (im_tree->records == 0)
		return (ret);
	else {
		VAN_ASSERT((F_ISSET(im_tree, INMEM_TREE_FROZEN)));
	}

	/* There is a dump running, just waiting. */
	while (table->frozen != NULL) {
 		/* Sleep for 1 seconds currently. */
 		_van_sleep(1, 0);
 	}
	table->frozen = im_tree;

	ret = _van_table_dump_frozen_init(table);
err:
	return (ret);
}

int _van_table_dump_frozen_init(VAN_TABLE *table) {
	int ret;
	VAN_TABLE_DUMP_JOB *dumpjob;

	dumpjob = NULL;
	ret = _van_calloc(NULL, 1, sizeof(VAN_TABLE_DUMP_JOB), &dumpjob);
	if (ret != 0)
		return (ret);
	dumpjob->table = table;
	dumpjob->finished = VAN_FALSE;

	ret = pthread_create(&dumpjob->thread_id, NULL,
	    _van_table_frozen_dump_thread, dumpjob);
	if (ret != 0) {
		_van_free(NULL, dumpjob);
		return (ret);
	}

	VAN_MUTEX_LOCK(table, dump_mutex, ret);
	TAILQ_INSERT_TAIL(&table->dump_jobs, dumpjob, links);
	VAN_MUTEX_UNLOCK(table, dump_mutex, ret);
	return (ret);
}

void *_van_table_frozen_dump_thread(void *arg) {
	VAN_TABLE_DUMP_JOB *dumpjob;
	VAN_TABLE *table;
	int ret, ret1;
	VAN_TABLE_ODT_ITEM *item;
	ONDISK_TREE *odtree;
	INMEM_TREE *frozen;

	item = NULL;
	odtree = NULL;
	dumpjob = (VAN_TABLE_DUMP_JOB *)arg;
	table = dumpjob->table;
	frozen = table->frozen;

	ret = _van_table_new_dumpfile(table, &item, &odtree);
	CHK_ERR(ret);
	ret = _van_ondisktree_store_inmem(odtree, frozen);
	CHK_ERR(ret);
	ret = _van_table_add_odtpath(table, odtree);
	CHK_ERR(ret);
	ret = _van_table_write_odt_paths(table);
	CHK_ERR(ret);

	/*
	 * Fully initialize the item under item->mutex (the same lock
	 * readers use) and only then publish it by bumping odt_top, so
	 * a concurrent get can never see trees[0] == NULL.
	 */
	VAN_MUTEX_LOCK(item, mutex, ret);
	item->trees[0] = odtree;
	/*
	 * Use the ticks recorded when the tree was frozen, not the dump
	 * completion ticks.  Otherwise a get operation created after the
	 * freeze but before this item is published would skip the item
	 * (item->ticks > create_ticks) and miss records that were already
	 * committed before the get operation started.
	 */
	item->ticks = frozen->frozen_ticks;
	F_CLR(item, VAN_ITEM_UNUSED);
	F_SET(item, VAN_ITEM_INUSE);
	F_CLR(item, VAN_ITEM_DUMPING);
	VAN_MUTEX_UNLOCK(item, mutex, ret);

	VAN_MUTEX_LOCK(table, odt_mtx, ret);
	table->odt_top++;
	VAN_MUTEX_UNLOCK(table, odt_mtx, ret);

	table->frozen = NULL;
 	F_SET(frozen, INMEM_TREE_DUMPED);
 	ret = _van_table_curticks(table, &frozen->dumped_ticks);
	/*
	 * Insert to the tail and later we will
	 * check from head.
	 */
	VAN_MUTEX_LOCK(table, im_dump_mtx, ret);
	TAILQ_INSERT_TAIL(&table->im_dump_trees, frozen, links);
	VAN_MUTEX_UNLOCK(table, im_dump_mtx, ret);

err:
	dumpjob->finished = VAN_TRUE;
	VAN_COND_SIGNAL(table, dump_join_cond, ret1);
	SET_RET(ret1, ret);
	return (void *)(uintmax_t)ret;
}

int _van_table_dump_join_init(VAN_TABLE *table) {
	int ret;
	VAN_TABLE_DUMP_JOIN_JOB *joinjob;

	ret = _van_calloc(NULL, 1, sizeof(VAN_TABLE_DUMP_JOIN_JOB), &joinjob);
	if (ret != 0)
		return (ret);
	joinjob->table = table;
	joinjob->finished = VAN_FALSE;

	ret = pthread_create(&joinjob->thread_id, NULL,
	    _van_table_dump_join_thread, joinjob);

	if (ret == 0)
		table->dump_join_job = joinjob;
	else
		_van_free(NULL, joinjob);

	return (ret);
}

/*
 * This function supports multiple dump trees,
 * but we only have one dumping job currently.
 *
 * To support multiple, we need to change the
 * read logic.
 */
void *_van_table_dump_join_thread(void *arg) {
	int ret, ret1;
	VAN_TABLE_DUMP_JOIN_JOB *joinjob;
	VAN_TABLE *table;
	VAN_TABLE_DUMP_JOB *dumpjob, *prejob;
	void *rptr;

	joinjob = (VAN_TABLE_DUMP_JOIN_JOB *)arg;
	table = joinjob->table;

	while (1) {
		VAN_MUTEX_LOCK(table, dump_mutex, ret);
		/* 
		fprintf(stderr, "VAN_COND_WAIT(table, dump_join_cond, &table->dump_mutex, ret)\n"); 
		we use prejob here, because we can not remove the dumpjob from the list
		when we are iterating it.
		*/
		if (!table->closed)
			VAN_COND_WAIT(table, dump_join_cond, &table->dump_mutex, ret);
		prejob = NULL;
		for (dumpjob = TAILQ_FIRST(&table->dump_jobs); dumpjob != NULL;
		    dumpjob = TAILQ_NEXT(dumpjob, links)) {
			if (prejob != NULL) {
				TAILQ_REMOVE(&table->dump_jobs, prejob, links);
				_van_free(NULL, prejob);
				prejob = NULL;
			}
			if (dumpjob->finished) {
				ret = pthread_join(dumpjob->thread_id, &rptr);
				ret1 = (int)(uintmax_t)rptr;
				SET_RET(ret1, ret);
				prejob = dumpjob;
			}
		}
		if (prejob != NULL) {
			TAILQ_REMOVE(&table->dump_jobs, prejob, links);
			_van_free(NULL, prejob);
			prejob = NULL;
		}
		VAN_MUTEX_UNLOCK(table, dump_mutex, ret);
		if (table->closed && TAILQ_FIRST(&table->dump_jobs) == NULL)
			break;

		/* Temporarily sleeping here. */
		_van_sleep(2, 0);
	}

	joinjob->finished = VAN_TRUE;

	return (void *)(uintmax_t)ret;
}


int _van_table_frozen_free_init(VAN_TABLE *table) {
	int ret;
	VAN_TABLE_FROZEN_FREE_JOB *freejob;

	ret = _van_calloc(NULL, 1, sizeof(VAN_TABLE_FROZEN_FREE_JOB), &freejob);
	if (ret != 0)
		return (ret);
	freejob->table = table;
	freejob->finished = VAN_FALSE;

	ret = pthread_create(&freejob->thread_id, NULL,
	    _van_table_frozen_free_thread, freejob);

	if (ret == 0)
		table->frozen_free_job = freejob;
	else
		_van_free(NULL, freejob);

	return (ret);
}

void *_van_table_frozen_free_thread(void *arg) {
	int ret;
	VAN_TABLE_FROZEN_FREE_JOB *freejob;
	INMEM_TREE *frozen, *tofree;
	VAN_TABLE *table;
	ticks_t minopticks;

	freejob = (VAN_TABLE_FROZEN_FREE_JOB *)arg;
	table = freejob->table;

	while (1) {

		VAN_MUTEX_LOCK(table, im_dump_mtx, ret);
		frozen = TAILQ_FIRST(&table->im_dump_trees);
		VAN_MUTEX_UNLOCK(table, im_dump_mtx, ret);

		VAN_MUTEX_LOCK(table, op_mtx, ret);
		minopticks = table->min_op_ticks;
		VAN_MUTEX_UNLOCK(table, op_mtx, ret);

		while (frozen != NULL && minopticks > frozen->dumped_ticks) {
			tofree = frozen;
			VAN_MUTEX_LOCK(table, im_dump_mtx, ret);
			TAILQ_REMOVE(&table->im_dump_trees, frozen, links);
			frozen = TAILQ_FIRST(&table->im_dump_trees);
			VAN_MUTEX_UNLOCK(table, im_dump_mtx, ret);
			ret = _van_inmemtree_destory(tofree);
		}

		if (table->closed && TAILQ_FIRST(&table->im_dump_trees) == NULL)
			break;

		/* Temporarily sleeping here. */
		_van_sleep(2, 0);
	}

	freejob->finished = VAN_TRUE;

	return (void *)(uintmax_t)ret;
}
