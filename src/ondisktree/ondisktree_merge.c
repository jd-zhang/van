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

typedef struct _ondisk_merge_item {
	ONDISK_ITEM *kitem;
	ONDISK_ITEM *ditem;
	int idx;
} ONDISK_MERGE_ITEM;

/*
 * The size means the number of elements in the array.items, while
 * the capacity means its last size. The capacity also means
 * the number of cursors, the number of indexes.
 * The finished indicates whether all indexes are 0.
 */
typedef struct _ondisk_merge_item_array {
	ONDISK_MERGE_ITEM *items;
	ONDISK_TREECURSOR **cursors;
	size_t *indexes;
	int finished;
	size_t capacity;
	size_t size;
} ONDISK_MERGE_ITEM_ARRAY;

static int _ondisk_merge_item_cmp(VAN_TABLE *table,
    ONDISK_MERGE_ITEM *item1, ONDISK_MERGE_ITEM *item2, int *cmp);
static int _ondisk_merge_items_compact(VAN_TABLE *table,
    ONDISK_MERGE_ITEM_ARRAY *array);
static int _ondisk_merge_items_fill(VAN_TABLE *table,
    ONDISK_MERGE_ITEM_ARRAY *array);
static int _ondisk_merge_item_insert(VAN_TABLE *table, ONDISK_MERGE_ITEM *items,
    size_t size, ONDISK_MERGE_ITEM *citem);
static int _van_ondisk_merge_init(ONDISK_MERGE_ITEM_ARRAY *array,
    size_t srccnt);
static int _van_ondisk_merge_cleanup(ONDISK_MERGE_ITEM_ARRAY *array,
    size_t srccnt);


static int _ondisk_merge_item_cmp(VAN_TABLE *table,
    ONDISK_MERGE_ITEM *item1, ONDISK_MERGE_ITEM *item2, int *cmp) {
	int ret;

	ret = _van_ondiskitem_cmp(table, item1->kitem, item2->kitem, cmp);
	if (ret != 0)
		return (ret);

	if (*cmp == 0)
		*cmp = -(item1->idx - item2->idx);

	return (ret);
}

static int _ondisk_merge_items_compact(VAN_TABLE *table,
    ONDISK_MERGE_ITEM_ARRAY *array) {
	int i, cmp, ret, total;
	ONDISK_MERGE_ITEM *fitem, *citem;

	ret = 0;
	if (array->size == 0)
		return (ret);

	fitem = &array->items[0];
	array->indexes[fitem->idx] = 1;
	total = 1;
	for (i = 1; i < array->size; i++) {
		citem = &array->items[i];

		ret = _van_ondiskitem_cmp(table, citem->kitem,
		   fitem->kitem, &cmp);
		/* TODO: Check ret */
		VAN_ASSERT(ret == 0);

		if (cmp == 0) {
			array->indexes[citem->idx] = 1;
			VAN_ASSERT(array->size >= 2);
			total++;
		} else
			break;
	}
	VAN_ASSERT(array->size >= total);
	array->size -= total;
	if (array->size > 0)
		memmove(&array->items[0], &array->items[total],
		    array->size * sizeof(ONDISK_MERGE_ITEM));

	return (ret);
}


static int _ondisk_merge_items_fill(VAN_TABLE *table,
    ONDISK_MERGE_ITEM_ARRAY *array) {
	int ret, cmp;
	size_t i;
	ONDISK_ITEM *ik, *id;
	ONDISK_MERGE_ITEM mitem;

	ret = 0;
	for (i = 0; i < array->capacity; i++)  {
		if (array->indexes[i] == 0)
			continue;

		ret = _van_ondisktree_cursor_getmerged(array->cursors[i], NULL,
		    &ik, &id, VAN_NEXT);
		/* TODO: Check ret */
		if (ret == VAN_NOTFOUND) {
			array->indexes[i] = 0;
			ret = 0;
			continue;
		} else if (ret != 0)
			goto err;

		memset(&mitem, 0, sizeof(ONDISK_MERGE_ITEM));
		mitem.kitem = ik;
		mitem.ditem = id;
		mitem.idx = i;

		ret = _ondisk_merge_item_insert(table, array->items,
		    array->size, &mitem);
		/* TODO: Error handling */
		VAN_ASSERT(ret == 0);

		array->size++;
		array->indexes[i] = 0;
	}
	/*
	 * We used to use the fill count to check 'finished',
	 * but no filling does not mean the items array
	 * is empty.
	 */
	if (array->size == 0)
		array->finished = 1;

err:
	return (ret);
}

/*
 * Insert a new item, get from the cursor's next operation.
 * The size increases, but not handled in this function.
 */
static int _ondisk_merge_item_insert(VAN_TABLE *table, ONDISK_MERGE_ITEM *items,
    size_t size, ONDISK_MERGE_ITEM *citem) {
	int i, cmp, ret;

	ret = 0;
	if (size == 0) {
		items[0] = *citem;
	}

	for (i = size; i > 0; i--) {
		ret = _ondisk_merge_item_cmp(table, citem, &items[i-1], &cmp);
		/* TODO: check error */
		VAN_ASSERT(ret == 0);

		if (cmp >= 0) {
			items[i] = *citem;
			break;
		} else {
			items[i] = items[i-1];
			if (i == 1) {
				items[i - 1] = *citem;
				break;
			}
		}
	}

	return (ret);
}

static int _van_ondisk_merge_init(ONDISK_MERGE_ITEM_ARRAY *array, size_t srccnt) {
	int ret;

	ret = 0;
	memset(array, 0, sizeof(ONDISK_MERGE_ITEM_ARRAY));
	ret = _van_calloc(NULL, srccnt, sizeof(ONDISK_TREECURSOR *), &array->cursors);
	ret = _van_calloc(NULL, srccnt, sizeof(ONDISK_MERGE_ITEM), &array->items);
	ret = _van_calloc(NULL, srccnt, sizeof(size_t), &array->indexes);
	array->finished = 0;
	array->capacity = srccnt;
	array->size = 0;

	return ret;
}

static int _van_ondisk_merge_cleanup(ONDISK_MERGE_ITEM_ARRAY *array, size_t srccnt) {
	int ret, ret1;
	size_t i;
	ONDISK_TREECURSOR *cursor;

	ret = 0;
	for (i = 0; i < srccnt; i++) {
		cursor = array->cursors[i];
		ret1 = _van_ondisktree_cursor_cleanup(cursor);
		SET_RET(ret1, ret);
		array->cursors[i] = NULL;
	}
	_van_free(NULL, array->cursors);
	_van_free(NULL, array->items);
	_van_free(NULL, array->indexes);

	return ret;
}

int _van_ondisktree_merge(VAN_TABLE *table, ONDISK_TREE **trees, size_t srccnt,
    ONDISK_TREE **restree) {
	ONDISK_MERGE_ITEM_ARRAY array;
	ONDISK_MERGE_ITEM *item;
	ONDISK_TREE *retree;
	int i, ret;
	char merge_path[VAN_MAXPATHLEN];
	time_t curtime;

	ret = _van_ondisk_merge_init(&array, srccnt);
	/* TODO: Error handling */
	VAN_ASSERT(ret == 0);

	for (i = 0; i < srccnt; i++) {
		ret = _van_ondisktree_cursor_init(trees[i], &array.cursors[i], 0);
		/* TODO: check error */
		VAN_ASSERT(ret == 0);
		array.indexes[i] = 1;
	}

	/*
	 * TODO: Here, we need to generate the filename for the
	 * new merged tree, and setup the merge_path.
	 */
	ret = _van_ondisktree_create(table, &retree);
	/* TODO: check error */
	VAN_ASSERT(ret == 0);

	ret = _van_table_new_odfilename(table, merge_path, VAN_MAXPATHLEN);
	/* TODO: Check ret */
	VAN_ASSERT(ret == 0);

	ret = _van_ondisktree_open(retree, merge_path,
	    VAN_OPEN_CREAT | VAN_OPEN_TRUNC);
	/* TODO: Check ret */
	VAN_ASSERT(ret == 0);

	ret = _van_ondisktree_store_init(retree);
	/* TODO: check error */
	VAN_ASSERT(ret == 0);

	while (!array.finished) {
		ret = _ondisk_merge_items_fill(table, &array);
		/* TODO: check error */
		VAN_ASSERT(ret == 0);
		/*
		 * Here, we fetch the first item, and then insert into the result
		 * tree.
		 */
		if (array.size > 0) {
			item = &array.items[0];
			ret = _van_ondisktree_ondiskitem_addpair(retree,
			    item->kitem, item->ditem);
			/* TODO: check error */
			VAN_ASSERT(ret == 0);

			ret = _ondisk_merge_items_compact(table, &array);
			/* TODO: check error */
			VAN_ASSERT(ret == 0);
		}
	}

	ret = _van_ondisktree_store_finalize(retree);
	/* TODO: check error */
	VAN_ASSERT(ret == 0);

	ret = _van_ondisk_merge_cleanup(&array, srccnt);
	/* TODO: check error */
	VAN_ASSERT(ret == 0);

	*restree = retree;

	return (ret);

}

