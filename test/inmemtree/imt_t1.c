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

/*
 * Just putting data into the inmem-tree
 */
int imt_t1_1() {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret, cnt;
	STRING_TESTDATA_DEFINE;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = _van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "_van_inmemtree_create");
	tree = table.active;

	cnt = 10000;
	STRING_TESTDATA_INIT;

	(void)generate_string_testdata(keys, ksizes, 5, 15, values, vsizes,
	    10, 20, cnt);

	ret = populate_inmemtree(tree, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "populate_inmemtree");

	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);

	ret = _van_inmemtree_destory(tree);
	CHK_RET(ret, "_van_inmemtree_destory");

	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");

	return (ret);
}

int imt_t1_2(int verbose) {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret, cnt;
	size_t rcnt;
	FILE *outf;
	time_t t1, t2;
	STRING_TESTDATA_DEFINE;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = _van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "_van_inmemtree_create");
	tree = table.active;
	tree->threshold = 1*GIGABYTE;

	if (verbose)
		outf = fopen("imt_t1_2.out", "w");

	/*
	 * When there are so many records,
	 * the speed slows quickly, since
	 * we haven't made good use of
	 * the memory.
	 */
	cnt = 100000;
	STRING_TESTDATA_INIT;
	(void)generate_string_testdata2(keys, ksizes, "KEY",
	    values, vsizes, "DATA", cnt, 1);

	/*
	 * 1 Put records
	 * 2 Dump leaves
	 * 3 Get using tree->get
	 * 4 Get using cursor->get(VAN_SET)
	 * 5 Get using VAN_NEXT
	 * 6 Get using VAN_PREV
	 * 7 Get using VAN_NEXT(VAN_WRITECURSOR)
	 * 8 Get using VAN_PREV(VAN_WRITECURSOR)
	 * 9 Get using VAN_LAST
	 * 10 Get using VAN_FIRST(VAN_WRITECURSOR)
	 */
	fprintf(stderr, "============ 1 Try to put %lu records =========\n",
	    (unsigned long)cnt);
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = populate_inmemtree(tree, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "populate_inmemtree");
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	tree->statprint(tree, stderr, "inemtree stat: ");

	fprintf(stderr, "========== 2 Dump leaves after putting ========\n");
	if (verbose) {
		(void)_van_inmemtree_dump_all_leaves(tree, outf);
		_van_alloc_stat_print(stderr, " ");
	}

	fprintf(stderr, "========== 3 Get using tree->get() ============\n");
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_data(tree, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "verify_inmemtree_data");
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");

	fprintf(stderr, "========== 4 Get using cursor->get(VAN_SET) ============\n");
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_data_withcursor(tree, keys, ksizes,
	    values, vsizes, cnt);
	CHK_RET(ret, "verify_inmemtree_data_withcursor");
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");


	fprintf(stderr, "========== 5 Get using VAN_NEXT ============\n");
	rcnt = 0;
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_NEXT, 0, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_NEXT)");
	TEST_ASSERT(cnt == rcnt);
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);

	fprintf(stderr, "========== 6 Get using VAN_PREV ============\n");
	rcnt = 0;
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_PREV, 0, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_PREV)");
	TEST_ASSERT(cnt == rcnt);
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);


	fprintf(stderr, "========== 7 Get using VAN_NEXT(VAN_WRITECURSOR) ============\n");
	rcnt = 0;
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_NEXT, VAN_WRITECURSOR, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_NEXT, VAN_WRITECURSOR)");
	TEST_ASSERT(cnt == rcnt);
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);

	fprintf(stderr, "========== 8 Get using VAN_PREV(VAN_WRITECURSOR) ============\n");
	rcnt = 0;
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_PREV, VAN_WRITECURSOR, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_PREV, VAN_WRITECURSOR)");
	TEST_ASSERT(cnt == rcnt);
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);


	fprintf(stderr, "========== 9 Get using VAN_LAST ============\n");
	ret = verify_inmemtree_cursor_border(tree, VAN_LAST, 0);
	CHK_RET(ret, "verify_inmemtree_cursor_border(VAN_LAST)");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");


	fprintf(stderr, "========== 10 Get using VAN_FIRST(VAN_WRITECURSOR) ============\n");
	ret = verify_inmemtree_cursor_border(tree, VAN_FIRST, VAN_WRITECURSOR);
	CHK_RET(ret, "verify_inmemtree_cursor_border(VAN_FIRST, VAN_WRITECURSOR)");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");

	fprintf(stderr, "========== 11 Destroy the tree ============\n");
	time(&t1);
	_van_alloc_stat_reset();
	ret = _van_inmemtree_destory(tree);
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	CHK_RET(ret, "_van_inmemtree_destory");
	if (verbose)
		fclose(outf);
	free_string_testdata(keys, ksizes, values, vsizes, cnt);

	return (ret);
}

/*
 * Like above, but the data is generated by generate_string_testdata
 * instead of generate_string_testdata2
 */
int imt_t1_3() {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret, cnt;
	size_t rcnt;
	time_t t1, t2;
	STRING_TESTDATA_DEFINE;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = _van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "_van_inmemtree_create");
	tree = table.active;
	/* we set this to avoid inmemtree to dump leaves,
	 since we don't want to test that here */
	tree->threshold = 1*GIGABYTE;

	cnt = 1000000;
	STRING_TESTDATA_INIT;
	(void)generate_string_testdata(keys, ksizes, 20, 30,
	    values, vsizes, 50, 120, cnt);


	fprintf(stderr, "============ 1 Try to put %lu records =========\n",
	    (unsigned long)cnt);
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = populate_inmemtree(tree, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "populate_inmemtree");
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	tree->statprint(tree, stderr, "inemtree stat: ");

	fprintf(stderr, "========== 2 Dump leaves after putting ========\n");

	fprintf(stderr, "========== 3 Get using tree->get() ============\n");
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_data(tree, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "verify_inmemtree_data");
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");

	fprintf(stderr, "========== 4 Get using cursor->get(VAN_SET) ============\n");
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_data_withcursor(tree, keys, ksizes,
	    values, vsizes, cnt);
	CHK_RET(ret, "verify_inmemtree_data_withcursor");
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");


	fprintf(stderr, "========== 5 Get using VAN_NEXT ============\n");
	rcnt = 0;
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_NEXT, 0, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_NEXT)");
	TEST_ASSERT(cnt == rcnt);
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);

	fprintf(stderr, "========== 6 Get using VAN_PREV ============\n");
	rcnt = 0;
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_PREV, 0, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_PREV)");
	TEST_ASSERT(cnt == rcnt);
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);


	fprintf(stderr, "========== 7 Get using VAN_NEXT(VAN_WRITECURSOR) ============\n");
	rcnt = 0;
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_NEXT, VAN_WRITECURSOR, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_NEXT, VAN_WRITECURSOR)");
	TEST_ASSERT(cnt == rcnt);
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);

	fprintf(stderr, "========== 8 Get using VAN_PREV(VAN_WRITECURSOR) ============\n");
	rcnt = 0;
	_van_alloc_stat_reset();
	_van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_PREV, VAN_WRITECURSOR, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_PREV, VAN_WRITECURSOR)");
	TEST_ASSERT(cnt == rcnt);
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);


	fprintf(stderr, "========== 9 Get using VAN_LAST ============\n");
	ret = verify_inmemtree_cursor_border(tree, VAN_LAST, 0);
	CHK_RET(ret, "verify_inmemtree_cursor_border(VAN_LAST)");


	fprintf(stderr, "========== 10 Get using VAN_FIRST(VAN_WRITECURSOR) ============\n");
	ret = verify_inmemtree_cursor_border(tree, VAN_FIRST, VAN_WRITECURSOR);
	CHK_RET(ret, "verify_inmemtree_cursor_border(VAN_FIRST, VAN_WRITECURSOR)");

	fprintf(stderr, "========== 11 Destroy the tree ============\n");
	time(&t1);
	_van_alloc_stat_reset();
	ret = _van_inmemtree_destory(tree);
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	_van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");
	CHK_RET(ret, "_van_inmemtree_destory");
	free_string_testdata(keys, ksizes, values, vsizes, cnt);

	return (ret);
}

/*
 * Verify put on a frozen tree, returns VAN_FROZEN
 */
int imt_t1_4() {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret, cnt;
	STRING_TESTDATA_DEFINE;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = _van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "_van_inmemtree_create");
	tree = table.active;

	cnt = 10000;
	STRING_TESTDATA_INIT;

	fprintf(stderr, "========== 1 generate test data ============\n");
	(void)generate_string_testdata(keys, ksizes, 5, 15, values, vsizes,
	    10, 20, cnt);

	fprintf(stderr, "========== 2 populate tree ============\n");
	ret = populate_inmemtree(tree, keys, ksizes, values, vsizes, cnt);
	CHK_RET(ret, "populate_inmemtree");

	fprintf(stderr, "========== 3 delete from tree ============\n");
	ret = delete_inmemtree_with_expected(tree, keys, ksizes, cnt, 0, 3, 0);

	fprintf(stderr, "========== 4 delete records deleted from tree ============\n");
	ret = delete_inmemtree_with_expected(tree, keys, ksizes, cnt, 0, 3, VAN_NOTFOUND);

	fprintf(stderr, "========== 5 verify the tree does not contain deleted data ============\n");
	ret = verify_inmemtree_with_expected(tree, keys, ksizes, values, vsizes,
	    cnt, 0, 3, VAN_NOTFOUND);
	
	fprintf(stderr, "========== 6 verify the tree contains non-deleted data ============\n");
	ret = verify_inmemtree_with_expected(tree, keys, ksizes, values, vsizes,
	    cnt, 1, 3, 0);
	ret = verify_inmemtree_with_expected(tree, keys, ksizes, values, vsizes,
	    cnt, 2, 3, 0);

	fprintf(stderr, "========== 7 freeze the tree ============\n");
	ret = _van_inmemtree_freeze(tree);
	CHK_RET(ret, "_van_inmemtree_freeze");

	fprintf(stderr, "========== 8 populate frozen tree with expected ============\n");
	ret = populate_inmemtree_with_expected(tree, 
		keys, ksizes, values, vsizes,  cnt, VAN_FROZEN);

	fprintf(stderr, "========== 9 delete from frozen tree with expected ============\n");
	ret = delete_inmemtree_with_expected(tree, keys, ksizes, cnt, 0, 1, VAN_FROZEN);

	fprintf(stderr, "========== 10 cleanup test data ============\n");
	(void)free_string_testdata(keys, ksizes, values, vsizes, cnt);

	fprintf(stderr, "========== 11 destroy the tree ============\n");
	ret = _van_inmemtree_destory(tree);
	CHK_RET(ret, "_van_inmemtree_destory");

	fprintf(stderr, "**** Malloc Statistics ****\n");
	_van_alloc_stat_print(stderr, " ");

	return (ret);
}
