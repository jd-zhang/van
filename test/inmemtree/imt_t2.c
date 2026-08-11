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
#include "test_common.h"
/* Multi-threaded inmemtree test */

static void *imt_t2_1_popthread(void *obj) {
	POPULATE_DATA *dataobj;
	INMEM_TREE *tree;
	int ret;

	dataobj = (POPULATE_DATA *)obj;
	tree = (INMEM_TREE *)dataobj->handle;
	ret = populate_inmemtree(tree, dataobj->keys, dataobj->ksizes,
	    dataobj->values, dataobj->vsizes, dataobj->cnt);
	CHK_RET(ret, "populate_inmemtree");
	
	return (NULL);
}

int imt_t2_1() {
#define IMT_T2_1_NUM 5
	VAN_TABLE table;
	INMEM_TREE *tree;
	pthread_t threads[IMT_T2_1_NUM];
	POPULATE_DATA objs[IMT_T2_1_NUM], *objp;
	size_t each_cnt;
	int i, ret, cnt;
	void *rptr;
	STRING_TESTDATA_DEFINE;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = _van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "_van_inmemtree_create");
	tree = table.active;

	memset(threads, 0, sizeof(threads));
	cnt = 10000;
	STRING_TESTDATA_INIT;
	each_cnt = cnt / IMT_T2_1_NUM;
	memset(objs, 0, sizeof(objs));
	for (i = 0; i < IMT_T2_1_NUM; i++) {
		objp = &objs[i];
		objp->handle = tree;
		objp->keys = keys + i * each_cnt;
		objp->ksizes = ksizes + i * each_cnt;
		objp->values = values + i * each_cnt;
		objp->vsizes = vsizes + i * each_cnt;
		objp->cnt = each_cnt;
	}

	(void)generate_string_testdata(keys, ksizes, 5, 15, values, vsizes,
	    10, 20, cnt);

	for (i = 0; i < IMT_T2_1_NUM; i++) {
		ret = pthread_create(&threads[i], NULL,
		    imt_t2_1_popthread, &objs[i]);
	}

	for (i = 0; i < IMT_T2_1_NUM; i++) {
		ret = pthread_join(threads[i], &rptr);
	}

	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);

	ret = _van_inmemtree_destory(tree);
	CHK_RET(ret, "_van_inmemtree_destory");

	return (ret);
}
