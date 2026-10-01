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
#include <pthread.h>
#include <unistd.h>

/*
 * Tests for the table level full scan, table->traverse().
 *
 * Cases:
 *   1. empty table traversal
 *   2. single tree, forward and backward order
 *   3. multi-source merge with overwrite (newest version wins)
 *   4. tombstone suppression (deleted key not reported)
 *   5. callback returning non zero stops the traversal
 *   6. dump triggered concurrently with a running traversal
 *   7. forward range scan, inclusive and exclusive ends
 *   8. backward range scan, boundaries handed over logically
 *   9. bounds that are not in the table, and opt == NULL regression
 *  10. ranges that cannot match anything return 0 without a callback
 *
 * Every case is independently runnable via table_tmain <case_name>.
 */

#define TB2_N 100000

/* ---------- small helpers ---------- */

static void vd_from_str(const char *s, VAN_DATUM *d) {
	memset(d, 0, sizeof(*d));
	d->data = (void *)s;
	d->size = strlen(s);
}

static int put_str(VAN_TABLE *t, const char *k, const char *v) {
	VAN_DATUM kd, vd;
	vd_from_str(k, &kd);
	vd_from_str(v, &vd);
	return (t->put(t, &kd, &vd, 0));
}

static int del_str(VAN_TABLE *t, const char *k) {
	VAN_DATUM kd;
	vd_from_str(k, &kd);
	return (t->del(t, &kd, 0));
}

static int tb2_setup(VAN_STORE **storep, VAN_TABLE **tablep,
    const char *name) {
	VAN_STORE *store;
	VAN_TABLE *table;
	int ret;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	/*
	 * Keep the inmem threshold huge so the scan thread never dumps
	 * on its own; dumps are done explicitly via table->dump_inmem.
	 */
	table->set_inmem_threshold(table, MEGABYTE);
	ret = table->open(table, name, 0);
	CHK_RET(ret, "table->open");
	*storep = store;
	*tablep = table;
	return (0);
}

static int tb2_teardown(VAN_TABLE *table, VAN_STORE *store) {
	int ret;

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");
	return (0);
}

/* ---------- collector ---------- */

typedef struct {
	char **keys;
	char **vals;
	int	cap;
	int	n;
	int	saw_key1;	/* set when "key1" is seen (used by case 4) */
} coll_t;

static int coll_cb(void *arg, const VAN_DATUM *k, const VAN_DATUM *v) {
	coll_t *c;
	char buf[128];

	c = (coll_t *)arg;
	TEST_ASSERT(c->n < c->cap);
	TEST_ASSERT(k->size < sizeof(buf));
	memcpy(buf, k->data, k->size);
	buf[k->size] = '\0';
	c->keys[c->n] = (char *)malloc(k->size + 1);
	TEST_ASSERT(c->keys[c->n] != NULL);
	memcpy(c->keys[c->n], buf, k->size + 1);
	if (v != NULL && v->data != NULL) {
		c->vals[c->n] = (char *)malloc(v->size + 1);
		TEST_ASSERT(c->vals[c->n] != NULL);
		memcpy(c->vals[c->n], v->data, v->size);
		c->vals[c->n][v->size] = '\0';
	} else {
		c->vals[c->n] = NULL;
	}
	if (strcmp(buf, "key1") == 0)
		c->saw_key1 = 1;
	c->n++;
	return (0);
}

static void coll_free(coll_t *c) {
	int i;

	for (i = 0; i < c->n; i++) {
		if (c->keys[i] != NULL)
			free(c->keys[i]);
		if (c->vals[i] != NULL)
			free(c->vals[i]);
	}
	c->n = 0;
}

/*
 * Find the value stored for key in a collected result set.
 * Returns the value string, or NULL if the key was not reported.
 */
static const char *coll_find(coll_t *c, const char *key) {
	int i;

	for (i = 0; i < c->n; i++) {
		if (strcmp(c->keys[i], key) == 0)
			return (c->vals[i]);
	}
	return (NULL);
}

/* ---------- case 1: empty table ---------- */

static int t2_empty_cb(void *arg, const VAN_DATUM *k, const VAN_DATUM *v) {
	int *called = (int *)arg;
	(*called)++;
	return (0);
}

int table_t2_1() {
	VAN_STORE *store;
	VAN_TABLE *table;
	int ret, called;

	ret = tb2_setup(&store, &table, "test1");
	CHK_RET(ret, "setup");

	/* No records inserted at all. */
	called = 0;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, NULL, t2_empty_cb, &called);
	CHK_RET(ret != 0, "traverse must return 0 on empty table");
	TEST_ASSERT2(called == 0, "callback must not be called on empty table");

	ret = tb2_teardown(table, store);
	CHK_RET(ret, "teardown");
	return (0);
}

/* ---------- case 2: single tree, forward/backward ---------- */

int table_t2_2() {
	VAN_STORE *store;
	VAN_TABLE *table;
	coll_t c, c2;
	char buf[32];
	int ret, i, N;

	N = TB2_N;
	ret = tb2_setup(&store, &table, "test1");
	CHK_RET(ret, "setup");

	for (i = 0; i < N; i++) {
		(void)snprintf(buf, sizeof(buf), "key%05d", i);
		ret = put_str(table, buf, buf);
		CHK_RET(ret, "put");
	}

	memset(&c, 0, sizeof(c));
	c.keys = (char **)malloc(N * sizeof(char *));
	c.vals = (char **)malloc(N * sizeof(char *));
	c.cap = N;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, NULL, coll_cb, &c);
	CHK_RET(ret != 0, "forward traverse must return 0");
	TEST_ASSERT2(c.n == N, "forward: expected %d keys, got %d", N, c.n);
	for (i = 1; i < N; i++) {
		TEST_ASSERT2(strcmp(c.keys[i - 1], c.keys[i]) < 0,
		    "forward order broken at %d: %s >= %s", i,
		    c.keys[i - 1], c.keys[i]);
	}

	memset(&c2, 0, sizeof(c2));
	c2.keys = (char **)malloc(N * sizeof(char *));
	c2.vals = (char **)malloc(N * sizeof(char *));
	c2.cap = N;
	ret = table->traverse(table, VAN_TRAVERSE_BACKWARD, NULL, coll_cb, &c2);
	CHK_RET(ret != 0, "backward traverse must return 0");
	TEST_ASSERT2(c2.n == N, "backward: expected %d keys, got %d", N, c2.n);
	for (i = 1; i < N; i++) {
		TEST_ASSERT2(strcmp(c2.keys[i - 1], c2.keys[i]) > 0,
		    "backward order broken at %d: %s <= %s", i,
		    c2.keys[i - 1], c2.keys[i]);
	}

	coll_free(&c);
	coll_free(&c2);
	ret = tb2_teardown(table, store);
	CHK_RET(ret, "teardown");
	return (0);
}

/* ---------- case 3: multi-source merge with overwrite ---------- */

int table_t2_3() {
	VAN_STORE *store;
	VAN_TABLE *table;
	coll_t c;
	const char *val;
	int ret;

	ret = tb2_setup(&store, &table, "test1");
	CHK_RET(ret, "setup");

	/* Put key1/key2 into the active tree, then dump to ondisk. */
	ret = put_str(table, "key1", "a");
	CHK_RET(ret, "put key1");
	ret = put_str(table, "key2", "a2");
	CHK_RET(ret, "put key2");
	ret = table->dump_inmem(table);
	CHK_RET(ret, "dump_inmem");

	/*
	 * Now override key1 with a newer value in the active tree and add
	 * key3 (active only).  The merge must report key1 once with 'b'.
	 */
	ret = put_str(table, "key1", "b");
	CHK_RET(ret, "put key1 (override)");
	ret = put_str(table, "key3", "c");
	CHK_RET(ret, "put key3");

	memset(&c, 0, sizeof(c));
	c.keys = (char **)malloc(8 * sizeof(char *));
	c.vals = (char **)malloc(8 * sizeof(char *));
	c.cap = 8;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, NULL, coll_cb, &c);
	CHK_RET(ret != 0, "traverse must return 0");
	TEST_ASSERT2(c.n == 3, "expected 3 keys after merge, got %d", c.n);

	val = coll_find(&c, "key1");
	TEST_ASSERT2(val != NULL, "key1 missing from merge result");
	TEST_ASSERT2(val != NULL && strcmp(val, "b") == 0,
	    "key1 must be newest value 'b', got '%s'", val);
	val = coll_find(&c, "key2");
	TEST_ASSERT2(val != NULL && strcmp(val, "a2") == 0,
	    "key2 must keep ondisk value 'a2', got '%s'", val);
	val = coll_find(&c, "key3");
	TEST_ASSERT2(val != NULL && strcmp(val, "c") == 0,
	    "key3 must be active value 'c', got '%s'", val);

	coll_free(&c);
	ret = tb2_teardown(table, store);
	CHK_RET(ret, "teardown");
	return (0);
}

/* ---------- case 4: tombstone suppression ---------- */

int table_t2_4() {
	VAN_STORE *store;
	VAN_TABLE *table;
	coll_t c;
	const char *val;
	int ret;

	ret = tb2_setup(&store, &table, "test1");
	CHK_RET(ret, "setup");

	ret = put_str(table, "key1", "a");
	CHK_RET(ret, "put key1");
	ret = del_str(table, "key1");
	CHK_RET(ret, "del key1");
	ret = table->dump_inmem(table);
	CHK_RET(ret, "dump_inmem (tombstone)");

	/* A live record in the active tree to show traversal still works. */
	ret = put_str(table, "key2", "b");
	CHK_RET(ret, "put key2");

	memset(&c, 0, sizeof(c));
	c.keys = (char **)malloc(8 * sizeof(char *));
	c.vals = (char **)malloc(8 * sizeof(char *));
	c.cap = 8;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, NULL, coll_cb, &c);
	CHK_RET(ret != 0, "traverse must return 0");
	TEST_ASSERT2(c.saw_key1 == 0,
	    "tombstoned key1 must not be reported");
	TEST_ASSERT2(c.n == 1, "expected exactly 1 live key, got %d", c.n);
	TEST_ASSERT2(strcmp(c.keys[0], "key2") == 0,
	    "only key2 should remain, got '%s'", c.keys[0]);
	val = coll_find(&c, "key2");
	TEST_ASSERT2(val != NULL && strcmp(val, "b") == 0,
	    "key2 must be 'b', got '%s'", val);

	coll_free(&c);
	ret = tb2_teardown(table, store);
	CHK_RET(ret, "teardown");
	return (0);
}

/* ---------- case 5: callback non-zero stops traversal ---------- */

typedef struct {
	int	n;
	int	stop_at;
	int	stop_ret;
} t5_ctx_t;

static int t5_cb(void *arg, const VAN_DATUM *k, const VAN_DATUM *v) {
	t5_ctx_t *ctx = (t5_ctx_t *)arg;

	ctx->n++;
	if (ctx->n == ctx->stop_at)
		return (ctx->stop_ret);
	return (0);
}

int table_t2_5() {
	VAN_STORE *store;
	VAN_TABLE *table;
	t5_ctx_t ctx;
	char buf[32];
	int ret, i, N, K;

	N = TB2_N;
	K = 500;
	ret = tb2_setup(&store, &table, "test1");
	CHK_RET(ret, "setup");

	for (i = 0; i < N; i++) {
		(void)snprintf(buf, sizeof(buf), "key%05d", i);
		ret = put_str(table, buf, buf);
		CHK_RET(ret, "put");
	}

	ctx.n = 0;
	ctx.stop_at = K;
	ctx.stop_ret = 42;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, NULL, t5_cb, &ctx);
	TEST_ASSERT2(ret == 42, "traverse must return callback value 42, got %d",
	    ret);
	TEST_ASSERT2(ctx.n == K,
	    "callback must be invoked exactly %d times, got %d", K, ctx.n);

	ret = tb2_teardown(table, store);
	CHK_RET(ret, "teardown");
	return (0);
}

/* ---------- case 6: dump during traversal ---------- */

typedef struct {
	VAN_TABLE *table;
	int	stop;
} t6_ctx_t;

static void *t6_dump_thread(void *arg) {
	t6_ctx_t *ctx = (t6_ctx_t *)arg;
	int ret;

	while (!ctx->stop) {
		/*
		 * Force a freeze+dump of the current active tree.  This
		 * exercises the hybrid isolation contract: trees created
		 * or destroyed while the scan runs must not corrupt the
		 * snapshot the traverse already holds.  Data used for the
		 * assertions lives in ondisk trees, which are refcount
		 * protected, so the scan keeps reading them safely.
		 */
		ret = ctx->table->dump_inmem(ctx->table);
		if (ret != 0)
			break;
		usleep(500);
	}
	return (NULL);
}

int table_t2_6() {
	VAN_STORE *store;
	VAN_TABLE *table;
	coll_t c;
	t6_ctx_t bctx;
	pthread_t th;
	char kbuf[32], vbuf[32];
	const char *val;
	int ret, i, N;

	N = TB2_N;
	ret = tb2_setup(&store, &table, "test1");
	CHK_RET(ret, "setup");

	for (i = 0; i < N; i++) {
		(void)snprintf(kbuf, sizeof(kbuf), "key%05d", i);
		(void)snprintf(vbuf, sizeof(vbuf), "val%05d", i);
		ret = put_str(table, kbuf, vbuf);
		CHK_RET(ret, "put");
	}

	/* Move everything into ondisk trees; active is now empty. */
	ret = table->dump_inmem(table);
	CHK_RET(ret, "dump_inmem");

	bctx.table = table;
	bctx.stop = 0;
	ret = pthread_create(&th, NULL, t6_dump_thread, &bctx);
	CHK_RET(ret, "pthread_create");

	memset(&c, 0, sizeof(c));
	c.keys = (char **)malloc(N * sizeof(char *));
	c.vals = (char **)malloc(N * sizeof(char *));
	c.cap = N;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, NULL, coll_cb, &c);
	bctx.stop = 1;
	(void)pthread_join(th, NULL);

	CHK_RET(ret != 0, "traverse during dump must return 0");
	TEST_ASSERT2(c.n == N,
	    "traverse during dump: expected %d keys, got %d", N, c.n);
	for (i = 0; i < N; i++) {
		(void)snprintf(kbuf, sizeof(kbuf), "key%05d", i);
		(void)snprintf(vbuf, sizeof(vbuf), "val%05d", i);
		val = coll_find(&c, kbuf);
		TEST_ASSERT2(val != NULL && strcmp(val, vbuf) == 0,
		    "key %s missing or wrong value during concurrent dump", kbuf);
	}

	coll_free(&c);
	ret = tb2_teardown(table, store);
	CHK_RET(ret, "teardown");
	return (0);
}

/* ---------- range scan helpers ---------- */

/*
 * "key%05d" for i in [0, N), split over two sources.
 *
 * The whole set is dumped into an ondisk tree and one key is written again
 * afterwards, so the active tree overlaps the ranges below and every scan
 * is really a multi-source merge rather than a single bounded cursor.
 *
 * With holes != 0 the keys key00100 and key00200 are left out, which is
 * how case 9 checks boundaries that do not exist.
 */
static int tb2_range_setup(VAN_STORE **storep, VAN_TABLE **tablep,
    int holes) {
	VAN_STORE *store;
	VAN_TABLE *table;
	char buf[32];
	int ret, i, N;

	N = TB2_N;
	ret = tb2_setup(&store, &table, "test1");
	CHK_RET(ret, "setup");

	for (i = 0; i < N; i++) {
		if (holes && (i == 100 || i == 200))
			continue;
		(void)snprintf(buf, sizeof(buf), "key%05d", i);
		ret = put_str(table, buf, buf);
		CHK_RET(ret, "put");
	}

	ret = table->dump_inmem(table);
	CHK_RET(ret, "dump_inmem");
	ret = put_str(table, "key00150", "live");
	CHK_RET(ret, "put key00150");

	*storep = store;
	*tablep = table;
	return (0);
}

static void rcoll_alloc(coll_t *c, int cap) {
	memset(c, 0, sizeof(*c));
	c->keys = (char **)malloc(cap * sizeof(char *));
	c->vals = (char **)malloc(cap * sizeof(char *));
	TEST_ASSERT(c->keys != NULL);
	TEST_ASSERT(c->vals != NULL);
	c->cap = cap;
}

static void rcoll_release(coll_t *c) {
	coll_free(c);
	if (c->keys != NULL)
		free(c->keys);
	if (c->vals != NULL)
		free(c->vals);
	c->keys = NULL;
	c->vals = NULL;
}

/*
 * The result has to be exactly the keys of [lo, hi], arriving in ascending
 * order for a forward scan and in descending order for a backward one.
 */
static void chk_seq(coll_t *c, int lo, int hi, int backward,
    const char *what) {
	char buf[32];
	int expect, i, idx, cmp;

	expect = hi - lo + 1;
	TEST_ASSERT2(c->n == expect, "%s: expected %d keys, got %d",
	    what, expect, c->n);
	for (i = 0; i < c->n; i++) {
		idx = backward ? (hi - i) : (lo + i);
		(void)snprintf(buf, sizeof(buf), "key%05d", idx);
		TEST_ASSERT2(strcmp(c->keys[i], buf) == 0,
		    "%s: position %d expected %s, got %s",
		    what, i, buf, c->keys[i]);
		if (i > 0) {
			cmp = strcmp(c->keys[i - 1], c->keys[i]);
			TEST_ASSERT2(backward ? (cmp > 0) : (cmp < 0),
			    "%s: order broken at %d: %s vs %s", what, i,
			    c->keys[i - 1], c->keys[i]);
		}
	}
}

/* ---------- case 7: forward range scan ---------- */

int table_t2_7() {
	VAN_STORE *store;
	VAN_TABLE *table;
	coll_t c;
	VAN_DATUM sk, ek;
	VAN_TRAVERSE_OPT opt;
	const char *val;
	int ret;

	ret = tb2_range_setup(&store, &table, 0);
	CHK_RET(ret, "setup");

	vd_from_str("key00100", &sk);
	vd_from_str("key00200", &ek);

	/* [100, 200): inclusive start, exclusive end. */
	rcoll_alloc(&c, TB2_N + 16);
	opt.start_key = &sk;
	opt.end_key = &ek;
	opt.start_inclusive = 1;
	opt.end_inclusive = 0;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "[100,200) forward must return 0");
	chk_seq(&c, 100, 199, 0, "[100,200)");
	rcoll_release(&c);

	/* [100, 200]: both ends inclusive, key00200 joins in. */
	rcoll_alloc(&c, TB2_N + 16);
	opt.end_inclusive = 1;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "[100,200] forward must return 0");
	chk_seq(&c, 100, 200, 0, "[100,200]");
	rcoll_release(&c);

	/* (100, 200): neither boundary belongs to the result. */
	rcoll_alloc(&c, TB2_N + 16);
	opt.start_inclusive = 0;
	opt.end_inclusive = 0;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "(100,200) forward must return 0");
	chk_seq(&c, 101, 199, 0, "(100,200)");
	/*
	 * key00150 sits in the ondisk tree and again in the active one;
	 * inside a range the newest version wins, just as in a full scan.
	 */
	val = coll_find(&c, "key00150");
	TEST_ASSERT2(val != NULL && strcmp(val, "live") == 0,
	    "(100,200): key00150 must be the active value 'live', got '%s'",
	    val);
	rcoll_release(&c);

	/* No end key: from the start key to the last record. */
	rcoll_alloc(&c, TB2_N + 16);
	opt.start_key = &sk;
	opt.start_inclusive = 1;
	opt.end_key = NULL;
	opt.end_inclusive = 0;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "[100,end) forward must return 0");
	chk_seq(&c, 100, TB2_N - 1, 0, "[100,end)");
	rcoll_release(&c);

	/* No start key: everything before the end key. */
	rcoll_alloc(&c, TB2_N + 16);
	opt.start_key = NULL;
	opt.start_inclusive = 0;
	opt.end_key = &ek;
	opt.end_inclusive = 0;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "[begin,200) forward must return 0");
	chk_seq(&c, 0, 199, 0, "[begin,200)");
	rcoll_release(&c);

	ret = tb2_teardown(table, store);
	CHK_RET(ret, "teardown");
	return (0);
}

/* ---------- case 8: backward range scan ---------- */

int table_t2_8() {
	VAN_STORE *store;
	VAN_TABLE *table;
	coll_t c;
	VAN_DATUM sk, ek;
	VAN_TRAVERSE_OPT opt;
	int ret;

	ret = tb2_range_setup(&store, &table, 0);
	CHK_RET(ret, "setup");

	/*
	 * The boundaries stay logical ones: (200, 100] read backwards is
	 * start_key = the high key 200, end_key = the low key 100.
	 */
	vd_from_str("key00200", &sk);
	vd_from_str("key00100", &ek);

	rcoll_alloc(&c, TB2_N + 16);
	opt.start_key = &sk;
	opt.end_key = &ek;
	opt.start_inclusive = 0;
	opt.end_inclusive = 1;
	ret = table->traverse(table, VAN_TRAVERSE_BACKWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "(200,100] backward must return 0");
	chk_seq(&c, 100, 199, 1, "(200,100]");
	rcoll_release(&c);

	/* [200, 100]: both ends inclusive. */
	rcoll_alloc(&c, TB2_N + 16);
	opt.start_inclusive = 1;
	opt.end_inclusive = 1;
	ret = table->traverse(table, VAN_TRAVERSE_BACKWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "[200,100] backward must return 0");
	chk_seq(&c, 100, 200, 1, "[200,100]");
	rcoll_release(&c);

	/* (200, 100): neither end included. */
	rcoll_alloc(&c, TB2_N + 16);
	opt.start_inclusive = 0;
	opt.end_inclusive = 0;
	ret = table->traverse(table, VAN_TRAVERSE_BACKWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "(200,100) backward must return 0");
	chk_seq(&c, 101, 199, 1, "(200,100)");
	rcoll_release(&c);

	/* No end key: from the start key down to the first record. */
	rcoll_alloc(&c, TB2_N + 16);
	opt.start_key = &sk;
	opt.start_inclusive = 0;
	opt.end_key = NULL;
	opt.end_inclusive = 0;
	ret = table->traverse(table, VAN_TRAVERSE_BACKWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "(200,begin] backward must return 0");
	chk_seq(&c, 0, 199, 1, "(200,begin]");
	rcoll_release(&c);

	/* No start key: from the last record down to the end key. */
	rcoll_alloc(&c, TB2_N + 16);
	opt.start_key = NULL;
	opt.start_inclusive = 0;
	opt.end_key = &ek;
	opt.end_inclusive = 1;
	ret = table->traverse(table, VAN_TRAVERSE_BACKWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "(end,100] backward must return 0");
	chk_seq(&c, 100, TB2_N - 1, 1, "(end,100]");
	rcoll_release(&c);

	ret = tb2_teardown(table, store);
	CHK_RET(ret, "teardown");
	return (0);
}

/* ---------- case 9: absent boundaries, opt == NULL regression ---------- */

int table_t2_9() {
	VAN_STORE *store;
	VAN_TABLE *table;
	coll_t c;
	VAN_DATUM sk, ek;
	VAN_TRAVERSE_OPT opt;
	char lastk[32];
	int ret, missing, expect;

	/* key00100 and key00200 are absent from this data set. */
	ret = tb2_range_setup(&store, &table, 1);
	CHK_RET(ret, "setup");

	missing = TB2_N - 2;
	/* The highest key of the data set, "key%05d" of the last record. */
	(void)snprintf(lastk, sizeof(lastk), "key%05d", TB2_N - 1);

	vd_from_str("key00100", &sk);
	vd_from_str("key00200", &ek);

	/*
	 * A start key that does not exist moves on to the next key there
	 * is, an end key that does not exist lets everything below it in.
	 */
	rcoll_alloc(&c, TB2_N + 16);
	opt.start_key = &sk;
	opt.end_key = &ek;
	opt.start_inclusive = 1;
	opt.end_inclusive = 0;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "absent boundaries forward must return 0");
	chk_seq(&c, 101, 199, 0, "absent boundaries forward");
	rcoll_release(&c);

	/* The same range read backwards. */
	rcoll_alloc(&c, TB2_N + 16);
	vd_from_str("key00200", &sk);
	vd_from_str("key00100", &ek);
	opt.start_key = &sk;
	opt.end_key = &ek;
	opt.start_inclusive = 1;
	opt.end_inclusive = 1;
	ret = table->traverse(table, VAN_TRAVERSE_BACKWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "absent boundaries backward must return 0");
	chk_seq(&c, 101, 199, 1, "absent boundaries backward");
	rcoll_release(&c);

	/*
	 * A start key past every record selects nothing, and is not a
	 * bug.  It has to sort above the highest key of the data set,
	 * which is why it is not a "key%05d" string at all.
	 */
	rcoll_alloc(&c, TB2_N + 16);
	vd_from_str("zzzzzz", &sk);
	opt.start_key = &sk;
	opt.start_inclusive = 1;
	opt.end_key = NULL;
	opt.end_inclusive = 0;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "start past every record must return 0");
	TEST_ASSERT2(c.n == 0, "start past every record: expected 0, got %d",
	    c.n);
	rcoll_release(&c);

	/*
	 * An end key past every record is simply an open range.  This data
	 * set also misses key00200, so [101, last] is not contiguous and
	 * cannot be checked with chk_seq().
	 */
	rcoll_alloc(&c, TB2_N + 16);
	vd_from_str("key00100", &sk);
	vd_from_str("zzzzzzzz", &ek);
	opt.start_key = &sk;
	opt.start_inclusive = 1;
	opt.end_key = &ek;
	opt.end_inclusive = 0;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt, coll_cb, &c);
	CHK_RET(ret != 0, "open end must return 0");
	expect = (TB2_N - 1 - 101 + 1) - 1;	/* [101, last] less key00200 */
	TEST_ASSERT2(c.n == expect, "open end: expected %d keys, got %d",
	    expect, c.n);
	TEST_ASSERT2(strcmp(c.keys[0], "key00101") == 0,
	    "open end must start at key00101, got '%s'", c.keys[0]);
	TEST_ASSERT2(strcmp(c.keys[c.n - 1], lastk) == 0,
	    "open end must end at %s, got '%s'", lastk, c.keys[c.n - 1]);
	TEST_ASSERT2(coll_find(&c, "key00200") == NULL,
	    "open end must skip the absent key00200");
	rcoll_release(&c);

	/*
	 * opt == NULL has to behave exactly as it always did: every key,
	 * holes of this data set included.
	 */
	rcoll_alloc(&c, TB2_N + 16);
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, NULL, coll_cb, &c);
	CHK_RET(ret != 0, "full scan must return 0");
	TEST_ASSERT2(c.n == missing,
	    "full scan: expected %d keys, got %d", missing, c.n);
	TEST_ASSERT2(strcmp(c.keys[0], "key00000") == 0,
	    "full scan must start at key00000, got '%s'", c.keys[0]);
	TEST_ASSERT2(strcmp(c.keys[c.n - 1], lastk) == 0,
	    "full scan must end at %s, got '%s'", lastk, c.keys[c.n - 1]);
	rcoll_release(&c);

	rcoll_alloc(&c, TB2_N + 16);
	ret = table->traverse(table, VAN_TRAVERSE_BACKWARD, NULL, coll_cb, &c);
	CHK_RET(ret != 0, "backward full scan must return 0");
	TEST_ASSERT2(c.n == missing,
	    "backward full scan: expected %d keys, got %d", missing, c.n);
	TEST_ASSERT2(strcmp(c.keys[0], lastk) == 0,
	    "backward full scan must start at %s, got '%s'", lastk,
	    c.keys[0]);
	TEST_ASSERT2(strcmp(c.keys[c.n - 1], "key00000") == 0,
	    "backward full scan must end at key00000, got '%s'",
	    c.keys[c.n - 1]);
	rcoll_release(&c);

	ret = tb2_teardown(table, store);
	CHK_RET(ret, "teardown");
	return (0);
}

/* ---------- case 10: ranges that cannot match anything ---------- */

int table_t2_10() {
	VAN_STORE *store;
	VAN_TABLE *table;
	VAN_DATUM sk, ek;
	VAN_TRAVERSE_OPT opt;
	int ret, called;

	ret = tb2_range_setup(&store, &table, 0);
	CHK_RET(ret, "setup");

	vd_from_str("key00300", &sk);
	vd_from_str("key00100", &ek);

	/* Forward: start beyond end. */
	called = 0;
	opt.start_key = &sk;
	opt.end_key = &ek;
	opt.start_inclusive = 1;
	opt.end_inclusive = 1;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt,
	    t2_empty_cb, &called);
	CHK_RET(ret != 0, "empty forward range must return 0");
	TEST_ASSERT2(called == 0,
	    "empty forward range must not call the callback");

	/*
	 * The boundaries are handed over the same logical way backwards,
	 * so the empty range there is the mirrored one: start below end.
	 */
	called = 0;
	vd_from_str("key00100", &sk);
	vd_from_str("key00300", &ek);
	opt.start_key = &sk;
	opt.end_key = &ek;
	opt.start_inclusive = 1;
	opt.end_inclusive = 1;
	ret = table->traverse(table, VAN_TRAVERSE_BACKWARD, &opt,
	    t2_empty_cb, &called);
	CHK_RET(ret != 0, "empty backward range must return 0");
	TEST_ASSERT2(called == 0,
	    "empty backward range must not call the callback");

	/* Same key on both sides stays empty while either side excludes. */
	vd_from_str("key00100", &sk);
	vd_from_str("key00100", &ek);
	opt.start_key = &sk;
	opt.end_key = &ek;

	called = 0;
	opt.start_inclusive = 0;
	opt.end_inclusive = 1;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt,
	    t2_empty_cb, &called);
	CHK_RET(ret != 0, "(100,100] must return 0");
	TEST_ASSERT2(called == 0, "(100,100] must not call the callback");

	called = 0;
	opt.start_inclusive = 1;
	opt.end_inclusive = 0;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt,
	    t2_empty_cb, &called);
	CHK_RET(ret != 0, "[100,100) must return 0");
	TEST_ASSERT2(called == 0, "[100,100) must not call the callback");

	/* ...but it does select that one key once both sides include it. */
	called = 0;
	opt.start_inclusive = 1;
	opt.end_inclusive = 1;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, &opt,
	    t2_empty_cb, &called);
	CHK_RET(ret != 0, "[100,100] must return 0");
	TEST_ASSERT2(called == 1,
	    "[100,100] must report exactly one key, called %d times", called);

	ret = tb2_teardown(table, store);
	CHK_RET(ret, "teardown");
	return (0);
}
