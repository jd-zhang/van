#include "test_common.h"

int imt_t1_1() {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret, cnt, i, klen, vlen;
	char kbuf[MAX_BUF_LEN], vbuf[MAX_BUF_LEN];
	VAN_DATUM key, value;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = _van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "_van_inmemtree_create");
	tree = table.active;

	cnt = 100;
	memset(&key, 0, sizeof(VAN_DATUM));
	memset(&value, 0, sizeof(VAN_DATUM));
	key.data = kbuf;
	value.data = vbuf;

	for (i = 0; i < cnt; i++) {
		klen = getrandomstring2(kbuf, 5, 15);
		vlen = getrandomstring2(vbuf, 10, 20);
		key.size = klen;
		value.size = vlen;
		ret = tree->put(tree, &key, &value, 0);
		CHK_RET(ret, "tree->put");

	}
	
	ret = _van_inmemtree_destory(tree);
	CHK_RET(ret, "_van_inmemtree_destory");

	
	return (ret);
}

int imt_t1_2(int verbose) {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret, cnt, i, rcnt;
	char kbuf[MAX_BUF_LEN], vbuf[MAX_BUF_LEN];
	VAN_DATUM key, value;
	INMEM_TREECURSOR *cursor;
	FILE *outf;
	time_t t1, t2;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = _van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "_van_inmemtree_create");
	tree = table.active;

	if (verbose)
		outf = fopen("imt_t1_2.out", "w");

	/* 
	 * When there are so many records,
	 * the speed slows quickly, since
	 * we haven't made good use of
	 * the memory.
	 */
	cnt = 1000000;
	memset(&key, 0, sizeof(VAN_DATUM));
	memset(&value, 0, sizeof(VAN_DATUM));
	key.data = kbuf;
	value.data = vbuf;

	fprintf(stderr, "============ 1 Try to put %lu records =========\n",
	    (unsigned long)cnt);
	if (verbose)
		fprintf(outf, "============ 1 Try to put %lu records =========\n",
		    (unsigned long)cnt);
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (i = 0; i < cnt; i++) {
		sprintf(kbuf, "KEY_%d", i);
		sprintf(vbuf, "DATA_%d", i);
		key.size = strlen(kbuf) + 1;
		value.size = strlen(vbuf) + 1;
		ret = tree->put(tree, &key, &value, 0);
		CHK_RET(ret, "tree->put");
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");

	fprintf(stderr, "========== 2 Dump leaves after putting ========\n");
	if (verbose) {
		fprintf(outf, "========== 2 Dump leaves after putting ========\n");
		(void)_van_inmemtree_dump_all_leaves(tree, outf);
	}

	fprintf(stderr, "========== 3 Get using tree->get() ============\n");
	if (verbose)
		fprintf(outf, "========== 3 Get using tree->get() ============\n");
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (i = 0; i < cnt; i++) {
		sprintf(kbuf, "KEY_%d", i);
		key.size = strlen(kbuf) + 1;
		ret = tree->get(tree, &key, &value, 0);
		CHK_RET(ret, "tree->get");
		memcpy(vbuf, value.data, value.size);
		if (verbose)
			fprintf(outf, "%s:%s\n", kbuf, vbuf);
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)cnt);
	if (verbose)
		fprintf(outf, " -- Get %lu records --\n",
		    (unsigned long)cnt);

	fprintf(stderr, "========== 4 Get using cursor->get(VAN_SET) ============\n");
	if (verbose)
		fprintf(outf, "========== 4 Get using cursor->get(VAN_SET) ============\n");
	time(&t1);
	ret = _van_inmemtree_cursor_init(tree, &cursor, 0);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	rcnt = 0;
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (i = 0; i < cnt; i++) {
		sprintf(kbuf, "KEY_%d", i);
		key.size = strlen(kbuf) + 1;
		ret = cursor->get(cursor, &key, &value, VAN_SET);
		CHK_RET(ret, "cursor->get(VAN_SET)");
		memcpy(vbuf, value.data, value.size);
		if (verbose)
			fprintf(outf, "%s:%s\n", kbuf, vbuf);
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)cnt);
	if (verbose)
		fprintf(outf, " -- Get %lu records --\n",
		    (unsigned long)cnt);
	ret = _van_inmemtree_cursor_cleanup(cursor);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");

	fprintf(stderr, "========== 5 Get using VAN_NEXT ============\n");
	if (verbose)
		fprintf(outf, "========== 5 Get using VAN_NEXT ============\n");
	ret = _van_inmemtree_cursor_init(tree, &cursor, 0);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	rcnt = 0;
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (ret = 0; ret == 0;) {
		ret = cursor->get(cursor, &key, &value, VAN_NEXT);
		if (ret == 0) {
			if (verbose)
				fprintf(outf, "%s:%s\n", (char *)key.data, 
			 	   (char *)value.data);
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_NEXT)");
		}
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)rcnt);
	if (verbose)
		fprintf(outf, " -- Get %lu records --\n",
		    (unsigned long)rcnt);
	ret = _van_inmemtree_cursor_cleanup(cursor);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");

	fprintf(stderr, "========== 6 Get using VAN_PREV ============\n");
	if (verbose)
		fprintf(outf, "========== 6 Get using VAN_PREV ============\n");
	ret = _van_inmemtree_cursor_init(tree, &cursor, 0);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	rcnt = 0;
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (ret = 0; ret == 0;) {
		ret = cursor->get(cursor, &key, &value, VAN_PREV);
		if (ret == 0) {
			if (verbose)
				fprintf(outf, "%s:%s\n", (char *)key.data, 
			 	   (char *)value.data);
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_PREV)");
		}
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)rcnt);
	if (verbose)
		fprintf(outf, " -- Get %lu records --\n",
		    (unsigned long)rcnt);
	ret = _van_inmemtree_cursor_cleanup(cursor);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");\


	fprintf(stderr, "========== 7 Get using VAN_NEXT(VAN_WRITECURSOR) ============\n");
	if (verbose)
		fprintf(outf, "========== 7 Get using VAN_NEXT(VAN_WRITECURSOR) ============\n");
	ret = _van_inmemtree_cursor_init(tree, &cursor, VAN_WRITECURSOR);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	rcnt = 0;
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (ret = 0; ret == 0;) {
		ret = cursor->get(cursor, &key, &value, VAN_NEXT);
		if (ret == 0) {
			if (verbose)
				fprintf(outf, "%s:%s\n", (char *)key.data, 
			 	   (char *)value.data);
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_NEXT)");
		}
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)rcnt);
	if (verbose)
		fprintf(outf, " -- Get %lu records --\n",
		    (unsigned long)rcnt);
	ret = _van_inmemtree_cursor_cleanup(cursor);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");

	fprintf(stderr, "========== 8 Get using VAN_PREV(VAN_WRITECURSOR) ============\n");
	if (verbose)
		fprintf(outf, "========== 8 Get using VAN_PREV(VAN_WRITECURSOR) ============\n");
	ret = _van_inmemtree_cursor_init(tree, &cursor, VAN_WRITECURSOR);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	rcnt = 0;
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (ret = 0; ret == 0;) {
		ret = cursor->get(cursor, &key, &value, VAN_PREV);
		if (ret == 0) {
			if (verbose)
				fprintf(outf, "%s:%s\n", (char *)key.data, 
			 	   (char *)value.data);
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_PREV)");
		}
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)rcnt);
	if (verbose)
		fprintf(outf, " -- Get %lu records --\n",
		    (unsigned long)rcnt);
	ret = _van_inmemtree_cursor_cleanup(cursor);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");

	fprintf(stderr, "========== 9 Destroy the tree ============\n");
	time(&t1);
	_van_alloc_stat_reset();
	ret = _van_inmemtree_destory(tree);
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	CHK_RET(ret, "_van_inmemtree_destory");
	if (verbose)
		fclose(outf);
	
	return (ret);
}

int imt_t1_3() {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret, cnt, i, rcnt, cmp;
	char kbuf[MAX_BUF_LEN], vbuf[MAX_BUF_LEN];
	VAN_DATUM key, value;
	INMEM_TREECURSOR *cursor;
	time_t t1, t2;
	char **keys, **values;
	size_t ksz, vsz;
	size_t *ksizes, *vsizes;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = _van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "_van_inmemtree_create");
	tree = table.active;

	cnt = 1000000;
	keys = (char **)malloc(cnt * sizeof(char *));
	TEST_ASSERT(keys != NULL);
	ksizes = (size_t *)malloc(cnt * sizeof(size_t));
	TEST_ASSERT(ksizes != NULL);
	values = (char **)malloc(cnt * sizeof(char *));
	TEST_ASSERT(values != NULL);
	vsizes = (size_t *)malloc(cnt * sizeof(size_t));
	TEST_ASSERT(vsizes != NULL);

	for (i = 0; i < cnt; i ++) {
		getrandomstring2(kbuf, 20, 30);
		getrandomstring2(vbuf, 50, 120);
		ksizes[i] = ksz = strlen(kbuf) + 1;
		vsizes[i] = vsz = strlen(vbuf) + 1;
		keys[i] = (char *)malloc(ksz);
		TEST_ASSERT(keys[i] != NULL);
		memcpy(keys[i], kbuf, ksz);
		values[i] = (char *)malloc(vsz);
		TEST_ASSERT(values[i] != NULL);
		memcpy(values[i], vbuf, vsz);
	}

	/* 
	 * When there are so many records,
	 * the speed slows quickly, since
	 * we haven't made good use of
	 * the memory.
	 */
	memset(&key, 0, sizeof(VAN_DATUM));
	memset(&value, 0, sizeof(VAN_DATUM));
	key.data = kbuf;
	value.data = vbuf;

	fprintf(stderr, "============ 1 Try to put %lu records =========\n",
	    (unsigned long)cnt);
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (i = 0; i < cnt; i++) {
		memcpy(kbuf, keys[i], ksizes[i]);
		memcpy(vbuf, values[i], vsizes[i]);
		key.size = ksizes[i];
		value.size = vsizes[i];
		ret = tree->put(tree, &key, &value, 0);
		CHK_RET(ret, "tree->put");
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");


	fprintf(stderr, "========== 2 Get using tree->get() ============\n");
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (i = 0; i < cnt; i++) {
		memcpy(kbuf, keys[i], ksizes[i]);
		key.size = ksizes[i];
		ret = tree->get(tree, &key, &value, 0);
		CHK_RET(ret, "tree->get");
		memcpy(vbuf, value.data, value.size);
		TEST_ASSERT(value.size == vsizes[i]);
		cmp = memcmp(vbuf, values[i], vsizes[i]);
		TEST_ASSERT(cmp == 0);
		
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)cnt);

	fprintf(stderr, "========== 3 Get using cursor->get(VAN_SET) ============\n");
	time(&t1);
	ret = _van_inmemtree_cursor_init(tree, &cursor, 0);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	rcnt = 0;
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (i = 0; i < cnt; i++) {
		memcpy(kbuf, keys[i], ksizes[i]);
		key.size = ksizes[i];
		ret = cursor->get(cursor, &key, &value, VAN_SET);
		if (ret != 0)
			fprintf(stderr, "Not found record %lu\n",
			    (unsigned long)i);
		CHK_RET(ret, "cursor->get(VAN_SET)");
		memcpy(vbuf, value.data, value.size);
		TEST_ASSERT(value.size == vsizes[i]);
		cmp = memcmp(vbuf, values[i], vsizes[i]);
		TEST_ASSERT(cmp == 0);
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)cnt);
	ret = _van_inmemtree_cursor_cleanup(cursor);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");

	fprintf(stderr, "========== 4 Get using cursor->get(VAN_SET, VAN_NOTFOUND) ============\n");
	time(&t1);
	ret = _van_inmemtree_cursor_init(tree, &cursor, 0);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	rcnt = 0;
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (i = 0; i < cnt; i++) {
		sprintf(kbuf, "KEY_%d", i);
		key.size = strlen(kbuf) + 1;
		ret = cursor->get(cursor, &key, &value, VAN_SET);
		TEST_ASSERT(ret == VAN_NOTFOUND);
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)cnt);
	ret = _van_inmemtree_cursor_cleanup(cursor);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");

	fprintf(stderr, "========== 5 Get using VAN_NEXT ============\n");
	ret = _van_inmemtree_cursor_init(tree, &cursor, 0);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	rcnt = 0;
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (ret = 0; ret == 0;) {
		ret = cursor->get(cursor, &key, &value, VAN_NEXT);
		if (ret == 0) {
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_NEXT)");
		}
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)rcnt);
	ret = _van_inmemtree_cursor_cleanup(cursor);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");

	fprintf(stderr, "========== 6 Get using VAN_PREV ============\n");
	ret = _van_inmemtree_cursor_init(tree, &cursor, 0);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	rcnt = 0;
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (ret = 0; ret == 0;) {
		ret = cursor->get(cursor, &key, &value, VAN_PREV);
		if (ret == 0) {
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_PREV)");
		}
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)rcnt);
	ret = _van_inmemtree_cursor_cleanup(cursor);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");\


	fprintf(stderr, "========== 7 Get using VAN_NEXT(VAN_WRITECURSOR) ============\n");
	ret = _van_inmemtree_cursor_init(tree, &cursor, VAN_WRITECURSOR);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	rcnt = 0;
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (ret = 0; ret == 0;) {
		ret = cursor->get(cursor, &key, &value, VAN_NEXT);
		if (ret == 0) {
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_NEXT)");
		}
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)rcnt);
	ret = _van_inmemtree_cursor_cleanup(cursor);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");

	fprintf(stderr, "========== 8 Get using VAN_PREV(VAN_WRITECURSOR) ============\n");
	ret = _van_inmemtree_cursor_init(tree, &cursor, VAN_WRITECURSOR);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	rcnt = 0;
	time(&t1);
	_van_alloc_stat_reset();
	_van_inmemtree_rwlock_stat_reset();
	for (ret = 0; ret == 0;) {
		ret = cursor->get(cursor, &key, &value, VAN_PREV);
		if (ret == 0) {
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_PREV)");
		}
	}
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n",
	    (unsigned long)rcnt);
	ret = _van_inmemtree_cursor_cleanup(cursor);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");

	fprintf(stderr, "========== 9 Destroy the tree ============\n");
	time(&t1);
	_van_alloc_stat_reset();
	ret = _van_inmemtree_destory(tree);
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	CHK_RET(ret, "_van_inmemtree_destory");
	
	return (ret);
}
