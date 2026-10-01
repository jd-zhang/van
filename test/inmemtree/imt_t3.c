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

/*
 * In-memory tree cursor over random data, standing and against
 * concurrent puts.
 *
 * An on disk tree is written once and never written again, so there is
 * no "read while writing" scenario for it.  An in-memory tree is the
 * only object where a cursor walk can run while other threads keep
 * putting, which is what these cases do:
 *
 *   imt_t3_1a  static data, forward full walk
 *   imt_t3_1b  static data, backward full walk
 *   imt_t3_1c..1j  static data, forward / backward range, four open
 *		    and closed ends each
 *   imt_t3_2a..2j  the same ten walks while three threads keep putting
 *
 * The walks are what table->traverse() does, spelled out with the
 * cursor: VAN_SET_RANGE lands on a bound and VAN_NEXT / VAN_PREV walks
 * away from it, which is the combination the table level uses for a
 * range scan.
 *
 * Every case is independently runnable via imt_tmain <case_name>.
 */

#define IMT3_N 1000000

/*
 * How many threads share the second batch of puts.  More writers mean
 * more concurrent splits, which is what a walk has to survive.
 */
#define IMT3_TPUTS 3

/* ---------- setup ---------- */

/*
 * The tree needs a table only for its compare function; the threshold
 * is set high enough that the tree is never frozen, so the only
 * concurrency left in the concurrent cases is put against the walk.
 */
static int imt_t3_setup(INMEM_TREE **treep) {
	VAN_TABLE table;
	INMEM_TREE *tree;
	int ret;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "van_inmemtree_create");
	tree = table.active;
	tree->threshold = 1 * GIGABYTE;
	*treep = tree;

	return (0);
}

/* ---------- collector: a growable array of copied records ---------- */

typedef struct {
	RECORD *recs;
	size_t	cap;
	size_t	n;
} coll_t;

static void coll_add(coll_t *c, const VAN_DATUM *k, const VAN_DATUM *v) {
	RECORD *r;

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
	 * over key+value pairs, which is what a subset compares.
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

static int rec_full_equal(const RECORD *a, const RECORD *b) {
	if (a->ksize != b->ksize || a->vsize != b->vsize)
		return (0);
	return memcmp(a->key, b->key, a->ksize) == 0 &&
	    memcmp(a->value, b->value, a->vsize) == 0;
}

/* Compare a key the cursor handed back against a record's key. */
static int key_cmp(const VAN_DATUM *key, const RECORD *rec) {
	VAN_DATUM k;

	memset(&k, 0, sizeof(k));
	k.data = rec->key;
	k.size = rec->ksize;
	return van_datum_defcmp(key, &k);
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
 * A deep copy: sort_testdata() sorts in place, and the populate order
 * of the original records has to survive, a sorted populate would not
 * be the scenario under test.
 */
static void testdata_dup(TESTDATA *dst, const TESTDATA *src) {
	size_t i;

	TESTDATA_INIT(dst, src->count);
	for (i = 0; i < src->count; i++)
		record_deep_copy(&dst->records[i], &src->records[i]);
}

/* The deep copy of two data sets taken one after the other. */
static void testdata_concat(TESTDATA *dst, const TESTDATA *a,
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
 * The records of src (sorted ascending) whose key falls into [lo, hi],
 * with the ends kept or dropped as asked.  The result is a new record
 * array whose entries alias the buffers of src, so only the array may
 * be freed, see testdata_free_shallow().
 */
static void testdata_range(TESTDATA *dst, TESTDATA *src, const RECORD *lo,
    int lo_incl, const RECORD *hi, int hi_incl) {
	RECORD *recs;
	size_t i, n;

	recs = (RECORD *)malloc(src->count * sizeof(RECORD));
	TEST_ASSERT(recs != NULL);
	n = 0;
	for (i = 0; i < src->count; i++) {
		if (rec_keycmp(&src->records[i], lo) < 0 ||
		    (!lo_incl && rec_keycmp(&src->records[i], lo) == 0))
			continue;
		if (rec_keycmp(&src->records[i], hi) > 0 ||
		    (!hi_incl && rec_keycmp(&src->records[i], hi) == 0))
			continue;
		recs[n++] = src->records[i];
	}
	dst->records = recs;
	dst->count = n;
}

static void testdata_free_shallow(TESTDATA *td) {
	free(td->records);
	td->records = NULL;
	td->count = 0;
}

/*
 * Is small a subset of big?  Both must be sorted.  Returns 0 on
 * success, else 1 + the position of the first record of small that is
 * missing from big (or has a different value there).
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
		if (!rec_full_equal(&small->records[i], &big->records[j]))
			return (i + 1);
		i++;
		j++;
	}
	return (0);
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
 * Two real records of a sorted data set as the anchors of a range: the
 * low one out of the first quarter, the high one out of the last, so
 * the range is wide but never the whole set.
 */
static void pick_anchors(TESTDATA *sorted, RECORD *lorec, RECORD *hirec) {
	size_t i, j;

	i = (size_t)(rand() % (int)(sorted->count / 4));
	j = sorted->count - 1 - (size_t)(rand() % (int)(sorted->count / 4));
	TEST_ASSERT2(i < j, "anchor indices out of order: %lu, %lu",
	    (unsigned long)i, (unsigned long)j);
	TEST_ASSERT2(rec_keycmp(&sorted->records[i],
	    &sorted->records[j]) < 0, "anchor keys not strictly ascending");
	*lorec = sorted->records[i];
	*hirec = sorted->records[j];
}

/* ---------- the walk ---------- */

/*
 * Walk the tree with a cursor and collect what it reports.
 *
 * Without a range this is VAN_FIRST / VAN_LAST followed by VAN_NEXT /
 * VAN_PREV.  With one it is VAN_SET_RANGE onto the near bound -- the
 * low key walking forward, the high one walking backward -- and then
 * the same step away from it, stopping when the far bound is passed.
 *
 * VAN_SET_RANGE is a lower bound in both directions, which is what a
 * forward walk wants.  Backward it is not: the landing point there is
 * corrected with a single VAN_PREV, exactly as the table level does it.
 *
 * lorec == NULL means "no range".
 */
static int imt3_walk(INMEM_TREE *tree, coll_t *c, int backward,
    const RECORD *lorec, int lo_incl, const RECORD *hirec, int hi_incl) {
	INMEM_TREECURSOR *cursor;
	VAN_DATUM key, value;
	flags_t step;
	int ret, ret1, cmp;

	step = backward ? VAN_PREV : VAN_NEXT;

	cursor = NULL;
	ret = tree->cursor(tree, &cursor, 0);
	CHK_RET(ret, "tree->cursor");

	memset(&key, 0, sizeof(key));
	memset(&value, 0, sizeof(value));

	if (lorec == NULL) {
		ret = cursor->get(cursor, &key, &value,
		    backward ? VAN_LAST : VAN_FIRST);
		if (ret != 0 && ret != VAN_NOTFOUND)
			CHK_RET(ret, "cursor->get (first)");
	} else {
		const RECORD *near = backward ? hirec : lorec;
		int near_incl = backward ? hi_incl : lo_incl;

		/*
		 * The search key goes in through the same datum the
		 * cursor fills with the key it landed on.
		 */
		key.data = near->key;
		key.size = near->ksize;
		ret = cursor->get(cursor, &key, &value, VAN_SET_RANGE);
		if (ret != 0 && ret != VAN_NOTFOUND)
			CHK_RET(ret, "cursor->get (VAN_SET_RANGE)");

		if (ret == 0) {
			cmp = key_cmp(&key, near);
			if (backward) {
				/*
				 * Landed above the high anchor, or on
				 * it while it is excluded: step back.
				 */
				if (cmp > 0 || (!near_incl && cmp == 0))
					ret = cursor->get(cursor, &key,
					    &value, VAN_PREV);
			} else if (!near_incl && cmp == 0) {
				ret = cursor->get(cursor, &key, &value,
				    VAN_NEXT);
			}
			if (ret != 0 && ret != VAN_NOTFOUND)
				CHK_RET(ret, "cursor->get (fixup)");
		} else if (backward) {
			/*
			 * Nothing is >= the high anchor, so every key
			 * of the tree is inside the range: start at
			 * the last one.
			 */
			ret = cursor->get(cursor, &key, &value, VAN_LAST);
			if (ret != 0 && ret != VAN_NOTFOUND)
				CHK_RET(ret, "cursor->get (last)");
		}
	}

	while (ret == 0) {
		const RECORD *far = backward ? lorec : hirec;
		int far_incl = backward ? lo_incl : hi_incl;

		if (far != NULL) {
			cmp = key_cmp(&key, far);
			if (backward) {
				if (cmp < 0 || (!far_incl && cmp == 0))
					break;
			} else {
				if (cmp > 0 || (!far_incl && cmp == 0))
					break;
			}
		}

		coll_add(c, &key, &value);
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		ret = cursor->get(cursor, &key, &value, step);
		if (ret != 0 && ret != VAN_NOTFOUND)
			CHK_RET(ret, "cursor->get (step)");
	}
	if (ret == VAN_NOTFOUND)
		ret = 0;

	ret1 = cursor->cleanup(cursor);
	SET_RET(ret1, ret);

	return (ret);
}

/*
 * The walk has to hand the records out in key order: ascending going
 * forward, descending going backward.
 */
static void check_order(coll_t *c, int backward, const char *what) {
	size_t i;

	for (i = 1; i < c->n; i++) {
		int cmp = rec_keycmp(&c->recs[i - 1], &c->recs[i]);
		TEST_ASSERT2(backward ? (cmp > 0) : (cmp < 0),
		    "%s: order broken at %lu", what, (unsigned long)i);
	}
}

/* ---------- concurrent populate ---------- */

typedef struct {
	INMEM_TREE *tree;
	TESTDATA    td;
} imt_t3_pop_t;

static void *imt_t3_popthread(void *arg) {
	imt_t3_pop_t *p;
	int ret;

	p = (imt_t3_pop_t *)arg;
	ret = populate_inmemtree(p->tree, &p->td);
	CHK_RET(ret, "populate_inmemtree (populate thread)");

	return (NULL);
}

static int imt3_start_writers(INMEM_TREE *tree, TESTDATA *r2,
    imt_t3_pop_t *pops, pthread_t *th) {
	size_t each, off;
	int i, ret;

	/*
	 * The slice a thread gets stays in the order the data was
	 * generated in, only the slices are handed out.
	 */
	each = r2->count / IMT3_TPUTS;
	memset(pops, 0, sizeof(*pops) * IMT3_TPUTS);
	off = 0;
	for (i = 0; i < IMT3_TPUTS; i++) {
		pops[i].tree = tree;
		TESTDATA_SLICE(&pops[i].td, r2, off,
		    i == IMT3_TPUTS - 1 ? r2->count - off : each);
		off += each;
		ret = pthread_create(&th[i], NULL, imt_t3_popthread,
		    &pops[i]);
		CHK_RET(ret, "pthread_create");
	}

	return (0);
}

/* ---------- static data: the walk has to be exactly the data ---------- */

static int imt3_static(int backward, int have_range, int lo_incl,
    int hi_incl, const char *what) {
	INMEM_TREE *tree;
	TESTDATA td, sorted, expect;
	coll_t got;
	RECORD lorec, hirec;
	int ret;

	TESTDATA_INIT(&td, IMT3_N);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);

	ret = imt_t3_setup(&tree);
	CHK_RET(ret, "setup");

	ret = populate_inmemtree(tree, &td);
	CHK_RET(ret, "populate_inmemtree");

	testdata_dup(&sorted, &td);
	sort_testdata(&sorted, van_datum_defcmp);

	if (have_range) {
		pick_anchors(&sorted, &lorec, &hirec);
		testdata_range(&expect, &sorted, &lorec, lo_incl, &hirec,
		    hi_incl);
	} else {
		expect = sorted;
	}

	memset(&got, 0, sizeof(got));
	ret = imt3_walk(tree, &got, backward,
	    have_range ? &lorec : NULL, lo_incl,
	    have_range ? &hirec : NULL, hi_incl);
	CHK_RET(ret, "walk");

	check_order(&got, backward, what);
	qsort(got.recs, got.n, sizeof(RECORD), rec_qsort_cmp);

	TEST_ASSERT2(got.n == expect.count, "%s: expected %lu records, got %lu",
	    what, (unsigned long)expect.count, (unsigned long)got.n);
	{
		TESTDATA gotv;
		size_t i;

		gotv.records = got.recs;
		gotv.count = got.n;
		for (i = 0; i < got.n; i++)
			TEST_ASSERT2(rec_full_equal(&got.recs[i],
			    &expect.records[i]), "%s: record %lu differs",
			    what, (unsigned long)i);
		(void)gotv;
	}

	coll_free(&got);
	if (have_range)
		testdata_free_shallow(&expect);
	(void)free_string_testdata(&sorted);
	(void)free_string_testdata(&td);

	ret = tree->destroy(tree);
	CHK_RET(ret, "tree->destroy");

	return (0);
}

/* ---------- concurrent puts: the walk stays inside what was written ---------- */

/*
 * Phase 1 fills half of the data set from the main thread.  Phase 2
 * splits the next quarter over IMT3_TPUTS threads while the main thread
 * walks the tree.
 *
 * What the walk sees is timing dependent: everything of phase 1, plus
 * any part of phase 2 that made it in before the walk came to it.  Two
 * relations hold regardless of the timing:
 *
 *   records1 <= records3 <= records2
 *
 * with a range, inside that range only.  Nothing is asserted about how
 * much of phase 2 shows up.
 */
static int imt3_concurrent(int backward, int have_range, int lo_incl,
    int hi_incl, const char *what) {
	INMEM_TREE *tree;
	TESTDATA td, r1, r2, r1c, allc, r1exp, allexp, gotv;
	coll_t got;
	RECORD lorec, hirec;
	imt_t3_pop_t pops[IMT3_TPUTS];
	pthread_t th[IMT3_TPUTS];
	int i, ret;

	TESTDATA_INIT(&td, IMT3_N);
	(void)generate_string_testdata(&td, 20, 30, 50, 120);
	/* Half for phase 1, the next quarter for phase 2, none wasted. */
	TESTDATA_SLICE(&r1, &td, 0, IMT3_N / 2);
	TESTDATA_SLICE(&r2, &td, IMT3_N / 2, IMT3_N / 4);

	ret = imt_t3_setup(&tree);
	CHK_RET(ret, "setup");

	/* Phase 1: the records the walk is later required to see. */
	ret = populate_inmemtree(tree, &r1);
	CHK_RET(ret, "populate_inmemtree (phase 1)");

	testdata_dup(&r1c, &r1);
	testdata_concat(&allc, &r1, &r2);
	sort_testdata(&r1c, van_datum_defcmp);
	sort_testdata(&allc, van_datum_defcmp);

	if (have_range) {
		/*
		 * Anchors out of the sorted phase 1 copy: the range is
		 * fully inside the data the walk must see, but phase 2
		 * records may fall into it as well.
		 */
		pick_anchors(&r1c, &lorec, &hirec);
		testdata_range(&r1exp, &r1c, &lorec, lo_incl, &hirec,
		    hi_incl);
		testdata_range(&allexp, &allc, &lorec, lo_incl, &hirec,
		    hi_incl);
	}

	ret = imt3_start_writers(tree, &r2, pops, th);
	CHK_RET(ret, "start writers");

	memset(&got, 0, sizeof(got));
	ret = imt3_walk(tree, &got, backward,
	    have_range ? &lorec : NULL, lo_incl,
	    have_range ? &hirec : NULL, hi_incl);
	CHK_RET(ret, "walk");
	for (i = 0; i < IMT3_TPUTS; i++)
		(void)pthread_join(th[i], NULL);

	check_order(&got, backward, what);
	qsort(got.recs, got.n, sizeof(RECORD), rec_qsort_cmp);

	/*
	 * How much of phase 2 the walk managed to see: anywhere between
	 * none of it and all of it is fine, it only has to stay inside
	 * what was written.  A number that never leaves the phase 1
	 * count would mean the two phases never really overlapped, and
	 * this case would be testing nothing.
	 */
	fprintf(stderr, "%s: walked %lu records\n", what,
	    (unsigned long)got.n);

	gotv.records = got.recs;
	gotv.count = got.n;

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
	(void)free_string_testdata(&r1c);
	(void)free_string_testdata(&allc);
	(void)free_string_testdata(&td);

	ret = tree->destroy(tree);
	CHK_RET(ret, "tree->destroy");

	return (0);
}

/* ---------- the cases ---------- */

int imt_t3_1a() {
	return imt3_static(0, 0, 0, 0, "1a forward full");
}

int imt_t3_1b() {
	return imt3_static(1, 0, 0, 0, "1b backward full");
}

int imt_t3_1c() {
	return imt3_static(0, 1, 1, 0, "1c forward [lo,hi)");
}

int imt_t3_1d() {
	return imt3_static(0, 1, 1, 1, "1d forward [lo,hi]");
}

int imt_t3_1e() {
	return imt3_static(0, 1, 0, 0, "1e forward (lo,hi)");
}

int imt_t3_1f() {
	return imt3_static(0, 1, 0, 1, "1f forward (lo,hi]");
}

int imt_t3_1g() {
	return imt3_static(1, 1, 1, 0, "1g backward [lo,hi)");
}

int imt_t3_1h() {
	return imt3_static(1, 1, 1, 1, "1h backward [lo,hi]");
}

int imt_t3_1i() {
	return imt3_static(1, 1, 0, 0, "1i backward (lo,hi)");
}

int imt_t3_1j() {
	return imt3_static(1, 1, 0, 1, "1j backward (lo,hi]");
}

int imt_t3_2a() {
	return imt3_concurrent(0, 0, 0, 0, "2a forward full, concurrent");
}

int imt_t3_2b() {
	return imt3_concurrent(1, 0, 0, 0, "2b backward full, concurrent");
}

int imt_t3_2c() {
	return imt3_concurrent(0, 1, 1, 0, "2c forward [lo,hi), concurrent");
}

int imt_t3_2d() {
	return imt3_concurrent(0, 1, 1, 1, "2d forward [lo,hi], concurrent");
}

int imt_t3_2e() {
	return imt3_concurrent(0, 1, 0, 0, "2e forward (lo,hi), concurrent");
}

int imt_t3_2f() {
	return imt3_concurrent(0, 1, 0, 1, "2f forward (lo,hi], concurrent");
}

int imt_t3_2g() {
	return imt3_concurrent(1, 1, 1, 0, "2g backward [lo,hi), concurrent");
}

int imt_t3_2h() {
	return imt3_concurrent(1, 1, 1, 1, "2h backward [lo,hi], concurrent");
}

int imt_t3_2i() {
	return imt3_concurrent(1, 1, 0, 0, "2i backward (lo,hi), concurrent");
}

int imt_t3_2j() {
	return imt3_concurrent(1, 1, 0, 1, "2j backward (lo,hi], concurrent");
}
