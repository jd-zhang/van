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
	TESTDATA td;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "van_inmemtree_create");
	tree = table.active;

	cnt = 10000;
	TESTDATA_INIT(&td, (size_t)cnt);

	(void)generate_string_testdata(&td, 5, 15, 10, 20);

	ret = populate_inmemtree(tree, &td);
	CHK_RET(ret, "populate_inmemtree");

	(void)free_string_testdata(&td);

	ret = tree->destroy(tree);
	CHK_RET(ret, "tree->destroy");

	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");

	return (ret);
}

int imt_t1_2(int verbose) {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret, cnt;
	size_t rcnt;
	FILE *outf;
	time_t t1, t2;
	TESTDATA td;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "van_inmemtree_create");
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
	TESTDATA_INIT(&td, (size_t)cnt);
	(void)generate_string_testdata2(&td, "KEY", "DATA", 1);

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
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = populate_inmemtree(tree, &td);
	CHK_RET(ret, "populate_inmemtree");
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	tree->statprint(tree, stderr, "inemtree stat: ");

	fprintf(stderr, "========== 2 Dump leaves after putting ========\n");
	if (verbose) {
		(void)tree->dump_all_leaves(tree, outf);
		van_alloc_stat_print(stderr, " ");
	}

	fprintf(stderr, "========== 3 Get using tree->get() ============\n");
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_data(tree, &td);
	CHK_RET(ret, "verify_inmemtree_data");
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");

	fprintf(stderr, "========== 4 Get using cursor->get(VAN_SET) ============\n");
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_data_withcursor(tree, &td);
	CHK_RET(ret, "verify_inmemtree_data_withcursor");
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");


	fprintf(stderr, "========== 5 Get using VAN_NEXT ============\n");
	rcnt = 0;
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_NEXT, 0, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_NEXT)");
	TEST_ASSERT(cnt == rcnt);
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);

	fprintf(stderr, "========== 6 Get using VAN_PREV ============\n");
	rcnt = 0;
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_PREV, 0, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_PREV)");
	TEST_ASSERT(cnt == rcnt);
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);


	fprintf(stderr, "========== 7 Get using VAN_NEXT(VAN_WRITECURSOR) ============\n");
	rcnt = 0;
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_NEXT, VAN_WRITECURSOR, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_NEXT, VAN_WRITECURSOR)");
	TEST_ASSERT(cnt == rcnt);
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);

	fprintf(stderr, "========== 8 Get using VAN_PREV(VAN_WRITECURSOR) ============\n");
	rcnt = 0;
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_PREV, VAN_WRITECURSOR, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_PREV, VAN_WRITECURSOR)");
	TEST_ASSERT(cnt == rcnt);
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);


	fprintf(stderr, "========== 9 Get using VAN_LAST ============\n");
	ret = verify_inmemtree_cursor_border(tree, VAN_LAST, 0);
	CHK_RET(ret, "verify_inmemtree_cursor_border(VAN_LAST)");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");


	fprintf(stderr, "========== 10 Get using VAN_FIRST(VAN_WRITECURSOR) ============\n");
	ret = verify_inmemtree_cursor_border(tree, VAN_FIRST, VAN_WRITECURSOR);
	CHK_RET(ret, "verify_inmemtree_cursor_border(VAN_FIRST, VAN_WRITECURSOR)");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");

	fprintf(stderr, "========== 11 Destroy the tree ============\n");
	time(&t1);
	van_alloc_stat_reset();
	ret = tree->destroy(tree);
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	CHK_RET(ret, "tree->destroy");
	if (verbose)
		fclose(outf);
	free_string_testdata(&td);

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
	TESTDATA td;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "van_inmemtree_create");
	tree = table.active;
	/* we set this to avoid inmemtree to dump leaves,
	 since we don't want to test that here */
	tree->threshold = 1*GIGABYTE;

	cnt = 1000000;
	TESTDATA_INIT(&td, (size_t)cnt);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);


	fprintf(stderr, "============ 1 Try to put %lu records =========\n",
	    (unsigned long)cnt);
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = populate_inmemtree(tree, &td);
	CHK_RET(ret, "populate_inmemtree");
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	tree->statprint(tree, stderr, "inemtree stat: ");

	fprintf(stderr, "========== 2 Dump leaves after putting ========\n");

	fprintf(stderr, "========== 3 Get using tree->get() ============\n");
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_data(tree, &td);
	CHK_RET(ret, "verify_inmemtree_data");
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");

	fprintf(stderr, "========== 4 Get using cursor->get(VAN_SET) ============\n");
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_data_withcursor(tree, &td);
	CHK_RET(ret, "verify_inmemtree_data_withcursor");
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");


	fprintf(stderr, "========== 5 Get using VAN_NEXT ============\n");
	rcnt = 0;
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_NEXT, 0, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_NEXT)");
	TEST_ASSERT(cnt == rcnt);
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);

	fprintf(stderr, "========== 6 Get using VAN_PREV ============\n");
	rcnt = 0;
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_PREV, 0, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_PREV)");
	TEST_ASSERT(cnt == rcnt);
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);


	fprintf(stderr, "========== 7 Get using VAN_NEXT(VAN_WRITECURSOR) ============\n");
	rcnt = 0;
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_NEXT, VAN_WRITECURSOR, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_NEXT, VAN_WRITECURSOR)");
	TEST_ASSERT(cnt == rcnt);
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);

	fprintf(stderr, "========== 8 Get using VAN_PREV(VAN_WRITECURSOR) ============\n");
	rcnt = 0;
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_cursor_direction(tree, VAN_PREV, VAN_WRITECURSOR, &rcnt);
	CHK_RET(ret, "verify_inmemtree_cursor_direction(VAN_PREV, VAN_WRITECURSOR)");
	TEST_ASSERT(cnt == rcnt);
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	fprintf(stderr, " -- Get %lu records --\n", (unsigned long)rcnt);


	fprintf(stderr, "========== 9 Get using VAN_LAST ============\n");
	ret = verify_inmemtree_cursor_border(tree, VAN_LAST, 0);
	CHK_RET(ret, "verify_inmemtree_cursor_border(VAN_LAST)");


	fprintf(stderr, "========== 10 Get using VAN_FIRST(VAN_WRITECURSOR) ============\n");
	ret = verify_inmemtree_cursor_border(tree, VAN_FIRST, VAN_WRITECURSOR);
	CHK_RET(ret, "verify_inmemtree_cursor_border(VAN_FIRST, VAN_WRITECURSOR)");

	fprintf(stderr, "========== 11 Destroy the tree ============\n");
	time(&t1);
	van_alloc_stat_reset();
	ret = tree->destroy(tree);
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");
	CHK_RET(ret, "tree->destroy");
	free_string_testdata(&td);

	return (ret);
}

/*
 * Like imt_t1_3, but the walks are checked against the expected records
 * instead of only counting them.
 *
 * The data is sorted with the compare function of the tree first, so a
 * forward walk has to hand back exactly records[0] .. records[count - 1]
 * and a backward walk the same records in reverse.  VAN_FIRST has to be
 * records[0] and VAN_LAST records[count - 1].
 *
 * The plain counting version is imt_t1_3().
 */
int imt_t1_3b() {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret, cnt;
	size_t rcnt;
	TESTDATA td;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "van_inmemtree_create");
	tree = table.active;
	/* Keep the tree from dumping leaves, we test the cursor only. */
	tree->threshold = 1*GIGABYTE;

	cnt = 100000;
	TESTDATA_INIT(&td, (size_t)cnt);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);

	fprintf(stderr, "============ 1 Try to put %lu records =========\n",
	    (unsigned long)cnt);
	ret = populate_inmemtree(tree, &td);
	CHK_RET(ret, "populate_inmemtree");

	fprintf(stderr, "========== 2 Sort the expected records ==========\n");
	ret = sort_testdata(&td, tree->config->cmp_func);
	CHK_RET(ret, "sort_testdata");

	fprintf(stderr, "========== 3 Walk forward using VAN_NEXT ==========\n");
	rcnt = 0;
	ret = verify_inmemtree_cursor_direction_full(tree, VAN_NEXT, 0, &rcnt,
	    &td);
	CHK_RET(ret, "verify_inmemtree_cursor_direction_full(VAN_NEXT)");
	TEST_ASSERT2(rcnt == (size_t)cnt, "expected %d records, walked %lu",
	    cnt, (unsigned long)rcnt);

	fprintf(stderr, "========== 4 Walk backward using VAN_PREV ==========\n");
	rcnt = 0;
	ret = verify_inmemtree_cursor_direction_full(tree, VAN_PREV, 0, &rcnt,
	    &td);
	CHK_RET(ret, "verify_inmemtree_cursor_direction_full(VAN_PREV)");
	TEST_ASSERT2(rcnt == (size_t)cnt, "expected %d records, walked %lu",
	    cnt, (unsigned long)rcnt);

	/*
	 * The same two walks with a write cursor, which takes a different
	 * path through the tree.
	 */
	fprintf(stderr,
	    "========== 5 Walk forward using VAN_NEXT(VAN_WRITECURSOR) ==\n");
	rcnt = 0;
	ret = verify_inmemtree_cursor_direction_full(tree, VAN_NEXT,
	    VAN_WRITECURSOR, &rcnt, &td);
	CHK_RET(ret, "verify_inmemtree_cursor_direction_full"
	    "(VAN_NEXT, VAN_WRITECURSOR)");
	TEST_ASSERT2(rcnt == (size_t)cnt, "expected %d records, walked %lu",
	    cnt, (unsigned long)rcnt);

	fprintf(stderr,
	    "========== 6 Walk backward using VAN_PREV(VAN_WRITECURSOR) ==\n");
	rcnt = 0;
	ret = verify_inmemtree_cursor_direction_full(tree, VAN_PREV,
	    VAN_WRITECURSOR, &rcnt, &td);
	CHK_RET(ret, "verify_inmemtree_cursor_direction_full"
	    "(VAN_PREV, VAN_WRITECURSOR)");
	TEST_ASSERT2(rcnt == (size_t)cnt, "expected %d records, walked %lu",
	    cnt, (unsigned long)rcnt);

	fprintf(stderr, "========== 7 VAN_FIRST is the first record ==========\n");
	ret = verify_inmemtree_cursor_border_full(tree, VAN_FIRST, 0,
	    &td.records[0]);
	CHK_RET(ret, "verify_inmemtree_cursor_border_full(VAN_FIRST)");

	fprintf(stderr, "========== 8 VAN_LAST is the last record ==========\n");
	ret = verify_inmemtree_cursor_border_full(tree, VAN_LAST, 0,
	    &td.records[td.count - 1]);
	CHK_RET(ret, "verify_inmemtree_cursor_border_full(VAN_LAST)");

	fprintf(stderr, "========== 9 VAN_FIRST(VAN_WRITECURSOR) ============\n");
	ret = verify_inmemtree_cursor_border_full(tree, VAN_FIRST,
	    VAN_WRITECURSOR, &td.records[0]);
	CHK_RET(ret, "verify_inmemtree_cursor_border_full"
	    "(VAN_FIRST, VAN_WRITECURSOR)");

	fprintf(stderr, "========== 10 VAN_LAST(VAN_WRITECURSOR) =============\n");
	ret = verify_inmemtree_cursor_border_full(tree, VAN_LAST,
	    VAN_WRITECURSOR, &td.records[td.count - 1]);
	CHK_RET(ret, "verify_inmemtree_cursor_border_full"
	    "(VAN_LAST, VAN_WRITECURSOR)");

	fprintf(stderr, "========== 11 Destroy the tree ===========\n");
	ret = tree->destroy(tree);
	CHK_RET(ret, "tree->destroy");

	(void)free_string_testdata(&td);

	return (ret);
}

/*
 * What a wrong expectation looks like.  imt_t1_3c() feeds these to the
 * checks above and expects every one of them to be caught.
 */
#define IMT_NEG_KEY     1   /* the first record has another key */
#define IMT_NEG_VALUE   2   /* the first record has another value */
#define IMT_NEG_COUNT   3   /* one record less than the tree holds */
#define IMT_NEG_BORDERK 4   /* VAN_FIRST / VAN_LAST: wrong key */
#define IMT_NEG_BORDERV 5   /* VAN_FIRST / VAN_LAST: wrong value */

static char imt_neg_key[] = "zzzz-bogus-key-that-is-not-in-the-tree";
static char imt_neg_val[] = "zzzz-bogus-value-that-is-not-in-the-tree";

typedef struct {
	INMEM_TREE *tree;
	flags_t     direction;
	flags_t     cflags;
	TESTDATA    td;
	int         mode;
} imt_neg_t;

/*
 * Runs in a child process, so it may corrupt the expected records: fork()
 * gives it a copy on write of everything the test owns.
 */
static int imt_neg_check(void *arg) {
	imt_neg_t *c;
	TESTDATA short_td;
	RECORD rec;
	size_t rcnt, idx;
	int backward;

	c = (imt_neg_t *)arg;
	rcnt = 0;
	backward = (c->direction == VAN_PREV || c->direction == VAN_LAST);
	/* Corrupt the record the walk reads first. */
	idx = backward ? c->td.count - 1 : 0;
	rec = c->td.records[idx];

	switch (c->mode) {
	case IMT_NEG_KEY:
		c->td.records[idx].key = imt_neg_key;
		c->td.records[idx].ksize = strlen(imt_neg_key);
		break;
	case IMT_NEG_VALUE:
		c->td.records[idx].value = imt_neg_val;
		c->td.records[idx].vsize = strlen(imt_neg_val);
		break;
	case IMT_NEG_COUNT:
		/* One record less than the tree holds. */
		TESTDATA_SLICE(&short_td, &c->td, 0, c->td.count - 1);
		return verify_inmemtree_cursor_direction_full(c->tree,
		    c->direction, c->cflags, &rcnt, &short_td);
	case IMT_NEG_BORDERK:
		rec.key = imt_neg_key;
		rec.ksize = strlen(imt_neg_key);
		return verify_inmemtree_cursor_border_full(c->tree,
		    c->direction, c->cflags, &rec);
	case IMT_NEG_BORDERV:
		rec.value = imt_neg_val;
		rec.vsize = strlen(imt_neg_val);
		return verify_inmemtree_cursor_border_full(c->tree,
		    c->direction, c->cflags, &rec);
	default:
		return (0);
	}

	return verify_inmemtree_cursor_direction_full(c->tree, c->direction,
	    c->cflags, &rcnt, &c->td);
}

static int imt_neg_one(INMEM_TREE *tree, TESTDATA *td, flags_t direction,
    flags_t cflags, int mode, const char *what) {
	imt_neg_t ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.tree = tree;
	ctx.direction = direction;
	ctx.cflags = cflags;
	ctx.td = *td;
	ctx.mode = mode;

	TEST_ASSERT2(verify_child_fails(imt_neg_check, &ctx),
	    "%s: the check passed, it must catch this", what);
	fprintf(stderr, "   caught: %s\n", what);

	return (0);
}

/*
 * Negative test for the checks imt_t1_3b() uses.
 *
 * A verification that only counts records cannot tell a wrong record from a
 * right one, so every case here feeds the check a deliberately wrong
 * expectation and requires it to fail.  Each check runs in a child process,
 * see verify_child_fails(): a mismatch ends the process, and the test has to
 * survive it to try the next one.
 *
 * Few records on purpose, this is about detection, not volume.
 */
int imt_t1_3c() {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret, cnt;
	TESTDATA td;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "van_inmemtree_create");
	tree = table.active;
	tree->threshold = 1*GIGABYTE;

	cnt = 10000;
	TESTDATA_INIT(&td, (size_t)cnt);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);

	fprintf(stderr, "============ 1 Try to put %lu records =========\n",
	    (unsigned long)cnt);
	ret = populate_inmemtree(tree, &td);
	CHK_RET(ret, "populate_inmemtree");

	fprintf(stderr, "========== 2 Sort the expected records ==========\n");
	ret = sort_testdata(&td, tree->config->cmp_func);
	CHK_RET(ret, "sort_testdata");

	fprintf(stderr, "========== 3 A wrong record must be caught ======\n");
	ret = imt_neg_one(tree, &td, VAN_NEXT, 0, IMT_NEG_KEY,
	    "VAN_NEXT, wrong key");
	ret = imt_neg_one(tree, &td, VAN_PREV, 0, IMT_NEG_KEY,
	    "VAN_PREV, wrong key");
	ret = imt_neg_one(tree, &td, VAN_NEXT, 0, IMT_NEG_VALUE,
	    "VAN_NEXT, wrong value");
	ret = imt_neg_one(tree, &td, VAN_PREV, 0, IMT_NEG_VALUE,
	    "VAN_PREV, wrong value");
	ret = imt_neg_one(tree, &td, VAN_NEXT, VAN_WRITECURSOR, IMT_NEG_KEY,
	    "VAN_NEXT(VAN_WRITECURSOR), wrong key");
	ret = imt_neg_one(tree, &td, VAN_PREV, VAN_WRITECURSOR, IMT_NEG_VALUE,
	    "VAN_PREV(VAN_WRITECURSOR), wrong value");

	fprintf(stderr, "========== 4 A wrong count must be caught =======\n");
	ret = imt_neg_one(tree, &td, VAN_NEXT, 0, IMT_NEG_COUNT,
	    "VAN_NEXT, one record too few");
	ret = imt_neg_one(tree, &td, VAN_PREV, 0, IMT_NEG_COUNT,
	    "VAN_PREV, one record too few");

	fprintf(stderr, "========== 5 A wrong border must be caught ======\n");
	ret = imt_neg_one(tree, &td, VAN_FIRST, 0, IMT_NEG_BORDERK,
	    "VAN_FIRST, wrong key");
	ret = imt_neg_one(tree, &td, VAN_LAST, 0, IMT_NEG_BORDERK,
	    "VAN_LAST, wrong key");
	ret = imt_neg_one(tree, &td, VAN_FIRST, 0, IMT_NEG_BORDERV,
	    "VAN_FIRST, wrong value");
	ret = imt_neg_one(tree, &td, VAN_LAST, 0, IMT_NEG_BORDERV,
	    "VAN_LAST, wrong value");

	fprintf(stderr, "========== 6 Destroy the tree ===========\n");
	ret = tree->destroy(tree);
	CHK_RET(ret, "tree->destroy");

	(void)free_string_testdata(&td);

	return (ret);
}

/*
 * Verify put on a frozen tree, returns VAN_FROZEN
 */
int imt_t1_4() {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret, cnt;
	TESTDATA td;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "van_inmemtree_create");
	tree = table.active;

	cnt = 10000;
	TESTDATA_INIT(&td, (size_t)cnt);

	fprintf(stderr, "========== 1 generate test data ============\n");
	(void)generate_string_testdata(&td, 5, 15, 10, 20);

	fprintf(stderr, "========== 2 populate tree ============\n");
	ret = populate_inmemtree(tree, &td);
	CHK_RET(ret, "populate_inmemtree");

	fprintf(stderr, "========== 3 delete from tree ============\n");
	ret = delete_inmemtree_with_expected(tree, &td, 0, 3, 0);

	fprintf(stderr, "========== 4 delete records deleted from tree ============\n");
	ret = delete_inmemtree_with_expected(tree, &td, 0, 3, VAN_NOTFOUND);

	fprintf(stderr, "========== 5 verify the tree does not contain deleted data ============\n");
	ret = verify_inmemtree_with_expected(tree, &td, 0, 3, VAN_NOTFOUND);
	
	fprintf(stderr, "========== 6 verify the tree contains non-deleted data ============\n");
	ret = verify_inmemtree_with_expected(tree, &td, 1, 3, 0);
	ret = verify_inmemtree_with_expected(tree, &td, 2, 3, 0);

	fprintf(stderr, "========== 7 freeze the tree ============\n");
	ret = tree->freeze(tree);
	CHK_RET(ret, "tree->freeze");

	fprintf(stderr, "========== 8 populate frozen tree with expected ============\n");
	ret = populate_inmemtree_with_expected(tree, &td, VAN_FROZEN);

	fprintf(stderr, "========== 9 delete from frozen tree with expected ============\n");
	ret = delete_inmemtree_with_expected(tree, &td, 0, 1, VAN_FROZEN);

	fprintf(stderr, "========== 10 cleanup test data ============\n");
	(void)free_string_testdata(&td);

	fprintf(stderr, "========== 11 destroy the tree ============\n");
	ret = tree->destroy(tree);
	CHK_RET(ret, "tree->destroy");

	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");

	return (ret);
}
