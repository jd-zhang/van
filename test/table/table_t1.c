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
 * What the populate threads get: the table plus the slice of the test data
 * they have to put.
 */
typedef struct {
	VAN_TABLE *table;
	TESTDATA   td;
} tb_t1_pop_t;

static void *tb_t1_popthread(void *obj) {
	tb_t1_pop_t *dataobj;
	VAN_TABLE *table;
	int ret;

	dataobj = (tb_t1_pop_t *)obj;
	table = dataobj->table;
	ret = populate_table(table, &dataobj->td);
	CHK_RET(ret, "populate_table");

	return (NULL);
}

/*
 * A tiny open addressing hash set of null terminated strings.  Used by
 * table_t1_4 to generate random string keys while guaranteeing that no
 * two keys are the same.
 */
typedef struct tb_t1_strset {
	char **slots;	/* NULL means an empty slot */
	size_t mask;	/* capacity - 1, capacity is a power of two */
} TB_T1_STRSET;

static uint32_t tb_t1_strset_hash(const char *s) {
	uint32_t h;

	h = 2166136261U;	/* FNV-1a offset basis */
	while (*s != '\0') {
		h ^= (unsigned char)*s;
		h *= 16777619U;
		s++;
	}
	return (h);
}

static void tb_t1_strset_init(TB_T1_STRSET *set, size_t expect) {
	size_t cap, i;

	cap = 1;
	while (cap < expect * 2)
		cap <<= 1;
	set->mask = cap - 1;
	set->slots = (char **)malloc(cap * sizeof(char *));
	TEST_ASSERT(set->slots != NULL);
	for (i = 0; i < cap; i++)
		set->slots[i] = NULL;
}

/*
 * Add a key into the set.  Return 1 if the key was newly inserted, 0 if
 * the key already exists (in that case nothing is inserted).
 */
static int tb_t1_strset_add(TB_T1_STRSET *set, const char *key) {
	size_t indx, i;
	uint32_t hash;

	hash = tb_t1_strset_hash(key);
	i = 0;
	indx = hash & set->mask;
	while (set->slots[indx] != NULL) {
		/* Defensive: the table never gets that full, the load
		 * factor stays below 1/2. */
		TEST_ASSERT(i < set->mask + 1);
		if (strcmp(set->slots[indx], key) == 0)
			return (0);
		indx = (indx + 1) & set->mask;
		i++;
	}
	set->slots[indx] = (char *)malloc(strlen(key) + 1);
	TEST_ASSERT(set->slots[indx] != NULL);
	strcpy(set->slots[indx], key);
	return (1);
}

static void tb_t1_strset_destroy(TB_T1_STRSET *set) {
	size_t cap, i;

	if (set->slots == NULL)
		return;
	cap = set->mask + 1;
	for (i = 0; i < cap; i++) {
		if (set->slots[i] != NULL)
			free(set->slots[i]);
	}
	free(set->slots);
	set->slots = NULL;
	set->mask = 0;
}

/*
 test populate_table with multiple threads, but avoid inmem dumping.
 */
#define TB_T1_NUM 5
int table_t1_1() {
	VAN_TABLE *table;
	VAN_STORE *store;
	pthread_t threads[TB_T1_NUM];
	tb_t1_pop_t objs[TB_T1_NUM], *objp;
	size_t each_cnt;
	int i, ret, cnt;
	void *rptr;
	TESTDATA td;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	store->set_cachesize(store, GIGABYTE);
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, 5*MEGABYTE);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");
	ret = table->start_threads(table);
	CHK_RET(ret, "table->start_threads");

	memset(threads, 0, sizeof(threads));
	cnt = 1000000;
	TESTDATA_INIT(&td, (size_t)cnt);
	each_cnt = cnt / TB_T1_NUM;
	memset(objs, 0, sizeof(objs));
	for (i = 0; i < TB_T1_NUM; i++) {
		objp = &objs[i];
		objp->table = table;
		TESTDATA_SLICE(&objp->td, &td, (size_t)(i * each_cnt),
		    each_cnt);
	}

	(void)generate_string_testdata(&td, 20, 30, 50, 120);

	for (i = 0; i < TB_T1_NUM; i++) {
		ret = pthread_create(&threads[i], NULL,
		    tb_t1_popthread, &objs[i]);
	}

	for (i = 0; i < TB_T1_NUM; i++) {
		ret = pthread_join(threads[i], &rptr);
	}

	(void)free_string_testdata(&td);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_strarray_print(store->log_handle->lognames, stderr, "\n");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	ret = verify_logfiles_from_scratch(".", "test1");
	CHK_RET(ret, "verify_logfiles_from_scratch");

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
	TESTDATA td;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	store->set_cachesize(store, 200*MEGABYTE);
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
	TESTDATA_INIT(&td, (size_t)cnt);

	(void)generate_string_testdata(&td, 20, 30, 50, 120);

	ret = populate_table(table, &td);
	CHK_RET(ret, "populate_table");

	(void)free_string_testdata(&td);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	ret = verify_logfiles_from_scratch(".", "test1");
	CHK_RET(ret, "verify_logfiles_from_scratch");

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
	tb_t1_pop_t objs[TB_T1_NUM], *objp;
	size_t each_cnt;
	int i, ret, cnt;
	void *rptr;
	TESTDATA td;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	store->set_cachesize(store, GIGABYTE);
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
	TESTDATA_INIT(&td, (size_t)cnt);
	each_cnt = cnt / TB_T1_NUM;
	memset(objs, 0, sizeof(objs));
	for (i = 0; i < TB_T1_NUM; i++) {
		objp = &objs[i];
		objp->table = table;
		TESTDATA_SLICE(&objp->td, &td, (size_t)(i * each_cnt),
		    each_cnt);
	}

	fprintf(stderr, "============ 1 generate %lu records =========\n",
	    (unsigned long)cnt);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);

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
	ret = verify_table_data(table, &td);
	CHK_RET(ret, "verify_table_data");

	(void)free_string_testdata(&td);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	ret = verify_logfiles_from_scratch(".", "test1");
	CHK_RET(ret, "verify_logfiles_from_scratch");

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
	tb_t1_pop_t objs[TB_T1_NUM], *objp;
	size_t each_cnt;
	int i, ret, cnt;
	void *rptr;
	TESTDATA td;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	store->set_cachesize(store, 200*MEGABYTE);
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
	TESTDATA_INIT(&td, (size_t)cnt);
	each_cnt = cnt / TB_T1_NUM;
	memset(objs, 0, sizeof(objs));
	for (i = 0; i < TB_T1_NUM; i++) {
		objp = &objs[i];
		objp->table = table;
		TESTDATA_SLICE(&objp->td, &td, (size_t)(i * each_cnt),
		    each_cnt);
	}

	fprintf(stderr, "============ 1 generate %lu records =========\n",
	    (unsigned long)cnt);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);

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
		ret = verify_table_data(table, &td);
		CHK_RET_NOEXIT(ret, "verify_table_data");
		table->show_odt_items(table, stderr);
	}
	/*
	ret = verify_table_data(table, &td);
	CHK_RET(ret, "verify_table_data");
	*/

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	/*
	 * Dump and merge run concurrently here, so the odlist that ends up
	 * on disk can lag behind the WAL.  Reopen and check the data
	 * itself: if every record is still readable, the list the WAL ends
	 * with is the good one and whatever else the odlist lists is a
	 * merged-away tree whose removal did not happen.  A failure here
	 * would instead mean real data loss, which is far worse.
	 *
	 * Closing the reopened table rewrites the odlist, so the
	 * consistency check below sees the state after that rewrite.
	 */
	ret = verify_table_after_reopen(".", "test1", &td);
	CHK_RET(ret, "verify_table_after_reopen");

	(void)free_string_testdata(&td);

	ret = verify_logfiles_from_scratch(".", "test1");
	CHK_RET(ret, "verify_logfiles_from_scratch");

	return (ret);
}

/*
 * Verify both directions of table->traverse() against the expected records.
 *
 * The records are put through table->put(), then the expected arrays are
 * sorted with the compare function of the table, so that a forward
 * traversal has to hand back records[0] .. records[count - 1] and a
 * backward one
 * the same records in reverse.
 *
 * A table has no cursor, so verify_table_cursor_direction_full() and
 * verify_table_cursor_border_full() walk it with table->traverse():
 * VAN_NEXT / VAN_FIRST forward, VAN_PREV / VAN_LAST backward.
 */
int table_t1_1e() {
	VAN_TABLE *table;
	VAN_STORE *store;
	int ret, cnt;
	size_t rcnt;
	TESTDATA td;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, MEGABYTE);
	ret = table->open(table, "test1", 0);
	CHK_RET(ret, "table->open");

	cnt = 100000;
	TESTDATA_INIT(&td, (size_t)cnt);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);

	fprintf(stderr, "============ 1 put %lu records =========\n",
	    (unsigned long)cnt);
	ret = populate_table(table, &td);
	CHK_RET(ret, "populate_table");

	fprintf(stderr, "============ 2 Sort the expected records =========\n");
	ret = sort_testdata(&td, table->cmp_func);
	CHK_RET(ret, "sort_testdata");

	fprintf(stderr,
	    "============ 3 traverse forward (VAN_NEXT) =========\n");
	rcnt = 0;
	ret = verify_table_cursor_direction_full(table, VAN_NEXT, 0, &rcnt,
	    &td);
	CHK_RET(ret, "verify_table_cursor_direction_full(VAN_NEXT)");
	TEST_ASSERT2(rcnt == (size_t)cnt, "expected %d records, walked %lu",
	    cnt, (unsigned long)rcnt);

	fprintf(stderr,
	    "============ 4 traverse backward (VAN_PREV) =========\n");
	rcnt = 0;
	ret = verify_table_cursor_direction_full(table, VAN_PREV, 0, &rcnt,
	    &td);
	CHK_RET(ret, "verify_table_cursor_direction_full(VAN_PREV)");
	TEST_ASSERT2(rcnt == (size_t)cnt, "expected %d records, walked %lu",
	    cnt, (unsigned long)rcnt);

	fprintf(stderr, "============ 5 VAN_FIRST is the first record =====\n");
	ret = verify_table_cursor_border_full(table, VAN_FIRST, 0,
	    &td.records[0]);
	CHK_RET(ret, "verify_table_cursor_border_full(VAN_FIRST)");

	fprintf(stderr, "============ 6 VAN_LAST is the last record ======\n");
	ret = verify_table_cursor_border_full(table, VAN_LAST, 0,
	    &td.records[td.count - 1]);
	CHK_RET(ret, "verify_table_cursor_border_full(VAN_LAST)");

	(void)free_string_testdata(&td);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	ret = verify_logfiles_from_scratch(".", "test1");
	CHK_RET(ret, "verify_logfiles_from_scratch");

	return (ret);
}

/*
 * What a wrong expectation looks like.  table_t1_1f() feeds these to the
 * checks and expects every one of them to be caught.
 *
 * A table has no cursor and no cursor flags, so the traversal direction is
 * the only thing that varies.
 */
#define TBL_NEG_KEY     1
#define TBL_NEG_VALUE   2
#define TBL_NEG_COUNT   3
#define TBL_NEG_BORDERK 4
#define TBL_NEG_BORDERV 5

static char tbl_neg_key[] = "zzzz-bogus-key-that-is-not-in-the-table";
static char tbl_neg_val[] = "zzzz-bogus-value-that-is-not-in-the-table";

typedef struct {
	VAN_TABLE *table;
	flags_t    direction;
	TESTDATA   td;
	int        mode;
} tbl_neg_t;

static int tbl_neg_check(void *arg) {
	tbl_neg_t *c;
	TESTDATA short_td;
	RECORD rec;
	size_t rcnt, idx;
	int backward;

	c = (tbl_neg_t *)arg;
	rcnt = 0;
	backward = (c->direction == VAN_PREV || c->direction == VAN_LAST);
	/* Corrupt the record the traversal reads first. */
	idx = backward ? c->td.count - 1 : 0;
	rec = c->td.records[idx];

	switch (c->mode) {
	case TBL_NEG_KEY:
		c->td.records[idx].key = tbl_neg_key;
		c->td.records[idx].ksize = strlen(tbl_neg_key);
		break;
	case TBL_NEG_VALUE:
		c->td.records[idx].value = tbl_neg_val;
		c->td.records[idx].vsize = strlen(tbl_neg_val);
		break;
	case TBL_NEG_COUNT:
		/* One record less than the table holds. */
		TESTDATA_SLICE(&short_td, &c->td, 0, c->td.count - 1);
		return verify_table_cursor_direction_full(c->table,
		    c->direction, 0, &rcnt, &short_td);
	case TBL_NEG_BORDERK:
		rec.key = tbl_neg_key;
		rec.ksize = strlen(tbl_neg_key);
		return verify_table_cursor_border_full(c->table,
		    c->direction, 0, &rec);
	case TBL_NEG_BORDERV:
		rec.value = tbl_neg_val;
		rec.vsize = strlen(tbl_neg_val);
		return verify_table_cursor_border_full(c->table,
		    c->direction, 0, &rec);
	default:
		return (0);
	}

	return verify_table_cursor_direction_full(c->table, c->direction,
	    0, &rcnt, &c->td);
}

static int tbl_neg_one(VAN_TABLE *table, TESTDATA *td, flags_t direction,
    int mode, const char *what) {
	tbl_neg_t ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.table = table;
	ctx.direction = direction;
	ctx.td = *td;
	ctx.mode = mode;

	TEST_ASSERT2(verify_child_fails(tbl_neg_check, &ctx),
	    "%s: the check passed, it must catch this", what);
	fprintf(stderr, "   caught: %s\n", what);

	return (0);
}

/*
 * Negative test for the checks table_t1_1e() uses: every wrong expectation
 * below has to be caught, see imt_t1_3c() for why they run in a child.
 */
int table_t1_1f() {
	VAN_TABLE *table;
	VAN_STORE *store;
	int ret, cnt;
	TESTDATA td;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, MEGABYTE);
	ret = table->open(table, "test1", 0);
	CHK_RET(ret, "table->open");

	cnt = 100000;
	TESTDATA_INIT(&td, (size_t)cnt);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);

	fprintf(stderr, "============ 1 put %lu records =========\n",
	    (unsigned long)cnt);
	ret = populate_table(table, &td);
	CHK_RET(ret, "populate_table");

	fprintf(stderr, "============ 2 Sort the expected records =========\n");
	ret = sort_testdata(&td, table->cmp_func);
	CHK_RET(ret, "sort_testdata");

	fprintf(stderr, "============ 3 A wrong record must be caught =====\n");
	ret = tbl_neg_one(table, &td, VAN_NEXT, TBL_NEG_KEY,
	    "VAN_NEXT, wrong key");
	ret = tbl_neg_one(table, &td, VAN_PREV, TBL_NEG_KEY,
	    "VAN_PREV, wrong key");
	ret = tbl_neg_one(table, &td, VAN_NEXT, TBL_NEG_VALUE,
	    "VAN_NEXT, wrong value");
	ret = tbl_neg_one(table, &td, VAN_PREV, TBL_NEG_VALUE,
	    "VAN_PREV, wrong value");

	fprintf(stderr, "============ 4 A wrong count must be caught ======\n");
	ret = tbl_neg_one(table, &td, VAN_NEXT, TBL_NEG_COUNT,
	    "VAN_NEXT, one record too few");
	ret = tbl_neg_one(table, &td, VAN_PREV, TBL_NEG_COUNT,
	    "VAN_PREV, one record too few");

	fprintf(stderr, "============ 5 A wrong border must be caught =====\n");
	ret = tbl_neg_one(table, &td, VAN_FIRST, TBL_NEG_BORDERK,
	    "VAN_FIRST, wrong key");
	ret = tbl_neg_one(table, &td, VAN_LAST, TBL_NEG_BORDERK,
	    "VAN_LAST, wrong key");
	ret = tbl_neg_one(table, &td, VAN_FIRST, TBL_NEG_BORDERV,
	    "VAN_FIRST, wrong value");
	ret = tbl_neg_one(table, &td, VAN_LAST, TBL_NEG_BORDERV,
	    "VAN_LAST, wrong value");

	(void)free_string_testdata(&td);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	ret = verify_logfiles_from_scratch(".", "test1");
	CHK_RET(ret, "verify_logfiles_from_scratch");

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
	tb_t1_pop_t objs[TB_T1_NUM], *objp;
	size_t each_cnt;
	int i, ret, cnt;
	void *rptr;
	TESTDATA td;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	store->set_cachesize(store, 200*MEGABYTE);
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
	TESTDATA_INIT(&td, (size_t)cnt);
	each_cnt = cnt / TB_T1_NUM;
	memset(objs, 0, sizeof(objs));
	for (i = 0; i < TB_T1_NUM; i++) {
		objp = &objs[i];
		objp->table = table;
		TESTDATA_SLICE(&objp->td, &td, (size_t)(i * each_cnt),
		    each_cnt);
	}

	fprintf(stderr, "============ 1 generate %lu records =========\n",
	    (unsigned long)cnt);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);

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
	ret = verify_table_data(table, &td);
	CHK_RET(ret, "verify_table_data");

	fprintf(stderr, "============ 4 close table =========\n");
	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");
	table = NULL;
	store = NULL;

	fprintf(stderr, "============ 5 reopen table from ondisk =========\n");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	ret = table->open(table, "test1", 0);
	CHK_RET(ret, "table->open");

	fprintf(stderr, "============ 6 verify %lu records after reopen =========\n",
	    (unsigned long)cnt);
	ret = verify_table_data(table, &td);
	CHK_RET(ret, "verify_table_data");

	(void)free_string_testdata(&td);

	fprintf(stderr, "============ 7 print log file names =========\n");
	ret = van_strarray_print(store->log_handle->lognames, stderr, "\n");
	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	ret = verify_logfiles_from_scratch(".", "test1");
	CHK_RET(ret, "verify_logfiles_from_scratch");

	return (ret);

}

/*
 * Test repeatedly putting and dumping the same key set, which produces a
 * series of ondisk trees whose keys overlap each other.  The background
 * merge thread merges these ondisk trees while we keep verifying the
 * whole data through the table interface, so that merge bugs (e.g. losing
 * the latest value of an overlapped key) can be caught.
 *
 * Steps:
 * 1. prepare 10000 ordered keys: key00000 ... key09999.
 * 2. run 100 groups, each group puts the same 10000 keys again with the
 *    value "value%05d-%02d" (key-index, group), then dumps the inmem tree
 *    to an ondisk tree by table->dump_inmem, so there will be 100 ondisk
 *    trees that share the same keys but carry different values.
 * 3. verify the whole data round by round, sleeping 10 seconds between
 *    two verify passes so that the background thread has time to merge.
 *    since every key is put 100 times, only the last (group 99) value is
 *    the expected one, which is exactly what verify_table_data checks.
 *    once the ondisk trees have been merged down to fewer than merge_min
 *    (table->odt_top < table->merge_cfg.merge_min) no further merge can
 *    ever be triggered, so the verify loop stops early instead of always
 *    running TB_T1_3_VERIFY_TIMES rounds.
 * 4. close the table and the store.
 */
#define TB_T1_3_NKEYS 100000
#define TB_T1_3_NGROUPS 100
#define TB_T1_3_VERIFY_TIMES 50  /* upper bound of the verify rounds */
#define TB_T1_3_VERIFY_INTERVAL 10
int table_t1_3() {
	VAN_TABLE *table;
	VAN_STORE *store;
	int ret, i, g, v, failcnt;
	size_t cnt;
	char keybuf[64];
	char valbuf[64];
	TESTDATA td;

	failcnt = 0;
	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	/*
	 * Keep the inmem threshold big, so that the scan thread never
	 * triggers a dump automatically; the dump is only done by the
	 * table->dump_inmem calls below.
	 */
	table->set_inmem_threshold(table, GIGABYTE);
	/* Merge the ondisk trees aggressively in the background. */
	table->set_merge_config(table, 4, 6);
	table->set_flags(table, VAN_TABLE_VERBOSE_MERGE);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");

	cnt = TB_T1_3_NKEYS;
	TESTDATA_INIT(&td, (size_t)cnt);
	fprintf(stderr, "============ 1 generate %lu ordered keys =========\n",
	    (unsigned long)cnt);
	for (i = 0; i < (int)cnt; i++) {
		(void)snprintf(keybuf, sizeof(keybuf), "key%05d", i);
		td.records[i].ksize = strlen(keybuf);
		td.records[i].key = (char *)malloc(td.records[i].ksize + 1);
		TEST_ASSERT(td.records[i].key != NULL);
		memcpy(td.records[i].key, keybuf, td.records[i].ksize + 1);

		(void)snprintf(valbuf, sizeof(valbuf), "value%05d-00", i);
		td.records[i].vsize = strlen(valbuf);
		td.records[i].value = (char *)malloc(td.records[i].vsize + 1);
		TEST_ASSERT(td.records[i].value != NULL);
		memcpy(td.records[i].value, valbuf, td.records[i].vsize + 1);
	}

	fprintf(stderr,
	    "============ 2 put & dump %d groups =========\n",
	    TB_T1_3_NGROUPS);
	for (g = 0; g < TB_T1_3_NGROUPS; g++) {
		fprintf(stderr, "--- group %d: put %lu records ---\n",
		    g, (unsigned long)cnt);
		for (i = 0; i < (int)cnt; i++) {
			(void)snprintf(valbuf, sizeof(valbuf),
			    "value%05d-%02d", i, g);
			td.records[i].vsize = strlen(valbuf);
			memcpy(td.records[i].value, valbuf,
			    td.records[i].vsize + 1);
		}
		ret = populate_table(table, &td);
		CHK_RET(ret, "populate_table");
		ret = table->dump_inmem(table);
		CHK_RET(ret, "table->dump_inmem");
	}
	ret = table->start_threads(table);
	CHK_RET(ret, "table->start_threads");

	fprintf(stderr,
	    "============ 3 verify %lu records %d times during merge =========\n",
	    (unsigned long)cnt, TB_T1_3_VERIFY_TIMES);
	table->show_odt_items(table, stderr);
	for (v = 0; v < TB_T1_3_VERIFY_TIMES; v++) {
		if (v > 0) {
			fprintf(stderr,
			    "--- sleep %d seconds to give merge time ---\n",
			    TB_T1_3_VERIFY_INTERVAL);
			_van_sleep(TB_T1_3_VERIFY_INTERVAL, 0);
		}
		fprintf(stderr, "--- verify round %d/%d ---\n",
		    v + 1, TB_T1_3_VERIFY_TIMES);
		ret = verify_table_data(table, &td);
		CHK_RET_NOEXIT(ret, "verify_table_data");
		if (ret != 0)
			failcnt++;
		table->show_odt_items(table, stderr);
		/*
		 * A merge needs at least merge_min ondisk trees, and the
		 * background merge thread compacts odt_items (so that
		 * table->odt_top is exactly the number of live trees) at
		 * the end of every merge scan.  Once odt_top drops below
		 * merge_min there can be no further merge, so the data is
		 * stable and the verify loop can stop here.
		 */
		if (table->odt_top < table->merge_cfg.merge_min) {
			fprintf(stderr,
			    "--- odt_top %lu < merge_min %lu, "
			    "no more merge possible, stop verifying ---\n",
			    (unsigned long)table->odt_top,
			    (unsigned long)table->merge_cfg.merge_min);
			break;
		}
	}

	fprintf(stderr, "============ 4 close table and store =========\n");
	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	ret = verify_logfiles_from_scratch(".", "test1");
	CHK_RET(ret, "verify_logfiles_from_scratch");

	(void)free_string_testdata(&td);

	return ((failcnt > 0) ? (-1) : (0));
}

/*
 * Same as table_t1_3, but the 10000 keys are random strings (every key is
 * guaranteed unique, duplicates are rerolled) instead of the ordered
 * "key%05d" keys, and each group refreshes every key with a random value
 * instead of a deterministic one.
 *
 * Repeatedly putting and dumping the same random key set produces a series
 * of fully overlapped ondisk trees, so the background merge thread must
 * keep the newest random value of every key; that is what we verify while
 * the merge is running.
 *
 * Steps:
 * 1. prepare 10000 unique random string keys and an initial random value
 *    for each of them.
 * 2. run 100 groups, each group puts the same 10000 keys again with a new
 *    random value for every key, then dumps the inmem tree by
 *    table->dump_inmem, so there will be 100 fully overlapped ondisk trees.
 * 3. verify the whole data round by round, sleeping 10 seconds between two
 *    verify passes so that the background thread has time to merge.  since
 *    every key is put 100 times, only the last (group 99) value is the
 *    expected one, which is exactly what verify_table_data checks.  once
 *    the ondisk trees have been merged down to fewer than merge_min
 *    (table->odt_top < table->merge_cfg.merge_min) no further merge can
 *    ever be triggered, so the verify loop stops early instead of always
 *    running TB_T1_4_VERIFY_TIMES rounds.
 * 4. close the table and the store.
 */
#define TB_T1_4_NKEYS 100000
#define TB_T1_4_NGROUPS 100
#define TB_T1_4_KEY_MINLEN 8
#define TB_T1_4_KEY_MAXLEN 20
#define TB_T1_4_VAL_MINLEN 8
#define TB_T1_4_VAL_MAXLEN 20
#define TB_T1_4_VERIFY_TIMES 50  /* upper bound of the verify rounds */
#define TB_T1_4_VERIFY_INTERVAL 10
int table_t1_4() {
	VAN_TABLE *table;
	VAN_STORE *store;
	TB_T1_STRSET strset;
	int ret, i, g, v, failcnt, len;
	size_t cnt;
	char kbuf[MAX_BUF_LEN];
	char vbuf[MAX_BUF_LEN];
	TESTDATA td;

	failcnt = 0;
	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	/*
	 * Keep the inmem threshold big, so that the scan thread never
	 * triggers a dump automatically; the dump is only done by the
	 * table->dump_inmem calls below.
	 */
	table->set_inmem_threshold(table, GIGABYTE);
	/* Merge the ondisk trees aggressively in the background. */
	table->set_merge_config(table, 4, 6);
	table->set_flags(table, VAN_TABLE_VERBOSE_MERGE);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");

	cnt = TB_T1_4_NKEYS;
	TESTDATA_INIT(&td, (size_t)cnt);
	fprintf(stderr,
	    "============ 1 generate %lu unique random keys =========\n",
	    (unsigned long)cnt);
	tb_t1_strset_init(&strset, cnt);
	for (i = 0; i < (int)cnt; i++) {
		/* Roll a random key until it is not seen before. */
		do {
			len = getrandomstring2(kbuf, TB_T1_4_KEY_MINLEN,
			    TB_T1_4_KEY_MAXLEN);
			kbuf[len] = '\0';
		} while (tb_t1_strset_add(&strset, kbuf) == 0);
		td.records[i].ksize = (size_t)len;
		td.records[i].key = (char *)malloc((size_t)len + 1);
		TEST_ASSERT(td.records[i].key != NULL);
		memcpy(td.records[i].key, kbuf, (size_t)len + 1);

		len = getrandomstring2(vbuf, TB_T1_4_VAL_MINLEN,
		    TB_T1_4_VAL_MAXLEN);
		vbuf[len] = '\0';
		td.records[i].vsize = (size_t)len;
		td.records[i].value = (char *)malloc((size_t)len + 1);
		TEST_ASSERT(td.records[i].value != NULL);
		memcpy(td.records[i].value, vbuf, (size_t)len + 1);
	}
	tb_t1_strset_destroy(&strset);

	fprintf(stderr,
	    "============ 2 put & dump %d groups with random values =========\n",
	    TB_T1_4_NGROUPS);
	for (g = 0; g < TB_T1_4_NGROUPS; g++) {
		fprintf(stderr, "--- group %d: put %lu records ---\n",
		    g, (unsigned long)cnt);
		for (i = 0; i < (int)cnt; i++) {
			len = getrandomstring2(vbuf, TB_T1_4_VAL_MINLEN,
			    TB_T1_4_VAL_MAXLEN);
			vbuf[len] = '\0';
			td.records[i].vsize = (size_t)len;
			memcpy(td.records[i].value, vbuf, (size_t)len + 1);
		}
		ret = populate_table(table, &td);
		CHK_RET(ret, "populate_table");
		ret = table->dump_inmem(table);
		CHK_RET(ret, "table->dump_inmem");
	}
	ret = table->start_threads(table);
	CHK_RET(ret, "table->start_threads");

	fprintf(stderr,
	    "============ 3 verify %lu records during merge =========\n",
	    (unsigned long)cnt);
	table->show_odt_items(table, stderr);
	for (v = 0; v < TB_T1_4_VERIFY_TIMES; v++) {
		if (v > 0) {
			fprintf(stderr,
			    "--- sleep %d seconds to give merge time ---\n",
			    TB_T1_4_VERIFY_INTERVAL);
			_van_sleep(TB_T1_4_VERIFY_INTERVAL, 0);
		}
		fprintf(stderr, "--- verify round %d/%d ---\n",
		    v + 1, TB_T1_4_VERIFY_TIMES);
		ret = verify_table_data(table, &td);
		CHK_RET_NOEXIT(ret, "verify_table_data");
		if (ret != 0)
			failcnt++;
		table->show_odt_items(table, stderr);
		/*
		 * A merge needs at least merge_min ondisk trees, and the
		 * background merge thread compacts odt_items (so that
		 * table->odt_top is exactly the number of live trees) at
		 * the end of every merge scan.  Once odt_top drops below
		 * merge_min there can be no further merge, so the data is
		 * stable and the verify loop can stop here.
		 */
		if (table->odt_top < table->merge_cfg.merge_min) {
			fprintf(stderr,
			    "--- odt_top %lu < merge_min %lu, "
			    "no more merge possible, stop verifying ---\n",
			    (unsigned long)table->odt_top,
			    (unsigned long)table->merge_cfg.merge_min);
			break;
		}
	}

	fprintf(stderr, "============ 4 close table and store =========\n");
	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	ret = verify_logfiles_from_scratch(".", "test1");
	CHK_RET(ret, "verify_logfiles_from_scratch");

	(void)free_string_testdata(&td);

	return ((failcnt > 0) ? (-1) : (0));
}

/*
 * table_t1_5*: a get of a key that is not in the table must report
 * VAN_NOTFOUND, whatever trees the table currently holds.
 *
 * The bug this guards against lived in _van_table_op_get(): once the
 * active and the frozen tree had both missed, it handed its 'ret' to
 * VAN_MUTEX_LOCK(), and the macro overwrote the VAN_NOTFOUND with the
 * result of pthread_mutex_lock().  A table that had no on-disk tree yet
 * then reported a missing key as a successful get with an empty value.
 *
 * The background threads stay off (VAN_TABLE_SKIP_THREAD_START), so every
 * tree transition below is the one the test asks for and the layout under
 * test is exactly the one the name of the case says.
 */
#define TB_T1_5_NKEYS	200
#define TB_T1_5_NAME	"test1_5"
#define TB_T1_5_MISSING	"no_such_key_was_ever_put_here"

typedef enum {
	TB_T1_5_ACTIVE = 0,		/* active only			*/
	TB_T1_5_ACTIVE_FROZEN,		/* active + frozen		*/
	TB_T1_5_ACTIVE_FROZEN_ODT,	/* active + frozen + ondisk	*/
	TB_T1_5_ACTIVE_ODT		/* active + ondisk		*/
} tb_t1_5_layout;

static int tb_t1_5_open(VAN_STORE **storep, VAN_TABLE **tablep) {
	VAN_STORE *store;
	VAN_TABLE *table;
	int ret;

	system("rm -fr test1_5*");
	system("rm -fr vanlog.*");

	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_test_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	/*
	 * Keep the threshold big so that nothing dumps on its own: the
	 * layout is built by the explicit dump_inmem / freeze calls.
	 */
	table->set_inmem_threshold(table, GIGABYTE);
	ret = table->open(table, TB_T1_5_NAME, VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");

	*storep = store;
	*tablep = table;

	return (0);
}

/*
 * Move the active tree to frozen *without* dumping it, which is what
 * leaves the table holding an active tree and a frozen one at once.
 *
 * _van_table_freeze() cannot be used for that: it also starts a dump,
 * which turns the frozen tree into an on-disk tree right away.
 */
static int tb_t1_5_freeze(VAN_TABLE *table) {
	INMEM_TREE *imtree;
	int ret;

	imtree = table->active;
	ret = imtree->freeze(imtree);
	CHK_RET(ret, "inmemtree->freeze");

	table->frozen = imtree;
	ret = van_inmemtree_create(table, &table->active);
	CHK_RET(ret, "van_inmemtree_create");
	table->active->threshold = table->inmem_threshold;

	return (0);
}

/* Put td->records[off .. off + n) into the table. */
static int tb_t1_5_put(VAN_TABLE *table, TESTDATA *td, size_t off,
    size_t n) {
	TESTDATA part;
	int ret;

	TESTDATA_SLICE(&part, td, off, n);
	ret = populate_table(table, &part);
	CHK_RET(ret, "populate_table");

	return (0);
}

/*
 * The key the missing-key check looks for must not be one of the
 * generated ones, otherwise it would ask for a key that is really there.
 */
static int tb_t1_5_absent(TESTDATA *td, const char *key, size_t klen) {
	size_t i;

	for (i = 0; i < td->count; i++)
		if (td->records[i].ksize == klen &&
		    memcmp(td->records[i].key, key, klen) == 0)
			return (0);

	return (1);
}

static void tb_t1_5_checkmissing(VAN_TABLE *table, TESTDATA *td) {
	VAN_DATUM key, val;
	int ret;

	TEST_ASSERT(tb_t1_5_absent(td, TB_T1_5_MISSING,
	    sizeof(TB_T1_5_MISSING)));

	memset(&key, 0, sizeof(key));
	key.data = (void *)TB_T1_5_MISSING;
	key.size = sizeof(TB_T1_5_MISSING);
	key.ulen = sizeof(TB_T1_5_MISSING);
	key.flags = 0;

	memset(&val, 0, sizeof(val));
	val.data = NULL;
	val.size = 0;
	val.ulen = 0;
	val.flags = VAN_MALLOC;

	ret = table->get(table, &key, &val, 0);
	if (val.data != NULL)
		(void)_van_datum_copyfree(&val);
	TEST_ASSERT2(ret == VAN_NOTFOUND,
	    "get of a missing key returned %d, expected VAN_NOTFOUND (%d)\n",
	    ret, VAN_NOTFOUND);
}

static int tb_t1_5_run(tb_t1_5_layout layout) {
	VAN_STORE *store;
	VAN_TABLE *table;
	TESTDATA td;
	size_t third;
	int ret;

	store = NULL;
	table = NULL;

	ret = tb_t1_5_open(&store, &table);
	CHK_RET(ret, "tb_t1_5_open");

	TESTDATA_INIT(&td, TB_T1_5_NKEYS);
	ret = generate_string_testdata(&td, 8, 20, 8, 20);
	CHK_RET(ret, "generate_string_testdata");

	third = TB_T1_5_NKEYS / 3;

	switch (layout) {
	case TB_T1_5_ACTIVE:
		/* Everything stays in the active tree. */
		ret = tb_t1_5_put(table, &td, 0, td.count);
		CHK_RET(ret, "put");
		break;
	case TB_T1_5_ACTIVE_FROZEN:
		ret = tb_t1_5_put(table, &td, 0, third);
		CHK_RET(ret, "put");
		ret = tb_t1_5_freeze(table);
		CHK_RET(ret, "freeze");
		ret = tb_t1_5_put(table, &td, third, td.count - third);
		CHK_RET(ret, "put");
		break;
	case TB_T1_5_ACTIVE_FROZEN_ODT:
		/*
		 * dump_inmem() freezes the *active* tree and dumps that,
		 * so the on-disk tree has to be built before the tree
		 * that is meant to stay frozen.
		 */
		ret = tb_t1_5_put(table, &td, 0, third);
		CHK_RET(ret, "put");
		ret = table->dump_inmem(table);
		CHK_RET(ret, "table->dump_inmem");
		ret = tb_t1_5_put(table, &td, third, third);
		CHK_RET(ret, "put");
		ret = tb_t1_5_freeze(table);
		CHK_RET(ret, "freeze");
		ret = tb_t1_5_put(table, &td, third * 2,
		    td.count - third * 2);
		CHK_RET(ret, "put");
		break;
	case TB_T1_5_ACTIVE_ODT:
		ret = tb_t1_5_put(table, &td, 0, third);
		CHK_RET(ret, "put");
		/* dump_inmem() sets table->frozen back to NULL. */
		ret = table->dump_inmem(table);
		CHK_RET(ret, "table->dump_inmem");
		ret = tb_t1_5_put(table, &td, third, td.count - third);
		CHK_RET(ret, "put");
		break;
	}

	fprintf(stderr, "--- layout: odt_top %lu, frozen %s ---\n",
	    (unsigned long)table->odt_top,
	    table->frozen != NULL ? "yes" : "no");

	tb_t1_5_checkmissing(table, &td);

	/* ... and every key that is there must still come back. */
	ret = verify_table_data(table, &td);
	CHK_RET(ret, "verify_table_data");

	(void)free_string_testdata(&td);

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	/* These cases use their own table name. */
	ret = verify_logfiles_from_scratch(".", TB_T1_5_NAME);
	CHK_RET(ret, "verify_logfiles_from_scratch");

	return (0);
}

int table_t1_5a() {
	return (tb_t1_5_run(TB_T1_5_ACTIVE));
}

int table_t1_5b() {
	return (tb_t1_5_run(TB_T1_5_ACTIVE_FROZEN));
}

int table_t1_5c() {
	return (tb_t1_5_run(TB_T1_5_ACTIVE_FROZEN_ODT));
}

int table_t1_5d() {
	return (tb_t1_5_run(TB_T1_5_ACTIVE_ODT));
}
