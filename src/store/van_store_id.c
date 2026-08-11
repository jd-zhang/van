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
#include "van_table.h"

/* 
 * The caller should have the store->tid_mtx locked,
 * or it is the init function or called by the init function.
 */
static int _van_store_id_initfreelinks(VAN_STORE *store, 
    int st, int et) {
	int ret, i;
	VAN_STORE_IDMAP *tid;

	ret = 0;
	for (i = st; i < et; i++) {
		tid = &store->tids[i];
		if (i == et - 1)
			tid->nextfree = -1;
		else
			tid->nextfree = i + 1;
		tid->table = NULL;
		tid->tid = (uint32)-1;
	}

	return (ret);
}

int _van_store_id_init(VAN_STORE *store) {
	int i, ret;
	VAN_STORE_IDMAP *tid;

	VAN_MUTEX_INIT(store, tid_mtx, ret);
	store->tids = store->prealloc_tids;
	store->tid_capacity = PREALLOC_TIDNUM;
	store->tid_cnt = 0;
	(void)_van_store_id_initfreelinks(store, 0, store->tid_capacity);
	store->tid_free = 0;

	return (ret);
}

int _van_store_id_destroy(VAN_STORE *store) {
	int i, ret;

	if (store->tids != store->prealloc_tids) {
		_van_free(NULL, store->tids);
		store->tids = NULL;
		store->tid_cnt = 0;
		/* 
		 * store->tid_capacity, store->tid_cnt, store->tid_free
		 * will not be reset.
		 */
	}
	VAN_MUTEX_DESTROY(store, tid_mtx, ret);

	return (ret);
}

int _van_store_id_bind(VAN_STORE *store, VAN_TABLE *table) {
	int locked, ret, ret1, free, newfree;
	size_t capacity;
	VAN_STORE_IDMAP *newidmap, *freeid;

	newidmap = NULL;
	locked = 0;

	VAN_MUTEX_LOCK(store, tid_mtx, ret);
	CHK_ERR(ret);
	locked = 1;
	capacity = store->tid_capacity;
	if (store->tid_cnt == capacity) {
		capacity *= 2;
		ret = _van_calloc(NULL, capacity,
		    sizeof(VAN_STORE_IDMAP), &newidmap);
		CHK_ERR(ret);
		memcpy(newidmap, store->tids,
		    sizeof(VAN_STORE_IDMAP) * store->tid_capacity);
		if (store->tids != store->prealloc_tids)
			_van_free(NULL, store->tids);
		store->tids = newidmap;
		store->tid_capacity = capacity;
		(void)_van_store_id_initfreelinks(store,
		    store->tid_cnt, store->tid_capacity);
		store->tid_free = store->tid_cnt;
	}

	VAN_ASSERT(store->tid_cnt < store->tid_capacity);
	free = store->tid_free;
	VAN_ASSERT(free != -1);
	freeid = &store->tids[free];
	newfree = freeid->nextfree;

	freeid->tid = (uint32)free;
	freeid->table = table;
	freeid->nextfree = -1;
	table->tid = freeid->tid;

	store->tid_free = newfree;
	store->tid_cnt += 1;

err:
	if (locked) {
		VAN_MUTEX_UNLOCK(store, tid_mtx, ret1);
		SET_RET(ret1, ret);
	}
	return (ret);
}

int _van_store_id_unbind(VAN_STORE *store, VAN_TABLE *table) {
	int locked, ret, ret1, free;
	VAN_STORE_IDMAP *freeid;

	VAN_MUTEX_LOCK(store, tid_mtx, ret);
	CHK_ERR(ret);
	locked = 1;
	VAN_ASSERT(store->tid_cnt > 0);
	VAN_ASSERT(table->tid != -1);

	free = (int)table->tid;
	freeid = &store->tids[free];
	
	freeid->nextfree = store->tid_free;
	freeid->tid = (uint32)-1;
	freeid->table = NULL;
	
	store->tid_free = free;
	store->tid_cnt--;

err:
	if (locked) {
		VAN_MUTEX_UNLOCK(store, tid_mtx, ret1);
		SET_RET(ret1, ret);
	}
	return (ret);
}
