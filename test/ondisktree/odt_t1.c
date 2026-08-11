#include "test_common.h"

int odt_t1_1() {
	VAN_TABLE *table;
	VAN_STORE *store;
	INMEM_TREE *tree;
	ONDISK_TREE *odtree;
	int ret, cnt;
	STRING_TESTDATA_DEFINE;
	const char *odtpath;
	size_t rcnt;
	time_t t1, t2;

	odtpath = "test1.ondisk.odt_t1";

	ret = _van_store_create(&store);
	CHK_RET(ret, "_van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "_van_store_open");
	ret = _van_table_create(store, &table);
	CHK_RET(ret, "_van_table_create");
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "_van_table_open");

	ret = _van_inmemtree_create(table, &tree);
	CHK_RET(ret, "_van_inmemtree_create");

	cnt = 100000;
	STRING_TESTDATA_INIT;

	(void)generate_string_testdata(keys, ksizes, 20, 30, values, vsizes,
	    50, 120, cnt);

	fprintf(stderr, "============ 1 Try to put %lu records =========\n",
	    (unsigned long)cnt);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	ret = populate_inmemtree(tree, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "populate_inmemtree");
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");

	fprintf(stderr, "========== 2 Build the ondisk tree ============\n");
	time(&t1);
	ret = _van_ondisktree_create(table, &odtree);
	CHK_RET(ret, "_van_ondisktree_create");
	ret = _van_ondisktree_open(odtree, odtpath,
	    VAN_OPEN_CREAT | VAN_OPEN_TRUNC);
	CHK_RET(ret, "_van_ondisktree_open(VAN_OPEN_CREAT | VAN_OPEN_TRUNC)");
	ret = _van_ondisktree_store_inmem(odtree, tree);
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));


	fprintf(stderr, "======== 3 Dump the tree(Basic Info) =======\n");
	ret = _van_ondisktree_dump(odtree, stderr);

	fprintf(stderr, "======== 4.1 trasverse ondisk tree using cursor(FORWARD) =======\n");
	rcnt = 0;
	ret = verify_ondisktree_cursor_direction(odtree, VAN_NEXT, 0, &rcnt);
	CHK_RET(ret, "verify_ondisktreecursor_with_inmemtreecursor(VAN_NEXT)");
	TEST_ASSERT(rcnt == cnt);

	fprintf(stderr, "======== 4.2 trasverse ondisk tree using cursor(FORWARD, with inmemtree) =======\n");
	rcnt = 0;
	ret = verify_ondisktreecursor_with_inmemtreecursor(tree, odtree, VAN_NEXT, &rcnt);
	CHK_RET(ret, "verify_ondisktreecursor_with_inmemtreecursor(VAN_NEXT)");
	TEST_ASSERT(rcnt == cnt);

	/* We close and reopen the tree here */
	ret = _van_ondisktree_close(odtree, 0);
	CHK_RET(ret, "_van_ondisktree_close - 1");
	odtree = NULL;
	ret = _van_ondisktree_create(table, &odtree);
	CHK_RET(ret, "_van_ondisktree_create");
	ret = _van_ondisktree_open(odtree, odtpath, 0);
	CHK_RET(ret, "_van_ondisktree_open(0)");

	fprintf(stderr, "======== 5.1 trasverse ondisk tree using cursor(BACKWARD) =======\n");
	rcnt = 0;
	ret = verify_ondisktree_cursor_direction(odtree, VAN_PREV, 0, &rcnt);
	CHK_RET(ret, "verify_ondisktreecursor_with_inmemtreecursor(VAN_PREV)");
	TEST_ASSERT(rcnt == cnt);

	fprintf(stderr, "======== 5.2 trasverse ondisk tree using cursor(BACKWARD, with inmemtree) =======\n");
	rcnt = 0;
	ret = verify_ondisktreecursor_with_inmemtreecursor(tree, odtree, VAN_PREV, &rcnt);
	CHK_RET(ret, "verify_ondisktreecursor_with_inmemtreecursor(VAN_PREV)");	
	TEST_ASSERT(rcnt == cnt);

	/* We close and reopen the tree here */
	ret = _van_ondisktree_close(odtree, 0);
	CHK_RET(ret, "_van_ondisktree_close - 2");
	odtree = NULL;
	ret = _van_ondisktree_create(table, &odtree);
	CHK_RET(ret, "_van_ondisktree_create");
	ret = _van_ondisktree_open(odtree, odtpath, 0);
	CHK_RET(ret, "_van_ondisktree_open(0)");

	fprintf(stderr, "======== 6 trasverse ondisk tree using cursor(VAN_SET) =======\n");
	ret = verify_ondisktree_data(odtree, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "verify_ondisktree_data");


	/* We close and reopen the tree here */
	ret = _van_ondisktree_close(odtree, 0);
	CHK_RET(ret, "_van_ondisktree_close - 1");
	odtree = NULL;
	ret = _van_ondisktree_create(table, &odtree);
	CHK_RET(ret, "_van_ondisktree_create");
	ret = _van_ondisktree_open(odtree, odtpath, 0);
	CHK_RET(ret, "_van_ondisktree_open(0)");

	fprintf(stderr, "======== 7 trasverse ondisk tree using cursor(LAST) =======\n");
	ret = verify_ondisktree_cursor_border(odtree, VAN_LAST, 0);
	CHK_RET(ret, "verify_ondisktree_cursor_border(VAN_LAST)");


	fprintf(stderr, "======== 8 trasverse ondisk tree using cursor(FIRST) =======\n");
	ret = verify_ondisktree_cursor_border(odtree, VAN_FIRST, 0);
	CHK_RET(ret, "verify_ondisktree_cursor_border(VAN_FIRST)");	

	fprintf(stderr, "====== 9 Destroy the tree(ondisk and inmemory) ====\n");
	time(&t1);
	_van_alloc_stat_reset();
	ret =  _van_ondisktree_close(odtree, 0);
	ret = _van_inmemtree_destory(tree);
	CHK_RET(ret, "_van_inmemtree_destory");

	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);
	
	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	
	return (ret);
}


int odt_t1_2() {
#define TNUM_ODT_T1_2 5
	VAN_STORE *store;
	VAN_TABLE *table;
	ONDISK_TREE *odtree, *modtree, *odtrees[TNUM_ODT_T1_2];
	int ret, cnt, i, j;
	time_t t1, t2;
	STRING_TESTDATA_DEFINE;

	const size_t record_cnt = 100000;

	memset(odtrees, 0, sizeof(odtrees));
	modtree = NULL;

	cnt = TNUM_ODT_T1_2 * record_cnt;
	STRING_TESTDATA_INIT;

	(void)generate_string_testdata(keys, ksizes, 20, 30,
	    values, vsizes, 50, 120, cnt);

	ret = _van_store_create(&store);
	CHK_RET(ret, "_van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "_van_store_open");
	ret = _van_table_create(store, &table);
	CHK_RET(ret, "_van_table_create");
	ret = table->open(table, "od_table", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "_van_table_open");

	for (j = 0; j < TNUM_ODT_T1_2; j++) {
		fprintf(stderr,
	"============ %d.1 Try to put %lu records to inmemtree-%d =========\n",
		    j, (unsigned long)record_cnt, j);
		_van_alloc_stat_reset();
		_van_inmemtree_rwlock_stat_reset();
		i = j * record_cnt;
		ret = populate_table(table, keys+i, ksizes+i,
		    values+i, vsizes+i, record_cnt);
		CHK_RET(ret, "populate_table");
		_van_inmemtree_rwlock_stat_print(stderr, " ");
		fprintf(stderr, "**** Malloc Statistics ****\n");
		_van_alloc_stat_print(stderr, " ");

		fprintf(stderr,
			"============ %d.2 Try to get %lu records from inmemtree-%d =========\n",
		    j, (unsigned long)record_cnt, j);
		i = j * record_cnt;
		ret = verify_table_data(table, keys+i, ksizes+i,
		    values+i, vsizes+i, record_cnt);
		CHK_RET(ret, "verify_table_data(odtree)");

		if (j+1 < TNUM_ODT_T1_2) {
			fprintf(stderr,
			    "========== %d.3 Build the ondisk tree ============\n", j);
			time(&t1);
			ret = _van_table_dump_inmem(table);
			CHK_RET(ret, "_van_table_dump_inmem");
			time(&t2);
			fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
			odtree = table->odt_items[j].trees[0];
			odtrees[j] = odtree;
			fprintf(stderr, "%d.3.2 Getting records from the odtree:%s\n",
			    j, odtree->file_path);
			i = j * record_cnt;
			ret = verify_ondisktree_data(odtree, keys+i, ksizes+i,
			    values+i, vsizes+i, record_cnt);
			CHK_RET(ret, "verify_ondisktree_data(odtree)");
		}
	}

	ret = _van_table_write_odt_paths(table);
	CHK_RET(ret, "_van_table_write_odt_paths");
	fprintf(stderr, "========== 6 Get all using table (1) ==========\n");
	ret = verify_table_data(table, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "verify_table_data(1)");

	fprintf(stderr, "========== 6 Merge and get (2) ==========\n");
	ret = _van_table_dump_inmem(table);
	CHK_RET(ret, "_van_table_dump_inmem");
	odtree = table->odt_items[TNUM_ODT_T1_2 - 1].trees[0];
	odtrees[TNUM_ODT_T1_2 - 1] = odtree;
	ret = _van_ondisktree_merge(table, odtrees, TNUM_ODT_T1_2, &modtree);
	CHK_RET(ret, "_van_ondisktree_merge");
	ret = verify_ondisktree_data(modtree, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "verify_ondisktree_data(2, modtree)");
	ret = _van_table_write_odt_paths(table);
	CHK_RET(ret, "_van_table_write_odt_paths");

	ret = _van_table_replace_odt_items(table, 0, TNUM_ODT_T1_2 - 1, modtree);
	CHK_RET(ret, "_van_table_replace_odt_items");

	fprintf(stderr, "========== 6 Get using table again (3) ==========\n");
	ret = verify_table_data(table, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "verify_table_data(3)");

	fprintf(stderr, "====== 7 Free the memory and close the table ====\n");
	time(&t1);
	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);
	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

	return (ret);
}

int odt_t1_3() {
#define TNUM_ODT_T1_3 5
	VAN_STORE *store;
	VAN_TABLE *table;
	ONDISK_TREE *odtree;
	ONDISK_TREE *modtree, *odtrees[TNUM_ODT_T1_3];
	int ret, cnt, j, k, rndv, cmp;
	char vbuf[MAX_BUF_LEN];
	VAN_DATUM key, value;
	time_t t1, t2;
	size_t vsz, opsz, total, dupcount;
	const size_t record_cnt = 100000;
	int *ops, *ops_all;
	STRING_TESTDATA_DEFINE;

	memset(odtrees, 0, sizeof(odtrees));
	modtree = NULL;

	cnt = record_cnt;
	STRING_TESTDATA_INIT;
	opsz = cnt * sizeof(int);
	ops = (int *)malloc(opsz);
	TEST_ASSERT(ops != NULL);
	ops_all = (int *)malloc(opsz);
	TEST_ASSERT(ops_all != NULL);

	(void)generate_string_testdata(keys, ksizes, 20, 30,
	    values, vsizes, 50, 120, cnt);

	ret = _van_store_create(&store);
	CHK_RET(ret, "_van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "_van_store_open");
	ret = _van_table_create(store, &table);
	CHK_RET(ret, "_van_table_create");
	ret = table->open(table, "od_table", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "_van_table_open");

	fprintf(stderr, "============ 1 Intial put %lu records =========\n",
	    (unsigned long)record_cnt);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	ret = populate_table(table, keys, ksizes, values, vsizes, cnt);
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");

	fprintf(stderr, "============ 2 Intial get %lu records =========\n",
	    (unsigned long)record_cnt);
	ret = verify_table_data(table, keys, ksizes, values, vsizes, cnt);


	for (j = 0; j < TNUM_ODT_T1_3; j++) {
		memset(ops, 0, opsz);
		/* Generate operations */
		fprintf(stderr, "======= 3.%d.1 Generate Operations =======\n", (int)j);
		for (k = 0; k < record_cnt; k++) {
			rndv = rand() % 15;
			if (rndv > 2)
				ops[k] = OP_NOOP;
			else
				ops[k] = rndv;
		}
		fprintf(stderr, "======== 3.%d.2 Perform Operations =======\n", (int)j);
		/* Perform Operations */
		for (k = 0; k < record_cnt; k++) {
			if (ops[k] == OP_NOOP)
				continue;
			memset(&key, 0, sizeof(key));
			key.data = keys[k];
			key.size = ksizes[k];
			if (ops[k] == OP_DELETE) {
				free(values[k]);
				values[k] = NULL;
				vsizes[k] = 0;
				ret = table->del(table, &key, 0);
				if (ops_all[k] == OP_DELETE) {
					TEST_ASSERT(ret == VAN_NOTFOUND || ret == 0);
				} else {
					CHK_RET(ret, "table->del");
				}
			} else if (ops[k] == OP_UPDATE) {
				free(values[k]);
				getrandomstring2(vbuf, 50, 120);
				vsizes[k] = vsz = strlen(vbuf) + 1;
				values[k] = (char *)malloc(vsz);
				TEST_ASSERT(values[k] != NULL);
				memcpy(values[k], vbuf, vsz);
				memset(&value, 0, sizeof(value));
				value.data = values[k];
				value.size = vsizes[k];
				ret = table->put(table, &key, &value, 0);
				CHK_RET(ret, "table->put");
			}
			/* Update ops_all */
			ops_all[k] = ops[k];
		}
		fprintf(stderr, "======== 3.%d.3 Verify Operations =======\n", (int)j);
		/* Verify Operations */
		for (k = 0; k < record_cnt; k++) {
			memset(&key, 0, sizeof(key));
			key.data = keys[k];
			key.size = ksizes[k];
			memset(&value, 0, sizeof(value));
			ret = table->get(table, &key, &value, 0);
			if (ops_all[k] == OP_DELETE) {
				TEST_ASSERT(ret == VAN_NOTFOUND);
			} else {
				CHK_RET(ret, "table->get");
				cmp = strcmp(values[k], (char *)value.data);
				TEST_ASSERT(cmp == 0);
			}
		}

		if (j + 1 < TNUM_ODT_T1_3) {
			/* Dump */
			fprintf(stderr, "======== 3.%d.4 Dump and Verify Operations again =======\n", (int)j);
			time(&t1);
			ret = _van_table_dump_inmem(table);
			CHK_RET(ret, "_van_table_dump_inmem");
			time(&t2);
			fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

			/* Verify Operations Again */
			for (k = 0; k < record_cnt; k++) {
				memset(&key, 0, sizeof(key));
				key.data = keys[k];
				key.size = ksizes[k];
				memset(&value, 0, sizeof(value));
				ret = table->get(table, &key, &value, 0);
				if (ops_all[k] == OP_DELETE) {
					TEST_ASSERT(ret == VAN_NOTFOUND);
				} else {
					CHK_RET(ret, "table->get");
					cmp = strcmp(values[k], (char *)value.data);
					TEST_ASSERT(cmp == 0);
				}
			}
			odtrees[j] = table->odt_items[j].trees[0];

		}

	}

	ret = _van_table_write_odt_paths(table);
	CHK_RET(ret, "_van_table_write_odt_paths");

	fprintf(stderr, "========== 4 Merge and get ==========\n");
	ret = _van_ondisktree_merge(table, odtrees, TNUM_ODT_T1_3 - 1, &modtree);
	CHK_RET(ret, "_van_ondisktree_merge");
	total = 0;
	dupcount = 0;
	odtree = modtree;
	ret = check_ondisktree_duplicate(odtree, 100, &total, &dupcount);
	CHK_RET(ret, "check_ondisktree_duplicate");
	fprintf(stderr, "Items in merged tree: total(%lu), dupcount(%lu)\n",
	    (unsigned long)total, (unsigned long)dupcount);
	ret = _van_table_replace_odt_items(table, 0, TNUM_ODT_T1_3 - 2, modtree);
	CHK_RET(ret, "_van_table_replace_odt_items");
	/* Verify Operations Again */
	for (k = 0; k < record_cnt; k++) {
		memset(&key, 0, sizeof(key));
		key.data = keys[k];
		key.size = ksizes[k];
		memset(&value, 0, sizeof(value));
		ret = table->get(table, &key, &value, 0);

		if (ops_all[k] == OP_DELETE) {
			TEST_ASSERT(ret == VAN_NOTFOUND);
		} else {
			CHK_RET(ret, "table->get");
			cmp = strcmp(values[k], (char *)value.data);
			TEST_ASSERT(cmp == 0);
		}
	}
	ret = _van_table_write_odt_paths(table);
	CHK_RET(ret, "_van_table_write_odt_paths");


	fprintf(stderr, "====== 5 Free the memory and close the table ====\n");
	time(&t1);
	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);
	free(ops);
	free(ops_all);
	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

	return (ret);
}
