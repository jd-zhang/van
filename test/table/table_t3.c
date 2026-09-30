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
 * Tests for table->traverse() over randomly generated data.
 *
 * table_t2.c drives the scan with hand made keys; this file asks the
 * same questions with random key/value strings, where neither the
 * content nor the order of the records is known in advance:
 *
 *   table_t3_1a  static data, forward full scan
 *   table_t3_1b  static data, backward full scan
 *   table_t3_1c..1f  static data, forward range, all four open/closed
 *   table_t3_1g..1j  static data, backward range, all four open/closed
 *   table_t3_2a..2j  the same ten scans against a table that a
 *		    background thread keeps populating
 *
 * Everything is compared as sets of key+value pairs after sorting, so
 * the tests never depend on the random order the data was generated
 * in.  Every case is independently runnable via table_tmain <case>.
 */

#define TB3_N 100000

/* ---------- setup / teardown (same pattern as table_t2.c) ---------- */

static int tb3_setup(VAN_STORE **storep, VAN_TABLE **tablep,
    const char *name) {
	VAN_STORE *store;
	VAN_TABLE *table;
	int ret;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");
	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	/*
	 * Keep the inmem threshold huge so the scan thread never dumps
	 * on its own; the tests here want a single stable data source.
	 */
	table->set_inmem_threshold(table, GIGABYTE);
	ret = table->open(table, name, VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");
	*storep = store;
	*tablep = table;
	return (0);
}

static int tb3_teardown(VAN_TABLE *table, VAN_STORE *store) {
	int ret;

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	ret = store->close(store);
	CHK_RET(ret, "store->close");
	return (0);
}

/* ---------- collector: a growable array of copied records ---------- */

typedef struct {
	RECORD *recs;
	size_t	cap;
	size_t	n;
} coll_t;

static int coll_cb(void *arg, const VAN_DATUM *k, const VAN_DATUM *v) {
	coll_t *c;
	RECORD *r;

	c = (coll_t *)arg;
	if (c->n == c->cap) {
		c->cap = c->cap == 0 ? 64 : c->cap * 2;
		c->recs = (RECORD *)realloc(c->recs,
		    c->cap * sizeof(RECORD));
		TEST_ASSERT(c->recs != NULL);
	}
	r = &c->recs[c->n++];
	memset(r, 0, sizeof(*r));
	r->ksize = k->size;
	r->key = (char *)malloc(k->size);
	TEST_ASSERT(r->key != NULL);
	memcpy(r->key, k->data, k->size);
	if (v != NULL && v->data != NULL) {
		r->vsize = v->size;
		r->value = (char *)malloc(v->size);
		TEST_ASSERT(r->value != NULL);
		memcpy(r->value, v->data, v->size);
	}
	return (0);
}

static void coll_free(coll_t *c) {
	size_t i;

	for (i = 0; i < c->n; i++) {
		free(c->recs[i].key);
		free(c->recs[i].value);
	}
	free(c->recs);
	memset(c, 0, sizeof(*c));
}

/* ---------- record comparison ---------- */

static int rec_keycmp(const RECORD *a, const RECORD *b) {
	VAN_DATUM k1, k2;
	int cmp;

	memset(&k1, 0, sizeof(k1));
	memset(&k2, 0, sizeof(k2));
	k1.data = a->key;
	k1.size = a->ksize;
	k2.data = b->key;
	k2.size = b->ksize;
	cmp = van_datum_defcmp(&k1, &k2);
	if (cmp != 0)
		return (cmp);
	/*
	 * Same key: order by value, so that sorting is a total order
	 * over key+value pairs.  A traverse reports one value per key,
	 * so ties only happen inside copies of mixed data sets.
	 */
	k1.data = a->value;
	k1.size = a->vsize;
	k2.data = b->value;
	k2.size = b->vsize;
	return (van_datum_defcmp(&k1, &k2));
}

static int rec_qsort_cmp(const void *pa, const void *pb) {
	return rec_keycmp((const RECORD *)pa, (const RECORD *)pb);
}

static void coll_sort(coll_t *c) {
	qsort(c->recs, c->n, sizeof(RECORD), rec_qsort_cmp);
}

/* Non zero iff two records are the same key AND the same value. */
static int rec_full_equal(const RECORD *a, const RECORD *b) {
	if (a->ksize != b->ksize || a->vsize != b->vsize)
		return (0);
	return memcmp(a->key, b->key, a->ksize) == 0 &&
	    memcmp(a->value, b->value, a->vsize) == 0;
}

/* ---------- test data set helpers ---------- */

static void record_deep_copy(RECORD *dst, const RECORD *src) {
	*dst = *src;
	dst->key = (char *)malloc(src->ksize);
	TEST_ASSERT(dst->key != NULL);
	memcpy(dst->key, src->key, src->ksize);
	dst->value = (char *)malloc(src->vsize);
	TEST_ASSERT(dst->value != NULL);
	memcpy(dst->value, src->value, src->vsize);
}

/*
 * A deep copy: sort_testdata() sorts in place, and the type 2 tests
 * need the populate order of their records preserved.
 */
static void testdata_copy(TESTDATA *dst, const TESTDATA *src) {
	size_t i;

	TESTDATA_INIT(dst, src->count);
	for (i = 0; i < src->count; i++)
		record_deep_copy(&dst->records[i], &src->records[i]);
}

/* The deep copy of two data sets taken one after the other. */
static void testdata_copy2(TESTDATA *dst, const TESTDATA *a,
    const TESTDATA *b) {
	size_t i, n;

	TESTDATA_INIT(dst, a->count + b->count);
	n = 0;
	for (i = 0; i < a->count; i++)
		record_deep_copy(&dst->records[n++], &a->records[i]);
	for (i = 0; i < b->count; i++)
		record_deep_copy(&dst->records[n++], &b->records[i]);
}

/*
 * Free the record array only; the key/value buffers belong to the data
 * set a shallow view was taken from.
 */
static void testdata_free_shallow(TESTDATA *td) {
	free(td->records);
	td->records = NULL;
	td->count = 0;
}

/*
 * Is small a subset of big?  Both must be sorted ascending by
 * (key, value).  Returns 0 on success, else 1 + the position of the
 * first record of small that is missing from big (or has a different
 * value there).
 */
static size_t testdata_is_subset(TESTDATA *small, TESTDATA *big) {
	size_t i, j;
	int cmp;

	i = 0;
	j = 0;
	while (i < small->count) {
		if (j == big->count)
			return (i + 1);
		cmp = rec_keycmp(&small->records[i], &big->records[j]);
		if (cmp < 0)
			return (i + 1);
		if (cmp > 0) {
			j++;
			continue;
		}
		/* Same key: the value has to match as well. */
		if (!rec_full_equal(&small->records[i], &big->records[j]))
			return (i + 1);
		i++;
		j++;
	}
	return (0);
}

/*
 * The records of src (sorted ascending by key) whose key falls into
 * [lo, hi], with the ends kept or dropped as asked.  The result is a
 * new record array whose entries alias the buffers of src, so only
 * testdata_free_shallow() may be called on it, and src itself keeps
 * being freed by free_string_testdata().
 */
static void testdata_intersect_range(TESTDATA *dst, TESTDATA *src,
    const VAN_DATUM *lo, int lo_incl, const VAN_DATUM *hi, int hi_incl) {
	VAN_DATUM k;
	RECORD *recs;
	size_t i, n;

	recs = (RECORD *)malloc(src->count * sizeof(RECORD));
	TEST_ASSERT(recs != NULL);
	memset(&k, 0, sizeof(k));
	n = 0;
	for (i = 0; i < src->count; i++) {
		k.data = src->records[i].key;
		k.size = src->records[i].ksize;
		if (lo != NULL) {
			int cmp = van_datum_defcmp(&k, lo);
			if (cmp < 0 || (cmp == 0 && !lo_incl))
				continue;
		}
		if (hi != NULL) {
			int cmp = van_datum_defcmp(&k, hi);
			if (cmp > 0 || (cmp == 0 && !hi_incl))
				continue;
		}
		recs[n++] = src->records[i];
	}
	dst->records = recs;
	dst->count = n;
}

/*
 * Pick two records of a sorted data set as the anchors of a range: the
 * low one from the first quarter, the high one from the last quarter,
 * so the range is wide but never the whole set.  Both anchors are real
 * records, and lo < hi is checked.
 */
static void pick_anchors(TESTDATA *sorted, RECORD *lorec, RECORD *hirec) {
	size_t i, j;
	int cmp;

	i = (size_t)(rand() % (int)(sorted->count / 4));
	j = sorted->count - 1 - (size_t)(rand() % (int)(sorted->count / 4));
	TEST_ASSERT2(i < j, "anchor indices out of order: %lu, %lu",
	    (unsigned long)i, (unsigned long)j);
	cmp = rec_keycmp(&sorted->records[i], &sorted->records[j]);
	TEST_ASSERT2(cmp < 0, "anchor keys not strictly ascending");
	*lorec = sorted->records[i];
	*hirec = sorted->records[j];
}

static void rec_key_datum(const RECORD *r, VAN_DATUM *d) {
	memset(d, 0, sizeof(*d));
	d->data = r->key;
	d->size = r->ksize;
}

/* ---------- exact order comparison (type 1) ---------- */

/*
 * The collected scan of a static table must be exactly the expected
 * records: the same count, and in ascending order for a forward scan,
 * descending for a backward one.
 */
static void cmp_exact(coll_t *got, TESTDATA *expect, int backward,
    const char *what) {
	size_t i, gi;

	TEST_ASSERT2(got->n == expect->count,
	    "%s: expected %lu records, got %lu",
	    what, (unsigned long)expect->count, (unsigned long)got->n);
	for (i = 0; i < expect->count; i++) {
		gi = backward ? got->n - 1 - i : i;
		TEST_ASSERT2(rec_full_equal(&got->recs[gi],
		    &expect->records[i]),
		    "%s: position %lu differs from the expected record %lu",
		    what, (unsigned long)gi, (unsigned long)i);
	}
}

/* ---------- type 1: static data ---------- */

/*
 * A full scan of a static table: forward is the sorted order, backward
 * its reverse.
 */
static int tb3_static_full(int backward, const char *what) {
	VAN_STORE *store;
	VAN_TABLE *table;
	TESTDATA sorted;
	coll_t got;
	flags_t direction;
	int ret;

	direction = backward ? VAN_TRAVERSE_BACKWARD : VAN_TRAVERSE_FORWARD;

	TESTDATA_INIT(&sorted, TB3_N);
	(void)generate_string_testdata(&sorted, 20, 30, 50, 120);

	ret = tb3_setup(&store, &table, "test1");
	CHK_RET(ret, "setup");

	ret = populate_table(table, &sorted);
	CHK_RET(ret, "populate_table");

	sort_testdata(&sorted, van_datum_defcmp);

	memset(&got, 0, sizeof(got));
	ret = table->traverse(table, direction, NULL, coll_cb, &got);
	CHK_RET(ret != 0, "traverse must return 0");

	cmp_exact(&got, &sorted, backward, what);

	coll_free(&got);
	ret = tb3_teardown(table, store);
	CHK_RET(ret, "teardown");
	(void)free_string_testdata(&sorted);
	return (0);
}

/*
 * A range scan of a static table: the result must be exactly the
 * records of the sorted set inside the range, in scan order.
 *
 * lo_incl/hi_incl describe the range in ascending key order; how they
 * map onto the logical boundaries of VAN_TRAVERSE_BACKWARD is the one
 * subtle bit, see the header of VAN_TRAVERSE_OPT.
 */
static int tb3_static_range(int backward, int lo_incl, int hi_incl,
    const char *what) {
	VAN_STORE *store;
	VAN_TABLE *table;
	TESTDATA sorted, expect;
	coll_t got;
	VAN_DATUM lod, hid;
	VAN_TRAVERSE_OPT opt;
	RECORD lorec, hirec;
	flags_t direction;
	int ret;

	direction = backward ? VAN_TRAVERSE_BACKWARD : VAN_TRAVERSE_FORWARD;

	TESTDATA_INIT(&sorted, TB3_N);
	(void)generate_string_testdata(&sorted, 20, 30, 50, 120);

	ret = tb3_setup(&store, &table, "test1");
	CHK_RET(ret, "setup");

	ret = populate_table(table, &sorted);
	CHK_RET(ret, "populate_table");

	sort_testdata(&sorted, van_datum_defcmp);
	pick_anchors(&sorted, &lorec, &hirec);
	rec_key_datum(&lorec, &lod);
	rec_key_datum(&hirec, &hid);

	/* The expected subset, from the same anchors and inclusivity. */
	testdata_intersect_range(&expect, &sorted, &lod, lo_incl,
	    &hid, hi_incl);

	if (backward) {
		/* Backward: start at the high anchor, stop at the low. */
		opt.start_key = &hid;
		opt.end_key = &lod;
		opt.start_inclusive = hi_incl;
		opt.end_inclusive = lo_incl;
	} else {
		opt.start_key = &lod;
		opt.end_key = &hid;
		opt.start_inclusive = lo_incl;
		opt.end_inclusive = hi_incl;
	}

	memset(&got, 0, sizeof(got));
	ret = table->traverse(table, direction, &opt, coll_cb, &got);
	CHK_RET(ret != 0, "traverse must return 0");

	cmp_exact(&got, &expect, backward, what);

	coll_free(&got);
	testdata_free_shallow(&expect);
	ret = tb3_teardown(table, store);
	CHK_RET(ret, "teardown");
	(void)free_string_testdata(&sorted);
	return (0);
}

int table_t3_1a() {
	return tb3_static_full(0, "1a forward full");
}

int table_t3_1b() {
	return tb3_static_full(1, "1b backward full");
}

int table_t3_1c() {
	return tb3_static_range(0, 1, 0, "1c forward [lo,hi)");
}

int table_t3_1d() {
	return tb3_static_range(0, 1, 1, "1d forward [lo,hi]");
}

int table_t3_1e() {
	return tb3_static_range(0, 0, 0, "1e forward (lo,hi)");
}

int table_t3_1f() {
	return tb3_static_range(0, 0, 1, "1f forward (lo,hi]");
}

int table_t3_1g() {
	return tb3_static_range(1, 1, 0, "1g backward [lo,hi)");
}

int table_t3_1h() {
	return tb3_static_range(1, 1, 1, "1h backward [lo,hi]");
}

int table_t3_1i() {
	return tb3_static_range(1, 0, 0, "1i backward (lo,hi)");
}

int table_t3_1j() {
	return tb3_static_range(1, 0, 1, "1j backward (lo,hi]");
}

/* ---------- type 2: traversal while a thread keeps populating ---------- */

/*
 * Phase 1 fills half of the data set from the main thread.  Phase 2
 * starts a thread that fills the next quarter while the main thread
 * scans the table.
 *
 * What the scan can see is timing dependent: everything of phase 1,
 * plus any part of phase 2 that made it in before the snapshot was
 * taken.  Two relations hold regardless of the timing:
 *
 *   records1_new <= got <= records2_new
 *
 * where records1_new is the sorted copy of the phase 1 records and
 * records2_new the sorted copy of phase 1 + phase 2 together.  Both
 * are deep copies: the populate order of the original records must
 * survive, a sorted populate would not be the scenario under test.
 */
typedef struct {
	VAN_TABLE *table;
	TESTDATA   td;
} tb3_pop_t;

static void *tb3_popthread(void *arg) {
	tb3_pop_t *p;
	int ret;

	p = (tb3_pop_t *)arg;
	ret = populate_table(p->table, &p->td);
	CHK_RET(ret, "populate_table (populate thread)");

	return (NULL);
}

static void tb3_view(coll_t *c, TESTDATA *td) {
	td->records = c->recs;
	td->count = c->n;
}

static void assert_subset(TESTDATA *small, TESTDATA *big, const char *what,
    const char *whichtd) {
	size_t miss;

	miss = testdata_is_subset(small, big);
	if (miss != 0)
		fprintf(stderr, "%s: %s first missing record %lu, key '%s'\n",
		    what, whichtd, (unsigned long)(miss - 1),
		    (char *)small->records[miss - 1].key);
	TEST_ASSERT2(miss == 0, "%s: %s is not a subset, first miss %lu",
	    what, whichtd, (unsigned long)(miss - 1));
}

/*
 * One concurrent scan.  backward picks the direction, lo_incl/hi_incl
 * the range ends in ascending key order; a NULL opt case is expressed
 * by have_range == 0, the anchors are then ignored.
 */
static int tb3_concurrent(int backward, int have_range, int lo_incl,
    int hi_incl, const char *what) {
	VAN_STORE *store;
	VAN_TABLE *table;
	TESTDATA td, r1, r2, r1c, allc, r1exp, allexp, gotv;
	coll_t got;
	VAN_DATUM lod, hid;
	VAN_TRAVERSE_OPT opt;
	RECORD lorec, hirec;
	tb3_pop_t pop;
	pthread_t th;
	flags_t direction;
	int ret;

	direction = backward ? VAN_TRAVERSE_BACKWARD : VAN_TRAVERSE_FORWARD;

	TESTDATA_INIT(&td, TB3_N);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);
	/* Half for phase 1, the next quarter for phase 2, none wasted. */
	TESTDATA_SLICE(&r1, &td, 0, TB3_N / 2);
	TESTDATA_SLICE(&r2, &td, TB3_N / 2, TB3_N / 4);

	ret = tb3_setup(&store, &table, "test1");
	CHK_RET(ret, "setup");

	/* Phase 1: the records the scan is later required to see. */
	ret = populate_table(table, &r1);
	CHK_RET(ret, "populate_table (phase 1)");

	testdata_copy(&r1c, &r1);
	testdata_copy2(&allc, &r1, &r2);
	sort_testdata(&r1c, van_datum_defcmp);
	sort_testdata(&allc, van_datum_defcmp);

	memset(&opt, 0, sizeof(opt));
	if (have_range) {
		/*
		 * Anchors out of the sorted phase 1 copy: the range is
		 * fully inside the data the scan must see, but phase 2
		 * records may fall into it as well.
		 */
		pick_anchors(&r1c, &lorec, &hirec);
		rec_key_datum(&lorec, &lod);
		rec_key_datum(&hirec, &hid);
		if (backward) {
			opt.start_key = &hid;
			opt.end_key = &lod;
			opt.start_inclusive = hi_incl;
			opt.end_inclusive = lo_incl;
		} else {
			opt.start_key = &lod;
			opt.end_key = &hid;
			opt.start_inclusive = lo_incl;
			opt.end_inclusive = hi_incl;
		}
		testdata_intersect_range(&r1exp, &r1c, &lod, lo_incl,
		    &hid, hi_incl);
		testdata_intersect_range(&allexp, &allc, &lod, lo_incl,
		    &hid, hi_incl);
	}

	/* Phase 2: keep filling while the main thread scans. */
	pop.table = table;
	pop.td = r2;
	ret = pthread_create(&th, NULL, tb3_popthread, &pop);
	CHK_RET(ret, "pthread_create");

	memset(&got, 0, sizeof(got));
	ret = table->traverse(table, direction,
	    have_range ? &opt : NULL, coll_cb, &got);
	CHK_RET(ret != 0, "traverse must return 0");
	(void)pthread_join(th, NULL);

	/*
	 * The result is compared as a set, the order a concurrent scan
	 * produces is not deterministic across sources.
	 */
	coll_sort(&got);
	tb3_view(&got, &gotv);

	if (have_range) {
		assert_subset(&r1exp, &gotv, what, "records1_range");
		assert_subset(&gotv, &allexp, what, "records3");
		testdata_free_shallow(&r1exp);
		testdata_free_shallow(&allexp);
	} else {
		assert_subset(&r1c, &gotv, what, "records1_new");
		assert_subset(&gotv, &allc, what, "records3");
	}

	coll_free(&got);
	ret = tb3_teardown(table, store);
	CHK_RET(ret, "teardown");
	(void)free_string_testdata(&r1c);
	(void)free_string_testdata(&allc);
	(void)free_string_testdata(&td);
	return (0);
}

int table_t3_2a() {
	return tb3_concurrent(0, 0, 0, 0, "2a forward full, concurrent");
}

int table_t3_2b() {
	return tb3_concurrent(1, 0, 0, 0, "2b backward full, concurrent");
}

int table_t3_2c() {
	return tb3_concurrent(0, 1, 1, 0, "2c forward [lo,hi), concurrent");
}

int table_t3_2d() {
	return tb3_concurrent(0, 1, 1, 1, "2d forward [lo,hi], concurrent");
}

int table_t3_2e() {
	return tb3_concurrent(0, 1, 0, 0, "2e forward (lo,hi), concurrent");
}

int table_t3_2f() {
	return tb3_concurrent(0, 1, 0, 1, "2f forward (lo,hi], concurrent");
}

int table_t3_2g() {
	return tb3_concurrent(1, 1, 1, 0, "2g backward [lo,hi), concurrent");
}

int table_t3_2h() {
	return tb3_concurrent(1, 1, 1, 1, "2h backward [lo,hi], concurrent");
}

int table_t3_2i() {
	return tb3_concurrent(1, 1, 0, 0, "2i backward (lo,hi), concurrent");
}

int table_t3_2j() {
	return tb3_concurrent(1, 1, 0, 1, "2j backward (lo,hi], concurrent");
}
