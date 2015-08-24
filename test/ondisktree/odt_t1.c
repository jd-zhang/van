#include "test_common.h"

int odt_t1_1() {
	VAN_TABLE table;
	INMEM_TREE *tree;
	INMEM_TREECURSOR *imc;
	ONDISK_TREECURSOR *odc;
	ONDISK_TREE *odtree;
	int ret, cnt, i;
	char kbuf[MAX_BUF_LEN], vbuf[MAX_BUF_LEN];
	VAN_DATUM key, value, key1, value1;
	time_t t1, t2;
	char **keys, **values;
	size_t ksz, vsz, rcnt;
	size_t *ksizes, *vsizes;

	memset(&table, 0, sizeof(VAN_TABLE));
	table.cache_header_size = 64;
	ret = _van_ondisk_cache_init(&table);
	ret = _van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "_van_inmemtree_create");
	tree = table.active;

	cnt = 100000;
	keys = (char **)malloc(cnt * sizeof(char *));
	TEST_ASSERT(keys != NULL);
	ksizes = (size_t *)malloc(cnt * sizeof(size_t));
	TEST_ASSERT(ksizes != NULL);
	values = (char **)malloc(cnt * sizeof(char *));
	TEST_ASSERT(values != NULL);
	vsizes = (size_t *)malloc(cnt * sizeof(size_t));
	TEST_ASSERT(vsizes != NULL);

	memset(kbuf, 0, sizeof(kbuf));
	memset(vbuf, 0, sizeof(vbuf));

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
	memset(&key1, 0, sizeof(VAN_DATUM));
	memset(&value1, 0, sizeof(VAN_DATUM));
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

	fprintf(stderr, "========== 2 Build the ondisk tree ============\n");
	time(&t1);
	ret = _van_ondisktree_create(&table, &odtree);
	odtree->blob_dir = "blobs";
	odtree->file_path = "test1";
	odtree->bloom_path = "test1_bloom";
	ret = _van_ondisktree_open(odtree, VAN_OPEN_CREAT | VAN_OPEN_TRUNC);
	ret = _van_ondisktree_store(odtree, tree);
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));


	fprintf(stderr, "======== 3 Dump the tree(Basic Info) =======\n");
	ret = _van_ondisktree_dump(odtree, stderr);

	fprintf(stderr, "======== 4 trasverse ondisk tree using cursor(FORWARD) =======\n");
	time(&t1);
	rcnt = 0;
	ret = _van_ondisktree_cursor_init(odtree, &odc, 0);
	CHK_RET(ret, "_van_ondisktree_cursor_init");
	ret = _van_inmemtree_cursor_init(tree, &imc, 0);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	for (ret = 0; ret == 0;) {
		ret = odc->get(odc, &key, &value, VAN_NEXT);
		if (ret == 0) {
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_NEXT)");
		}
		if (ret != 0)
			break;
		ret = imc->get(imc, &key1, &value1, VAN_NEXT);
		if (ret == 0) {
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_NEXT)");
		}
		if (ret != 0)
			break;
		/* Compare */
		VAN_ASSERT(key.size == key1.size);
		VAN_ASSERT((memcmp(key.data, key1.data, key.size) == 0));
		VAN_ASSERT(value.size == value1.size);
		VAN_ASSERT((memcmp(value.data, value1.data, value.size) == 0));
	}
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds, get %lu records\n",
	    difftime(t2, t1), (unsigned long)rcnt);
	ret = _van_ondisktree_cursor_cleanup(odc);
	CHK_RET(ret, "_van_ondisktree_cursor_cleanup");
	ret = _van_inmemtree_cursor_cleanup(imc);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");

	fprintf(stderr, "======== 5 trasverse ondisk tree using cursor(BACKWARD) =======\n");
	time(&t1);
	rcnt = 0;
	ret = _van_ondisktree_cursor_init(odtree, &odc, 0);
	CHK_RET(ret, "_van_ondisktree_cursor_init");
	ret = _van_inmemtree_cursor_init(tree, &imc, 0);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
	for (ret = 0; ret == 0;) {
		ret = odc->get(odc, &key, &value, VAN_PREV);
		if (ret == 0) {
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_NEXT)");
		}
		if (ret != 0)
			break;
		ret = imc->get(imc, &key1, &value1, VAN_PREV);
		if (ret == 0) {
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_NEXT)");
		}
		if (ret != 0)
			break;
		/* Compare */
		VAN_ASSERT(key.size == key1.size);
		VAN_ASSERT((memcmp(key.data, key1.data, key.size) == 0));
		VAN_ASSERT(value.size == value1.size);
		VAN_ASSERT((memcmp(value.data, value1.data, value.size) == 0));
	}
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds, get %lu records\n",
	    difftime(t2, t1), (unsigned long)rcnt);
	ret = _van_ondisktree_cursor_cleanup(odc);
	CHK_RET(ret, "_van_ondisktree_cursor_cleanup");
	ret = _van_inmemtree_cursor_cleanup(imc);
	CHK_RET(ret, "_van_inmemtree_cursor_cleanup");

	fprintf(stderr, "======== 6 trasverse ondisk tree using cursor(VAN_SET) =======\n");
	time(&t1);
	rcnt = 0;
	ret = _van_ondisktree_cursor_init(odtree, &odc, 0);
	CHK_RET(ret, "_van_ondisktree_cursor_init");
	memset(&key, 0, sizeof(VAN_DATUM));
	for (i = 0; i < cnt; i ++) {
		key.data = keys[i];
		key.size = ksizes[i];
		ret = odc->get(odc, &key, &value, VAN_SET);
		if (ret == 0) {
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, "cursor->get(VAN_NEXT)");
		}
		if (ret != 0)
			break;
		/* Compare */
		VAN_ASSERT(value.size == vsizes[i]);
		VAN_ASSERT((memcmp(value.data, values[i], value.size) == 0));
	}
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds, get %lu records\n",
	    difftime(t2, t1), (unsigned long)rcnt);
	ret = _van_ondisktree_cursor_cleanup(odc);
	CHK_RET(ret, "_van_ondisktree_cursor_cleanup");

	fprintf(stderr, "====== 7 Destroy the tree(ondisk and inmemory) ====\n");
	time(&t1);
	_van_alloc_stat_reset();
	ret =  _van_ondisktree_close(odtree);
	ret = _van_inmemtree_destory(tree);
	ret = _van_ondisk_cache_destroy(&table);

	for (i = 0; i < cnt; i ++) {
		free(keys[i]);
		free(values[i]);
	}
	free(keys);
	free(values);
	free(ksizes);
	free(vsizes);

	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	_van_inmemtree_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	CHK_RET(ret, "_van_inmemtree_destory");
	
	return (ret);
}
