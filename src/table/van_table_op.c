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
#include "van_rec.h"

int _van_table_op_create(VAN_TABLE *table, VAN_OPERATION **tbopp,
    flags_t cflag, ticks_t tk) {
	int ret;
	VAN_OPERATION *tbop;

	VAN_ASSERT(tbopp != NULL);
	tbop = NULL;

	ret = _van_calloc(NULL, 1, sizeof(VAN_OPERATION), &tbop);
	if (ret != 0)
		return (ret);

	tbop->table = table;
	tbop->create_ticks = tk;
	F_SET(tbop, cflag);
	ret = _van_table_op_init(tbop);
	if (ret == 0)
		*tbopp = tbop;

	return (ret);
}

int _van_table_op_init(VAN_OPERATION *tbop) {
	int ret;
	VAN_TABLE *table;

	/* Member functions */
	ret = 0;
	tbop->get = _van_table_op_get;
	tbop->put = _van_table_op_put;
	tbop->del = _van_table_op_del;

	table = tbop->table;
	VAN_MUTEX_LOCK(table, op_mtx, ret);
	TAILQ_INSERT_TAIL(&table->operations, tbop, links);
	if (table->min_op_ticks == MAX_TICKS)
		table->min_op_ticks = tbop->create_ticks;
	VAN_MUTEX_UNLOCK(table, op_mtx, ret);

	F_SET(tbop, VAN_OP_INITED);
	return (ret);
}

int _van_table_op_close(VAN_OPERATION *tbop) {
	int ret;
	VAN_TABLE *table;
	VAN_OPERATION *fop;

	table = tbop->table;
	VAN_MUTEX_LOCK(table, op_mtx, ret);
	TAILQ_REMOVE(&table->operations, tbop, links);
	if ((fop = TAILQ_FIRST(&table->operations)) != NULL) {
		if (table->min_op_ticks != fop->create_ticks)
			table->min_op_ticks = fop->create_ticks;
	} else
		table->min_op_ticks = MAX_TICKS;
	VAN_MUTEX_UNLOCK(table, op_mtx, ret);

	ret = _van_table_op_destory(tbop);

	return (ret);
}

int _van_table_op_destory(VAN_OPERATION *tbop) {
	int ret;

	ret = 0;
	_van_free(NULL, tbop);

	return (ret);
}

int _van_table_op_del(VAN_OPERATION *tbop, VAN_DATUM *key, flags_t flags) {
	int ret;
	VAN_TABLE *table;
	INMEM_TREE *active;
	unsigned long expgen;
	VAN_LSN lsn;
	uint32 size;

	ret = 0;
	table = tbop->table;
	INIT_LSN(&lsn);

	/*
	 * WAL: write the del log record before modifying the in-memory
	 * tree so the change can be replayed on recovery. The txnid of
	 * the owning transaction is attached by _van_put_logrecord
	 * (0 for non-transactional operations). TRANSIENT operations
	 * are not logged.
	 */
	if (!F_ISSET(tbop, VAN_OP_TRANSIENT)) {
		size = _van_calc_logrec_size(_inmemtree_del_recspec, key);
		ret = _van_put_logrecord(table->store, table, tbop->txn,
		    &lsn, flags, VAN_INMEMTREE_DEL, size,
		    _inmemtree_del_recspec, key);
		if (ret != 0)
			return (ret);
	}
	/*
	 * The active->del should return VAN_FROZEN after it is frozen,
	 * before it is freed completely.
	 */
	while (!table->closed) {
		if (table->active == NULL) { 
			_van_sleep(0, 1000);
			continue;
		}
		expgen = atomic_load(&table->dumpgen);
		ret = table->active->del(table->active, key, flags);
		if	(ret == VAN_FROZEN || ret == VAN_INMEM_FULL) {
			_van_table_freeze(table, expgen);
			_van_sleep(0, 1000);
			continue;
		}
		break;
	}

	return (ret);
}

int _van_table_op_put(VAN_OPERATION *tbop, VAN_DATUM *key,
    VAN_DATUM *data, flags_t flags) {
	int ret;
	VAN_TABLE *table;
	INMEM_TREE *active;
	unsigned long expgen;
	int loopcnt = 0;
	VAN_LSN lsn;
	uint32 size;

	ret = 0;
	table = tbop->table;
	INIT_LSN(&lsn);

	/*
	 * WAL: write the put log record before modifying the in-memory
	 * tree so the change can be replayed on recovery. The txnid of
	 * the owning transaction is attached by _van_put_logrecord
	 * (0 for non-transactional operations). TRANSIENT operations
	 * are not logged.
	 */
	if (!F_ISSET(tbop, VAN_OP_TRANSIENT)) {
		size = _van_calc_logrec_size(_inmemtree_put_recspec, key, data);
		ret = _van_put_logrecord(table->store, table, tbop->txn,
		    &lsn, flags, VAN_INMEMTREE_PUT, size,
		    _inmemtree_put_recspec, key, data);
		if (ret != 0)
			return (ret);
	}
	/*
	 * The active->put should return VAN_FROZEN after it is frozen,
	 * before it is freed completely.
	 */

	while (!table->closed) {
		loopcnt++;
		if (table->active == NULL) { 
			_van_sleep(0, 1000);
			continue;
		}
		expgen = atomic_load(&table->dumpgen);
		ret = table->active->put(table->active, key, data, flags);
		if	(ret == VAN_FROZEN || ret == VAN_INMEM_FULL) {
			_van_table_freeze(table, expgen);
			_van_sleep(0, 1000);
			continue;
		}
		break;
	}

	return (ret);
}

int _van_table_op_get(VAN_OPERATION *tbop, VAN_DATUM *key,
    VAN_DATUM *data, flags_t flags) {
	/*
	 * 'ret' carries the result of the search, 'mtx_ret' only the
	 * result of the mutex calls.  They have to be separate: the
	 * VAN_MUTEX_* macros assign to whatever variable they are given,
	 * so handing them 'ret' overwrites the VAN_NOTFOUND that the
	 * in-memory searches below produced and turns a missing key into
	 * a successful get with an empty value.
	 */
	int ret, mtx_ret;
	VAN_TABLE *table;
	INMEM_TREE *active, *frozen;
	ONDISK_TREE *odtree;
	size_t odt_top;
	int cur_indx, gonext, changed;
	VAN_TABLE_ODT_ITEM *citem;

	ret = 0;
	mtx_ret = 0;
	table = tbop->table;
	active = table->active;
	/*
	 * When it is found in the active (exists or deleted),
	 * We get the item. Otherwise, it will return VAN_NOTFOUND
	 * if the active is not freed, otherwise, it will return
	 * VAN_STALE if it is freed.
	 *
	 * Other return values means error.
	 *
	 * TODO: VAN_STALE is not used currently, need to check more.
	 */
	ret = active->getraw(active, key, data, flags);
	if (ret == 0 || ret == VAN_DELETED)
		goto ret;
	if (ret != VAN_NOTFOUND && ret != VAN_STALE)
		goto ret;
	/*
	 * Like above, we will stop if it is found(exists or deleted),
	 * and return VAN_STALE or VAN_NOTFOUND.
	 *
	 * TODO: VAN_STALE is not used currently, need to check more.
	 */
	frozen = table->frozen;
	if (frozen != NULL) {
		ret = frozen->getraw(frozen, key, data, flags);
		if (ret == 0 || ret == VAN_DELETED)
			goto ret;
		if (ret != VAN_NOTFOUND && ret != VAN_STALE)
			goto ret;
	}

	/*
	 * Now, we need to search the tree list.
	 * The search logic is:
	 * Search from the first tree with ticks smaller than current cursor,
	 * then find from high to low if not found. After searching a tree,
	 * if the tree is merged and the merged ticks is smaller than current
	 * cursor, we just jump to the merged tree. If the merged ticks is
	 * bigger than current cursor, we just search down and down.
	 * When we returnt the last tree, we just return the result.
	 */
	VAN_MUTEX_LOCK(table, odt_mtx, mtx_ret);
	if (mtx_ret != 0) {
		ret = mtx_ret;
		goto err;
	}
	odt_top = table->odt_top;
	VAN_MUTEX_UNLOCK(table, odt_mtx, mtx_ret);
	if (mtx_ret != 0 && ret == 0) {
		ret = mtx_ret;
		goto err;
	}
	cur_indx = (int)odt_top;

	while (cur_indx >= 0) {
		gonext = 0;
		citem = &table->odt_items[cur_indx];
		VAN_MUTEX_LOCK(citem, mutex, mtx_ret);
		if (mtx_ret != 0) {
			ret = mtx_ret;
			goto err;
		}
		/*
		 * First check the table_ticks, and skip to next
		 * if it is generated later than the operation.
		 *
		 * If the item is not used, we can also goto next.
		 */
		if (F_ISSET(citem, VAN_ITEM_UNUSED) ||
		    (citem->ticks > tbop->create_ticks)) {
			cur_indx--;
			gonext = 1;
			goto unlock_item;
		}
		odtree = citem->trees[0];

		/*
		 * The item is merged.
		 *
		 * if we are not the merged indx, we need to use
		 * merged_ticks to determine whether we should
		 * jump to the merged_indx or check current.
		 *
		 * If we are the merged_indx, we need to
		 * check merged_ticks to dertermine whether
		 * we should use citem->tree[0] or citem->tree[1].
		 */
		if (F_ISSET(citem, VAN_ITEM_MERGED)) {
			VAN_ASSERT(cur_indx >= citem->merged_indx);
			if (cur_indx > citem->merged_indx)
				changed = 1;
			else
				changed = 0;
			if (citem->merged_ticks <= tbop->create_ticks) {
				VAN_ASSERT(citem->ticks <= citem->merged_ticks);
				if (changed) {
					cur_indx = citem->merged_indx;
					VAN_MUTEX_UNLOCK(citem, mutex, mtx_ret);
					if (mtx_ret != 0 && ret == 0) {
						ret = mtx_ret;
						goto err;
					}
					citem = &table->odt_items[cur_indx];
					VAN_MUTEX_LOCK(citem, mutex, mtx_ret);
					if (mtx_ret != 0) {
						ret = mtx_ret;
						goto err;
					}
				}
				odtree = citem->trees[1];
				 /* The item may have been changed. */
				if (odtree == NULL)
					odtree = citem->trees[0];
			}
		}
unlock_item:
	if (gonext) {
		VAN_MUTEX_UNLOCK(citem, mutex, mtx_ret);
		if (mtx_ret != 0 && ret == 0) {
			ret = mtx_ret;
			goto err;
		}
		continue;
	}

	/*
	 * Take a reference on the ondisk tree while holding the item
	 * lock.  A concurrent merge job that has finished with this
	 * tree will clear the item (under the same lock) and then close
	 * and destroy the tree; the reference guarantees the tree stays
	 * alive while we search it below.  The reference is released
	 * right after the search returns.
	 */
	VAN_ASSERT(odtree != NULL);
	atomic_fetch_add(&odtree->refcount, 1);
	VAN_MUTEX_UNLOCK(citem, mutex, mtx_ret);
	if (mtx_ret != 0 && ret == 0) {
		ret = mtx_ret;
		goto err;
	}

	ret = odtree->getraw(odtree, key, data, flags);
	atomic_fetch_sub(&odtree->refcount, 1);
	if (ret == 0 || ret == VAN_DELETED)
		break;

	if (ret != VAN_NOTFOUND && ret != VAN_STALE)
		break;

	cur_indx--;
	}

err:
	/*
	 * Nothing to undo: the jumps above leave the search while no mutex
	 * is held, and a mutex that failed to lock is not held either.
	 * A lock failure outranks the search result, so it replaces 'ret'
	 * above; a plain search result is returned as it is.
	 */
ret:
	return (ret);
}
