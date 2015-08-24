#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"
#include "van_ondisk_tree.h"

int _van_table_op_create(VAN_TABLE *table, VAN_OPERATION **tbopp, flags_t cflag) {
	int ret;
	VAN_OPERATION *tbop;

	VAN_ASSERT(tbopp != NULL);
	tbop = NULL;

	ret = _van_calloc(NULL, 1, sizeof(VAN_OPERATION), &tbop);
	if (ret != 0)
		return (ret);

	tbop->table = table;
	F_SET(tbop, cflag);
	ret = _van_table_op_init(tbop);
	if (ret == 0)
		*tbopp = tbop;

	return (ret);
}

int _van_table_op_init(VAN_OPERATION *tbop) {
	int ret;
	VAN_TABLE *table;

	/* create ticks */
	ret = _van_table_curticks(tbop->table, &tbop->create_ticks);
	if (ret != 0)
		goto ret;

	/* Member functions */
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
ret:		
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

	ret = 0;
	table = tbop->table;
	active = table->active;

	/* 
	 * The active->del should return VAN_READONLY after it is frozen,
	 * before it is freed completely.
	 */
	while ((ret = active->del(active, key, flags)) == VAN_READONLY) {
		active = table->active;
	}

	return (ret);
}

int _van_table_op_put(VAN_OPERATION *tbop, VAN_DATUM *key,
    VAN_DATUM *data, flags_t flags) {
	int ret;
	VAN_TABLE *table;
	INMEM_TREE *active;

	ret = 0;
	table = tbop->table;
	active = table->active;
	/* 
	 * The active->put should return VAN_READONLY after it is frozen,
	 * before it is freed completely.
	 */
	while ((ret = active->put(active, key, data, flags)) == VAN_READONLY) {
		active = table->active;
	}

	return (ret);
}

int _van_table_op_get(VAN_OPERATION *tbop, VAN_DATUM *key,
    VAN_DATA *data, flags_t flags) {
	int ret;
	VAN_TABLE *table;
	INMEM_TREE *active, *frozen;
	VAN_ONDISK_TREE *odtree;
	size_t odt_top;
	int cur_indx, gonext, changed;
	VAN_TABLE_ODT_ITEM *citem;

	ret = 0;
	active = table->active;
	frozen = table->frozen;

	/* 
	 * When it is found in the active (exists or deleted),
	 * We get the item. Otherwise, it will return VAN_NOTFOUND
	 * if the active is not freed, otherwise, it will return
	 * VAN_STALE if it is freed.
	 *
	 * Other return values means error.
	 */
	ret = active->get(active, key, data, flags);
	if (ret == 0 || ret == VAN_DELETED)
		goto ret;

	if (ret != VAN_NOTFOUND && ret != VAN_STALE)
		goto ret;
	/*
	 * Like above, we will stop if it is found(exists or deleted),
	 * and return VAN_STALE or VAN_NOTFOUND.
	 */
	ret = frozen->get(frozen, key, data, flags);
	if (ret == 0 || ret == VAN_DELETED)
		goto ret;

	if (ret != VAN_NOTFOUND && ret != VAN_STALE)
		goto ret;

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
	VAN_MUTEX_LOCK(table, odt_mtx, ret);
	odt_top = table->odt_top;
	VAN_MUTEX_LOCK(table, odt_mtx, ret);
	cur_indx = (int)odt_top;

	while (cur_indx >= 0) {
		gonext = 0;
		citem = &table->odt_items[cur_indx];
		VAN_MUTEX_LOCK(citem, mutex, ret);
		odtree = citem->trees[0];
		/* 
		 * First check the table_ticks, and skip to next
		 * if it is generated later than the operation.
		 *
		 * If the item is not used, we can also goto next.
		 */
		if (F_ISSET(citem, VAN_ITEM_UNUSED) ||
		    (citem->table_ticks > tbop->create_ticks)) {
			cur_indx--;
			gonext = 1;
			goto unlock_item;
		}

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
				if (changed) {
					VAN_MUTEX_UNLOCK(citem, mutex, ret);
					citem = table->odt_items[citem->merged_indx];
					VAN_MUTEX_LOCK(citem, mutex, ret);
					cur_indx = citem->merged_indx;
				}
				VAN_ASSERT(citem->table_ticks <= citem->merged_ticks);
				odtree = citem->trees[1];
			}
		}
unlock_item:
		VAN_MUTEX_UNLOCK(citem, mutex, ret);
		if (gonext)
			continue;

		ret = odtree->get(odtree, key, data, flags);
		if (ret == 0 || ret == VAN_DELETED)
			break;

		if (ret != VAN_NOTFOUND && ret != VAN_STALE)
			break;

		cur_indx--;
	}

ret:
	return (ret);
}
