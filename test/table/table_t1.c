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
static void *tb_t1_popthread(void *obj) {
	POPULATE_DATA *dataobj;
	VAN_TABLE *table;
	int ret;

	dataobj = (POPULATE_DATA *)obj;
	table = (VAN_TABLE *)dataobj->handle;
	ret = populate_table(table, dataobj->keys, dataobj->ksizes,
	    dataobj->values, dataobj->vsizes, dataobj->cnt);
	CHK_RET(ret, "populate_table");

	return (NULL);
}

/*
 test populate_table with multiple threads, but avoid inmem dumping.
 */
#define TB_T1_NUM 5
int table_t1_1() {
	VAN_TABLE *table;
	VAN_STORE *store;
	pthread_t threads[TB_T1_NUM];
	POPULATE_DATA objs[TB_T1_NUM], *objp;
	size_t each_cnt;
	int i, ret, cnt;
	void *rptr;
	STRING_TESTDATA_DEFINE;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, GIGABYTE);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");
	ret = table->start_threads(table);
	CHK_RET(ret, "table->start_threads");

	memset(threads, 0, sizeof(threads));
	cnt = 1000000;
	STRING_TESTDATA_INIT;
	each_cnt = cnt / TB_T1_NUM;
	memset(objs, 0, sizeof(objs));
	for (i = 0; i < TB_T1_NUM; i++) {
		objp = &objs[i];
		objp->handle = table;
		objp->keys = keys + i * each_cnt;
		objp->ksizes = ksizes + i * each_cnt;
		objp->values = values + i * each_cnt;
		objp->vsizes = vsizes + i * each_cnt;
		objp->cnt = each_cnt;
	}

	(void)generate_string_testdata(keys, ksizes, 20, 30, values, vsizes,
	    50, 120, cnt);

	for (i = 0; i < TB_T1_NUM; i++) {
		ret = pthread_create(&threads[i], NULL,
		    tb_t1_popthread, &objs[i]);
	}

	for (i = 0; i < TB_T1_NUM; i++) {
		ret = pthread_join(threads[i], &rptr);
	}

	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_strarray_print(store->log_handle->lognames, stderr, "\n");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	return (ret);

}

/*
 test populate_table using single thread(easy for debugging), 
 but set inmem_threshold to a small value, and
 set merge_config to big values, to make dump but avoid merging.
 */

int table_t1_1b() {
	VAN_TABLE *table;
	VAN_STORE *store;
	int ret, cnt;
	STRING_TESTDATA_DEFINE;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, MEGABYTE);
	table->set_merge_config(table, 100000,200000);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");
	ret = table->start_threads(table);
	CHK_RET(ret, "table->start_threads");

	cnt = 200000;
	STRING_TESTDATA_INIT;

	(void)generate_string_testdata(keys, ksizes, 20, 30, values, vsizes,
	    50, 120, cnt);

	ret = populate_table(table, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "populate_table");

	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	return (ret);

}

/*
 test populate_table using multi threads, 
 but set inmem_threshold to a small value, and
 set merge_config to big values, to make dump but avoid merging.

 also, verify records using table->get()
 */
int table_t1_1c() {
	VAN_TABLE *table;
	VAN_STORE *store;
	pthread_t threads[TB_T1_NUM];
	POPULATE_DATA objs[TB_T1_NUM], *objp;
	size_t each_cnt;
	int i, ret, cnt;
	void *rptr;
	STRING_TESTDATA_DEFINE;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, MEGABYTE);
	
	table->set_merge_config(table, 100000,200000);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");
	ret = table->start_threads(table);
	CHK_RET(ret, "table->start_threads");

	memset(threads, 0, sizeof(threads));
	if (is_cygwin()) {
		cnt = 100000;
	} else {
		cnt = 1000000;
	}
	STRING_TESTDATA_INIT;
	each_cnt = cnt / TB_T1_NUM;
	memset(objs, 0, sizeof(objs));
	for (i = 0; i < TB_T1_NUM; i++) {
		objp = &objs[i];
		objp->handle = table;
		objp->keys = keys + i * each_cnt;
		objp->ksizes = ksizes + i * each_cnt;
		objp->values = values + i * each_cnt;
		objp->vsizes = vsizes + i * each_cnt;
		objp->cnt = each_cnt;
	}

	fprintf(stderr, "============ 1 generate %lu records =========\n",
	    (unsigned long)cnt);
	(void)generate_string_testdata(keys, ksizes, 20, 30, values, vsizes,
	    50, 120, cnt);

	fprintf(stderr, "============ 2 populate %lu records =========\n",
	    (unsigned long)cnt);
	for (i = 0; i < TB_T1_NUM; i++) {
		ret = pthread_create(&threads[i], NULL,
		    tb_t1_popthread, &objs[i]);
	}

	for (i = 0; i < TB_T1_NUM; i++) {
		ret = pthread_join(threads[i], &rptr);
	}

	fprintf(stderr, "============ 3 verify %lu records using table =========\n",
	    (unsigned long)cnt);
	ret = verify_table_data(table, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "verify_table_data");

	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	return (ret);
}

/*
 test populate_table using multi threads, 
 but set inmem_threshold to a small value.
 this will make dump/merge both happen.

 keep verifying records during the dumping and merging process.
 */
int table_t1_1d() {
	VAN_TABLE *table;
	VAN_STORE *store;
	pthread_t threads[TB_T1_NUM];
	POPULATE_DATA objs[TB_T1_NUM], *objp;
	size_t each_cnt;
	int i, ret, cnt;
	void *rptr;
	STRING_TESTDATA_DEFINE;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, MEGABYTE);
	table->set_merge_config(table, 4,6);
	table->set_flags(table, VAN_TABLE_VERBOSE_MERGE);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");
	ret = table->start_threads(table);
	CHK_RET(ret, "table->start_threads");

	memset(threads, 0, sizeof(threads));
	if (is_cygwin()) {
		cnt = 100000;
	} else {
		cnt = 1000000;
	}
	STRING_TESTDATA_INIT;
	each_cnt = cnt / TB_T1_NUM;
	memset(objs, 0, sizeof(objs));
	for (i = 0; i < TB_T1_NUM; i++) {
		objp = &objs[i];
		objp->handle = table;
		objp->keys = keys + i * each_cnt;
		objp->ksizes = ksizes + i * each_cnt;
		objp->values = values + i * each_cnt;
		objp->vsizes = vsizes + i * each_cnt;
		objp->cnt = each_cnt;
	}

	fprintf(stderr, "============ 1 generate %lu records =========\n",
	    (unsigned long)cnt);
	(void)generate_string_testdata(keys, ksizes, 20, 30, values, vsizes,
	    50, 120, cnt);

	fprintf(stderr, "============ 2 populate %lu records =========\n",
	    (unsigned long)cnt);
	for (i = 0; i < TB_T1_NUM; i++) {
		ret = pthread_create(&threads[i], NULL,
		    tb_t1_popthread, &objs[i]);
	}

	for (i = 0; i < TB_T1_NUM; i++) {
		ret = pthread_join(threads[i], &rptr);
	}

	fprintf(stderr, "============ 3 verify %lu records using table during merging =========\n",
	    (unsigned long)cnt);
	for (i = 0; i < 30; i++) {
		fprintf(stderr, "============ loop.%d verify %lu records using table =========\n",
		    i, (unsigned long)cnt);
		ret = verify_table_data(table, keys, ksizes, values, vsizes, cnt);
		CHK_RET_NOEXIT(ret, "verify_table_data");
		table->show_odt_items(table, stderr);
	}
	/*
	ret = verify_table_data(table, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "verify_table_data");
	*/

	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	return (ret);
}

/*
 Test populate_table using multiple threads with a small inmem_threshold
 (1MB), so that data is dumped to disk during populate.

 Verify the records using the table interface right after populate, then
 close and reopen the table with threads auto-started to verify the
 records are restored from the ondisk files, then print the log names.
 */
int table_t1_2() {
	VAN_TABLE *table;
	VAN_STORE *store;
	pthread_t threads[TB_T1_NUM];
	POPULATE_DATA objs[TB_T1_NUM], *objp;
	size_t each_cnt;
	int i, ret, cnt;
	void *rptr;
	STRING_TESTDATA_DEFINE;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, MEGABYTE);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");
	ret = table->start_threads(table);
	CHK_RET(ret, "table->start_threads");

	memset(threads, 0, sizeof(threads));
	cnt = 200000;
	STRING_TESTDATA_INIT;
	each_cnt = cnt / TB_T1_NUM;
	memset(objs, 0, sizeof(objs));
	for (i = 0; i < TB_T1_NUM; i++) {
		objp = &objs[i];
		objp->handle = table;
		objp->keys = keys + i * each_cnt;
		objp->ksizes = ksizes + i * each_cnt;
		objp->values = values + i * each_cnt;
		objp->vsizes = vsizes + i * each_cnt;
		objp->cnt = each_cnt;
	}

	fprintf(stderr, "============ 1 generate %lu records =========\n",
	    (unsigned long)cnt);
	(void)generate_string_testdata(keys, ksizes, 20, 30, values, vsizes,
	    50, 120, cnt);

	fprintf(stderr, "============ 2 populate %lu records =========\n",
	    (unsigned long)cnt);
	for (i = 0; i < TB_T1_NUM; i++) {
		ret = pthread_create(&threads[i], NULL,
		    tb_t1_popthread, &objs[i]);
	}

	for (i = 0; i < TB_T1_NUM; i++) {
		ret = pthread_join(threads[i], &rptr);
	}

	fprintf(stderr, "============ 3 verify %lu records using table =========\n",
	    (unsigned long)cnt);
	ret = verify_table_data(table, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "verify_table_data");

	fprintf(stderr, "============ 4 close table =========\n");
	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");
	table = NULL;
	store = NULL;

	fprintf(stderr, "============ 5 reopen table from ondisk =========\n");
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	ret = table->open(table, "test1", 0);
	CHK_RET(ret, "table->open");

	fprintf(stderr, "============ 6 verify %lu records after reopen =========\n",
	    (unsigned long)cnt);
	ret = verify_table_data(table, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "verify_table_data");

	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);

	fprintf(stderr, "============ 7 print log file names =========\n");
	ret = van_strarray_print(store->log_handle->lognames, stderr, "\n");
	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	return (ret);

}
