#include "test_common.h"


static void *tb_t1_1_popthread(void *obj) {
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


int table_t1_1() {
#define TB_T1_1_NUM 5
	VAN_TABLE *table;
	VAN_STORE *store;
	pthread_t threads[TB_T1_1_NUM];
	POPULATE_DATA objs[TB_T1_1_NUM], *objp;
	size_t each_cnt;
	int i, ret, cnt;
	void *rptr;
	STRING_TESTDATA_DEFINE;

	ret = _van_store_create(&store);
	CHK_RET(ret, "_van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "_van_store_open");
	ret = _van_table_create(store, &table);
	CHK_RET(ret, "_van_table_create");
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "_van_table_open");
	ret = _van_table_start_threads(table);
	CHK_RET(ret, "_van_table_start_threads");

	memset(threads, 0, sizeof(threads));
	cnt = 1000000;
	STRING_TESTDATA_INIT;
	each_cnt = cnt / TB_T1_1_NUM;
	memset(objs, 0, sizeof(objs));
	for (i = 0; i < TB_T1_1_NUM; i++) {
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

	for (i = 0; i < TB_T1_1_NUM; i++) {
		ret = pthread_create(&threads[i], NULL,
		    tb_t1_1_popthread, &objs[i]);
	}

	for (i = 0; i < TB_T1_1_NUM; i++) {
		ret = pthread_join(threads[i], &rptr);
	}

	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	ret = _van_store_create(&store);
	CHK_RET(ret, "_van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "_van_store_open");
	ret = _van_strarray_print(store->log_handle->lognames, stderr, "\n");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	return (ret);

}
