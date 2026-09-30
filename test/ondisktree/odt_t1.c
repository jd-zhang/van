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

int odt_t1_1() {
	VAN_TABLE *table;
	VAN_STORE *store;
	INMEM_TREE *tree;
	ONDISK_TREE *odtree;
	int ret, cnt;
	TESTDATA td;
	const char *odtpath;
	size_t rcnt;
	time_t t1, t2;

	odtpath = "test1.ondisk.odt_t1";
	system("rm -rf test1.ondisk.odt_t1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");

	ret = van_inmemtree_create(table, &tree);
	CHK_RET(ret, "van_inmemtree_create");
	tree->threshold = GIGABYTE;

	cnt = 100000;
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

	fprintf(stderr, "========== 2 Build the ondisk tree ============\n");
	time(&t1);
	ret = van_ondisktree_create(table, &odtree);
	CHK_RET(ret, "van_ondisktree_create");
	ret = odtree->open(odtree, odtpath,
	    VAN_OPEN_CREAT | VAN_OPEN_TRUNC);
	CHK_RET(ret, "odtree->open(VAN_OPEN_CREAT | VAN_OPEN_TRUNC)");
	ret = odtree->store_inmem(odtree, tree);
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));


	fprintf(stderr, "======== 3 Dump the tree(Basic Info) =======\n");
	ret = odtree->dump(odtree, stderr);

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
	ret = odtree->close(odtree, 0);
	CHK_RET(ret, "odtree->close - 1");
	odtree = NULL;
	ret = van_ondisktree_create(table, &odtree);
	CHK_RET(ret, "van_ondisktree_create");
	ret = odtree->open(odtree, odtpath, 0);
	CHK_RET(ret, "odtree->open(0)");

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
	ret = odtree->close(odtree, 0);
	CHK_RET(ret, "odtree->close - 2");
	odtree = NULL;
	ret = van_ondisktree_create(table, &odtree);
	CHK_RET(ret, "van_ondisktree_create");
	ret = odtree->open(odtree, odtpath, 0);
	CHK_RET(ret, "odtree->open(0)");

	fprintf(stderr, "======== 6 trasverse ondisk tree using cursor(VAN_SET) =======\n");
	ret = verify_ondisktree_data(odtree, &td);
	CHK_RET(ret, "verify_ondisktree_data");

	/* We close and reopen the tree here */
	ret = odtree->close(odtree, 0);
	CHK_RET(ret, "odtree->close - 1");
	odtree = NULL;
	ret = van_ondisktree_create(table, &odtree);
	CHK_RET(ret, "van_ondisktree_create");
	ret = odtree->open(odtree, odtpath, 0);
	CHK_RET(ret, "odtree->open(0)");

	fprintf(stderr, "======== 7 trasverse ondisk tree using cursor(LAST) =======\n");
	ret = verify_ondisktree_cursor_border(odtree, VAN_LAST, 0);
	CHK_RET(ret, "verify_ondisktree_cursor_border(VAN_LAST)");


	fprintf(stderr, "======== 8 trasverse ondisk tree using cursor(FIRST) =======\n");
	ret = verify_ondisktree_cursor_border(odtree, VAN_FIRST, 0);
	CHK_RET(ret, "verify_ondisktree_cursor_border(VAN_FIRST)");

	fprintf(stderr, "====== 9 Destroy the tree(ondisk and inmemory) ====\n");
	time(&t1);
	van_alloc_stat_reset();
	ret =  odtree->close(odtree, 0);
	ret = tree->destroy(tree);
	CHK_RET(ret, "tree->destroy");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");

	(void)free_string_testdata(&td);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	van_rwlock_stat_print(stderr, " ");


	return (ret);
}


/*
 * Like odt_t1_1, but the walks are checked against the expected records
 * instead of only counting them.
 *
 * The records go into an in-memory tree first and are then stored into the
 * on-disk tree, exactly as odt_t1_1() does.  The expected records are
 * sorted with the compare function the on-disk tree compares keys with, so
 * that walking it has to hand back records[0] .. records[count - 1] forward
 * and the reverse order backward.
 */
int odt_t1_1b() {
	VAN_TABLE *table;
	VAN_STORE *store;
	INMEM_TREE *tree;
	ONDISK_TREE *odtree;
	int ret, cnt;
	size_t rcnt;
	TESTDATA td;
	const char *odtpath;

	odtpath = "test1.ondisk.odt_t1b";
	system("rm -rf test1.ondisk.odt_t1b*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");

	ret = van_inmemtree_create(table, &tree);
	CHK_RET(ret, "van_inmemtree_create");
	tree->threshold = GIGABYTE;

	cnt = 100000;
	TESTDATA_INIT(&td, (size_t)cnt);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);

	fprintf(stderr, "============ 1 Try to put %lu records =========\n",
	    (unsigned long)cnt);
	ret = populate_inmemtree(tree, &td);
	CHK_RET(ret, "populate_inmemtree");

	fprintf(stderr, "========== 2 Build the ondisk tree ============\n");
	ret = van_ondisktree_create(table, &odtree);
	CHK_RET(ret, "van_ondisktree_create");
	ret = odtree->open(odtree, odtpath,
	    VAN_OPEN_CREAT | VAN_OPEN_TRUNC);
	CHK_RET(ret, "odtree->open(VAN_OPEN_CREAT | VAN_OPEN_TRUNC)");
	ret = odtree->store_inmem(odtree, tree);
	CHK_RET(ret, "odtree->store_inmem");

	fprintf(stderr, "========== 3 Sort the expected records ==========\n");
	ret = sort_testdata(&td, odtree->table->cmp_func);
	CHK_RET(ret, "sort_testdata");

	fprintf(stderr, "======== 4 Walk forward using VAN_NEXT ========\n");
	rcnt = 0;
	ret = verify_ondisktree_cursor_direction_full(odtree, VAN_NEXT, 0,
	    &rcnt, &td);
	CHK_RET(ret, "verify_ondisktree_cursor_direction_full(VAN_NEXT)");
	TEST_ASSERT2(rcnt == (size_t)cnt, "expected %d records, walked %lu",
	    cnt, (unsigned long)rcnt);

	fprintf(stderr, "======== 5 Walk backward using VAN_PREV ========\n");
	rcnt = 0;
	ret = verify_ondisktree_cursor_direction_full(odtree, VAN_PREV, 0,
	    &rcnt, &td);
	CHK_RET(ret, "verify_ondisktree_cursor_direction_full(VAN_PREV)");
	TEST_ASSERT2(rcnt == (size_t)cnt, "expected %d records, walked %lu",
	    cnt, (unsigned long)rcnt);

	fprintf(stderr, "======== 6 VAN_FIRST is the first record ========\n");
	ret = verify_ondisktree_cursor_border_full(odtree, VAN_FIRST, 0,
	    &td.records[0]);
	CHK_RET(ret, "verify_ondisktree_cursor_border_full(VAN_FIRST)");

	fprintf(stderr, "======== 7 VAN_LAST is the last record ========\n");
	ret = verify_ondisktree_cursor_border_full(odtree, VAN_LAST, 0,
	    &td.records[td.count - 1]);
	CHK_RET(ret, "verify_ondisktree_cursor_border_full(VAN_LAST)");

	fprintf(stderr, "====== 8 Destroy the tree(ondisk and inmemory) =====\n");
	ret = odtree->close(odtree, 0);
	CHK_RET(ret, "odtree->close");
	ret = tree->destroy(tree);
	CHK_RET(ret, "tree->destroy");

	(void)free_string_testdata(&td);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	return (ret);
}

/*
 * What a wrong expectation looks like.  odt_t1_1c() feeds these to the
 * checks and expects every one of them to be caught.
 *
 * There is no write cursor for an ondisk tree, so the directions are the
 * only thing that varies here.
 */
#define ODT_NEG_KEY     1
#define ODT_NEG_VALUE   2
#define ODT_NEG_COUNT   3
#define ODT_NEG_BORDERK 4
#define ODT_NEG_BORDERV 5

static char odt_neg_key[] = "zzzz-bogus-key-that-is-not-in-the-tree";
static char odt_neg_val[] = "zzzz-bogus-value-that-is-not-in-the-tree";

typedef struct {
	ONDISK_TREE *tree;
	flags_t      direction;
	TESTDATA     td;
	int          mode;
} odt_neg_t;

static int odt_neg_check(void *arg) {
	odt_neg_t *c;
	TESTDATA short_td;
	RECORD rec;
	size_t rcnt, idx;
	int backward;

	c = (odt_neg_t *)arg;
	rcnt = 0;
	backward = (c->direction == VAN_PREV || c->direction == VAN_LAST);
	/* Corrupt the record the walk reads first. */
	idx = backward ? c->td.count - 1 : 0;
	rec = c->td.records[idx];

	switch (c->mode) {
	case ODT_NEG_KEY:
		c->td.records[idx].key = odt_neg_key;
		c->td.records[idx].ksize = strlen(odt_neg_key);
		break;
	case ODT_NEG_VALUE:
		c->td.records[idx].value = odt_neg_val;
		c->td.records[idx].vsize = strlen(odt_neg_val);
		break;
	case ODT_NEG_COUNT:
		/* One record less than the tree holds. */
		TESTDATA_SLICE(&short_td, &c->td, 0, c->td.count - 1);
		return verify_ondisktree_cursor_direction_full(c->tree,
		    c->direction, 0, &rcnt, &short_td);
	case ODT_NEG_BORDERK:
		rec.key = odt_neg_key;
		rec.ksize = strlen(odt_neg_key);
		return verify_ondisktree_cursor_border_full(c->tree,
		    c->direction, 0, &rec);
	case ODT_NEG_BORDERV:
		rec.value = odt_neg_val;
		rec.vsize = strlen(odt_neg_val);
		return verify_ondisktree_cursor_border_full(c->tree,
		    c->direction, 0, &rec);
	default:
		return (0);
	}

	return verify_ondisktree_cursor_direction_full(c->tree, c->direction,
	    0, &rcnt, &c->td);
}

static int odt_neg_one(ONDISK_TREE *tree, TESTDATA *td, flags_t direction,
    int mode, const char *what) {
	odt_neg_t ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.tree = tree;
	ctx.direction = direction;
	ctx.td = *td;
	ctx.mode = mode;

	TEST_ASSERT2(verify_child_fails(odt_neg_check, &ctx),
	    "%s: the check passed, it must catch this", what);
	fprintf(stderr, "   caught: %s\n", what);

	return (0);
}

/*
 * Negative test for the checks odt_t1_1b() uses: every wrong expectation
 * below has to be caught, see imt_t1_3c() for why they run in a child.
 */
int odt_t1_1c() {
	VAN_TABLE *table;
	VAN_STORE *store;
	INMEM_TREE *tree;
	ONDISK_TREE *odtree;
	int ret, cnt;
	TESTDATA td;
	const char *odtpath;

	odtpath = "test1.ondisk.odt_t1c";
	system("rm -rf test1.ondisk.odt_t1c*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");

	ret = van_inmemtree_create(table, &tree);
	CHK_RET(ret, "van_inmemtree_create");
	tree->threshold = GIGABYTE;

	cnt = 10000;
	TESTDATA_INIT(&td, (size_t)cnt);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);

	fprintf(stderr, "============ 1 Try to put %lu records =========\n",
	    (unsigned long)cnt);
	ret = populate_inmemtree(tree, &td);
	CHK_RET(ret, "populate_inmemtree");

	fprintf(stderr, "========== 2 Build the ondisk tree ============\n");
	ret = van_ondisktree_create(table, &odtree);
	CHK_RET(ret, "van_ondisktree_create");
	ret = odtree->open(odtree, odtpath,
	    VAN_OPEN_CREAT | VAN_OPEN_TRUNC);
	CHK_RET(ret, "odtree->open(VAN_OPEN_CREAT | VAN_OPEN_TRUNC)");
	ret = odtree->store_inmem(odtree, tree);
	CHK_RET(ret, "odtree->store_inmem");

	fprintf(stderr, "========== 3 Sort the expected records ==========\n");
	ret = sort_testdata(&td, odtree->table->cmp_func);
	CHK_RET(ret, "sort_testdata");

	fprintf(stderr, "======== 4 A wrong record must be caught ========\n");
	ret = odt_neg_one(odtree, &td, VAN_NEXT, ODT_NEG_KEY,
	    "VAN_NEXT, wrong key");
	ret = odt_neg_one(odtree, &td, VAN_PREV, ODT_NEG_KEY,
	    "VAN_PREV, wrong key");
	ret = odt_neg_one(odtree, &td, VAN_NEXT, ODT_NEG_VALUE,
	    "VAN_NEXT, wrong value");
	ret = odt_neg_one(odtree, &td, VAN_PREV, ODT_NEG_VALUE,
	    "VAN_PREV, wrong value");

	fprintf(stderr, "======== 5 A wrong count must be caught =========\n");
	ret = odt_neg_one(odtree, &td, VAN_NEXT, ODT_NEG_COUNT,
	    "VAN_NEXT, one record too few");
	ret = odt_neg_one(odtree, &td, VAN_PREV, ODT_NEG_COUNT,
	    "VAN_PREV, one record too few");

	fprintf(stderr, "======== 6 A wrong border must be caught ========\n");
	ret = odt_neg_one(odtree, &td, VAN_FIRST, ODT_NEG_BORDERK,
	    "VAN_FIRST, wrong key");
	ret = odt_neg_one(odtree, &td, VAN_LAST, ODT_NEG_BORDERK,
	    "VAN_LAST, wrong key");
	ret = odt_neg_one(odtree, &td, VAN_FIRST, ODT_NEG_BORDERV,
	    "VAN_FIRST, wrong value");
	ret = odt_neg_one(odtree, &td, VAN_LAST, ODT_NEG_BORDERV,
	    "VAN_LAST, wrong value");

	fprintf(stderr, "====== 7 Destroy the tree(ondisk and inmemory) ===\n");
	ret = odtree->close(odtree, 0);
	CHK_RET(ret, "odtree->close");
	ret = tree->destroy(tree);
	CHK_RET(ret, "tree->destroy");

	(void)free_string_testdata(&td);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	return (ret);
}

int odt_t1_2() {
#define TNUM_ODT_T1_2 5
	VAN_STORE *store;
	VAN_TABLE *table;
	ONDISK_TREE *odtree, *modtree, *odtrees[TNUM_ODT_T1_2];
	int ret, cnt, j;
	time_t t1, t2;
	TESTDATA td, slice;

	const size_t record_cnt = 100000;

	memset(odtrees, 0, sizeof(odtrees));
	modtree = NULL;

	cnt = TNUM_ODT_T1_2 * record_cnt;
	TESTDATA_INIT(&td, (size_t)cnt);

	(void)generate_string_testdata(&td, 20, 30, 50, 120);

	system("rm -f od_table.ondisk*");
	system("rm -f od_table.odlist*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, GIGABYTE);
	ret = table->open(table, "od_table", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");

	for (j = 0; j < TNUM_ODT_T1_2; j++) {
		fprintf(stderr,
	"============ %d.1 Try to put %lu records to inmemtree-%d =========\n",
		    j, (unsigned long)record_cnt, j);
		van_alloc_stat_reset();
		van_rwlock_stat_reset();
		/* The slice of the data set this round works on. */
		TESTDATA_SLICE(&slice, &td, (size_t)(j * record_cnt),
		    record_cnt);
		ret = populate_table(table, &slice);
		CHK_RET(ret, "populate_table");
		van_rwlock_stat_print(stderr, " ");
		fprintf(stderr, "**** Malloc Statistics ****\n");
		van_alloc_stat_print(stderr, " ");

		fprintf(stderr,
			"============ %d.2 Try to get %lu records from inmemtree-%d =========\n",
		    j, (unsigned long)record_cnt, j);
		ret = verify_table_data(table, &slice);
		CHK_RET(ret, "verify_table_data(odtree)");

		if (j+1 < TNUM_ODT_T1_2) {
			fprintf(stderr,
			    "========== %d.3 Build the ondisk tree ============\n", j);
			time(&t1);
			ret = table->dump_inmem(table);
			CHK_RET(ret, "table->dump_inmem");
			time(&t2);
			fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
			odtree = table->odt_items[j].trees[0];
			odtrees[j] = odtree;
			fprintf(stderr, "%d.3.2 Getting records from the odtree:%s\n",
			    j, odtree->file_path);
			ret = verify_ondisktree_data(odtree, &slice);
			CHK_RET(ret, "verify_ondisktree_data(odtree)");
		}
	}

	ret = table->write_odt_paths(table);
	CHK_RET(ret, "table->write_odt_paths");
	table->show_odt_items(table, stderr);
	fprintf(stderr, "========== 6 Get all using table (1) ==========\n");
	ret = verify_table_data(table, &td);
	CHK_RET(ret, "verify_table_data(1)");

	fprintf(stderr, "========== 6 Merge and get (2) ==========\n");
	ret = table->dump_inmem(table);
	CHK_RET(ret, "table->dump_inmem");
	odtree = table->odt_items[TNUM_ODT_T1_2 - 1].trees[0];
	odtrees[TNUM_ODT_T1_2 - 1] = odtree;
	ret = table->merge_ondisktrees(table, odtrees, TNUM_ODT_T1_2, &modtree);
	CHK_RET(ret, "table->merge_ondisktrees");
	ret = verify_ondisktree_data(modtree, &td);
	CHK_RET(ret, "verify_ondisktree_data(2, modtree)");
	ret = table->write_odt_paths(table);
	CHK_RET(ret, "table->write_odt_paths");
	table->show_odt_items(table, stderr);

	ret = table->replace_odt_items(table, 0, TNUM_ODT_T1_2 - 1, modtree);
	CHK_RET(ret, "table->replace_odt_items");
	table->show_odt_items(table, stderr);

	fprintf(stderr, "========== 6 Get using table again (3) ==========\n");
	ret = verify_table_data(table, &td);
	CHK_RET(ret, "verify_table_data(3)");

	fprintf(stderr, "====== 7 Free the memory and close the table ====\n");
	time(&t1);
	(void)free_string_testdata(&td);
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
	TESTDATA td;

	memset(odtrees, 0, sizeof(odtrees));
	modtree = NULL;

	cnt = record_cnt;
	TESTDATA_INIT(&td, (size_t)cnt);
	opsz = cnt * sizeof(int);
	ops = (int *)malloc(opsz);
	TEST_ASSERT(ops != NULL);
	ops_all = (int *)malloc(opsz);
	TEST_ASSERT(ops_all != NULL);

	(void)generate_string_testdata(&td, 20, 30, 50, 120);
	system("rm -f od_table.ondisk*");
	system("rm -f od_table.odlist*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, GIGABYTE);
	ret = table->open(table, "od_table", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");

	fprintf(stderr, "============ 1 Intial put %lu records =========\n",
	    (unsigned long)record_cnt);
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = populate_table(table, &td);
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");

	fprintf(stderr, "============ 2 Intial get %lu records =========\n",
	    (unsigned long)record_cnt);
	ret = verify_table_data(table, &td);


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
			key.data = td.records[k].key;
			key.size = td.records[k].ksize;
			if (ops[k] == OP_DELETE) {
				free(td.records[k].value);
				td.records[k].value = NULL;
				td.records[k].vsize = 0;
				ret = table->del(table, &key, 0);
				if (ops_all[k] == OP_DELETE) {
					TEST_ASSERT(ret == VAN_NOTFOUND || ret == 0);
				} else {
					CHK_RET(ret, "table->del");
				}
			} else if (ops[k] == OP_UPDATE) {
				free(td.records[k].value);
				getrandomstring2(vbuf, 50, 120);
				vsz = strlen(vbuf) + 1;
				td.records[k].vsize = vsz;
				td.records[k].value = (char *)malloc(vsz);
				TEST_ASSERT(td.records[k].value != NULL);
				memcpy(td.records[k].value, vbuf, vsz);
				memset(&value, 0, sizeof(value));
				value.data = td.records[k].value;
				value.size = td.records[k].vsize;
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
			key.data = td.records[k].key;
			key.size = td.records[k].ksize;
			memset(&value, 0, sizeof(value));
			ret = table->get(table, &key, &value, 0);
			if (ops_all[k] == OP_DELETE) {
				TEST_ASSERT(ret == VAN_NOTFOUND);
			} else {
				CHK_RET(ret, "table->get");
				cmp = strcmp(td.records[k].value,
				    (char *)value.data);
				TEST_ASSERT(cmp == 0);
			}
		}

		if (j + 1 < TNUM_ODT_T1_3) {
			/* Dump */
			fprintf(stderr, "======== 3.%d.4 Dump and Verify Operations again =======\n", (int)j);
			time(&t1);
			ret = table->dump_inmem(table);
			CHK_RET(ret, "table->dump_inmem");
			time(&t2);
			fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

			/* Verify Operations Again */
			for (k = 0; k < record_cnt; k++) {
				memset(&key, 0, sizeof(key));
				key.data = td.records[k].key;
				key.size = td.records[k].ksize;
				memset(&value, 0, sizeof(value));
				ret = table->get(table, &key, &value, 0);
				if (ops_all[k] == OP_DELETE) {
					TEST_ASSERT(ret == VAN_NOTFOUND);
				} else {
					CHK_RET(ret, "table->get");
					cmp = strcmp(td.records[k].value,
					    (char *)value.data);
					TEST_ASSERT(cmp == 0);
				}
			}
			odtrees[j] = table->odt_items[j].trees[0];

		}

	}

	ret = table->write_odt_paths(table);
	CHK_RET(ret, "table->write_odt_paths");

	fprintf(stderr, "========== 4 Merge and get ==========\n");
	ret = table->merge_ondisktrees(table, odtrees, TNUM_ODT_T1_3 - 1, &modtree);
	CHK_RET(ret, "table->merge_ondisktrees");
	total = 0;
	dupcount = 0;
	odtree = modtree;
	ret = check_ondisktree_duplicate(odtree, 100, &total, &dupcount);
	CHK_RET(ret, "check_ondisktree_duplicate");
	fprintf(stderr, "Items in merged tree: total(%lu), dupcount(%lu)\n",
	    (unsigned long)total, (unsigned long)dupcount);
	ret = table->replace_odt_items(table, 0, TNUM_ODT_T1_3 - 2, modtree);
	CHK_RET(ret, "table->replace_odt_items");
	/* Verify Operations Again */
	for (k = 0; k < record_cnt; k++) {
		memset(&key, 0, sizeof(key));
		key.data = td.records[k].key;
		key.size = td.records[k].ksize;
		memset(&value, 0, sizeof(value));
		ret = table->get(table, &key, &value, 0);

		if (ops_all[k] == OP_DELETE) {
			TEST_ASSERT(ret == VAN_NOTFOUND);
		} else {
			CHK_RET(ret, "table->get");
			cmp = strcmp(td.records[k].value,
			    (char *)value.data);
			TEST_ASSERT(cmp == 0);
		}
	}
	ret = table->write_odt_paths(table);
	CHK_RET(ret, "table->write_odt_paths");


	fprintf(stderr, "====== 5 Free the memory and close the table ====\n");
	time(&t1);
	(void)free_string_testdata(&td);
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
