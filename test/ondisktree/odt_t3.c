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
#include <stdlib.h>
#include <string.h>

/*
 * Static range traversal of an on-disk tree through its cursor.
 *
 * An on-disk tree is written once and never written again, so there is
 * no "read while writing" scenario for it.  We build it the same way the
 * table level does -- populate the active in-memory tree, then
 * table->dump_inmem() -- and walk the resulting on-disk tree with the
 * cursor, exactly as table->traverse() spells it out: VAN_SET_RANGE
 * lands on a bound and VAN_NEXT / VAN_PREV walks away from it.
 *
 *   odt_t3_1a  forward full walk
 *   odt_t3_1b  backward full walk
 *   odt_t3_1c..1j  forward / backward range, four open/closed ends each
 *   odt_t3_2a..2e  boundary cases (missing bound, empty range)
 *
 * Every case is independently runnable via odt_tmain <case_name>.
 *
 * The table is opened with VAN_TABLE_SKIP_THREAD_START so no background
 * thread dumps or recycles trees while the test controls the dumps
 * itself; start_threads is never called afterwards.
 */

#define ODT3_N 100000

/* ---------- context: a single on-disk tree under test ---------- */

typedef struct {
	VAN_STORE	*store;
	VAN_TABLE	*table;
	ONDISK_TREE	*odtree;
	TESTDATA	td;
} odt3_ctx_t;

/* ---------- collector: a growable array of deep-copied records ---------- */

typedef struct {
	RECORD *recs;
	size_t	cap;
	size_t	n;
} coll_t;

static void coll_add(coll_t *c, const VAN_DATUM *k, const VAN_DATUM *v) {
	RECORD *r;

	if (c->n == c->cap) {
		c->cap = c->cap == 0 ? 64 : c->cap * 2;
		c->recs = (RECORD *)realloc(c->recs, c->cap * sizeof(RECORD));
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

/* ---------- record comparison (key, then value = total order) ---------- */

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
	return (van_datum_defcmp(key, &k));
}

/*
 * Records of src (sorted ascending) whose key falls into [lo, hi], with
 * the ends kept or dropped as asked.  Either endpoint may be NULL, which
 * means "unbounded" on that side.  The result aliases the buffers of src
 * (shallow), so only the array itself may be freed.
 */
static void odt3_range(TESTDATA *dst, TESTDATA *src, const RECORD *lo,
    int lo_incl, const RECORD *hi, int hi_incl) {
	RECORD *recs;
	size_t i, n;

	recs = (RECORD *)malloc(src->count * sizeof(RECORD));
	TEST_ASSERT(recs != NULL);
	n = 0;
	for (i = 0; i < src->count; i++) {
		if (lo != NULL) {
			int c = rec_keycmp(&src->records[i], lo);
			if (c < 0)
				continue;
			if (!lo_incl && c == 0)
				continue;
		}
		if (hi != NULL) {
			int c = rec_keycmp(&src->records[i], hi);
			if (c > 0)
				continue;
			if (!hi_incl && c == 0)
				continue;
		}
		recs[n++] = src->records[i];
	}
	dst->records = recs;
	dst->count = n;
}

/* ---------- setup / teardown ---------- */

static int odt3_setup(odt3_ctx_t *ctx, size_t n) {
	int ret;

	memset(ctx, 0, sizeof(*ctx));
	TESTDATA_INIT(&ctx->td, n);
	(void)generate_string_testdata(&ctx->td, 20, 30, 50, 120);

	system("rm -fr test1.ondisk.odt_t3*");
	system("rm -fr vanlog.*");

	ret = van_test_store_create(&ctx->store);
	CHK_RET(ret, "van_store_create");
	ret = ctx->store->open(ctx->store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(ctx->store, &ctx->table);
	CHK_RET(ret, "van_table_create");
	/* Keep the inmem threshold huge so nothing dumps on its own. */
	ctx->table->set_inmem_threshold(ctx->table, GIGABYTE);
	/*
	 * Skip the background threads; the dump is done explicitly below.
	 * No start_threads() is called afterwards.
	 */
	ret = ctx->table->open(ctx->table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");

	ret = populate_table(ctx->table, &ctx->td);
	CHK_RET(ret, "populate_table");
	ret = ctx->table->dump_inmem(ctx->table);
	CHK_RET(ret, "table->dump_inmem");

	TEST_ASSERT(ctx->table->odt_top >= 1);
	ctx->odtree = ctx->table->odt_items[ctx->table->odt_top - 1].trees[0];
	TEST_ASSERT(ctx->odtree != NULL);
	return (0);
}

static int odt3_teardown(odt3_ctx_t *ctx) {
	int ret;

	/* The on-disk tree lives in table->odt_items; table->close frees it. */
	ret = ctx->table->close(ctx->table, 0);
	CHK_RET(ret, "table->close");
	ret = ctx->store->close(ctx->store);
	CHK_RET(ret, "store->close");
	(void)free_string_testdata(&ctx->td);
	return (0);
}

/* ---------- the walk ---------- */

/*
 * Walk the on-disk tree with a cursor and collect what it reports.
 *
 * lorec / hirec are the low / high anchors of the range; either may be
 * NULL for "unbounded" on that side.  VAN_SET_RANGE is a lower bound in
 * both directions, so backward it is corrected with a single VAN_PREV,
 * exactly as the table level does it.
 */
static int odt3_walk(ONDISK_TREE *odtree, coll_t *c, int backward,
    const RECORD *lorec, int lo_incl, const RECORD *hirec, int hi_incl) {
	ONDISK_TREECURSOR *cursor;
	VAN_DATUM key, value;
	flags_t step;
	int ret, ret1, cmp;
	int have_lo, have_hi;

	have_lo = (lorec != NULL);
	have_hi = (hirec != NULL);
	step = backward ? VAN_PREV : VAN_NEXT;

	cursor = NULL;
	ret = odtree->cursor(odtree, &cursor, 0);
	CHK_RET(ret, "odtree->cursor");

	memset(&key, 0, sizeof(key));
	memset(&value, 0, sizeof(value));

	if (backward) {
		if (have_hi) {
			const RECORD *near = hirec;
			int near_incl = hi_incl;

			key.data = near->key;
			key.size = near->ksize;
			ret = cursor->get(cursor, &key, &value, VAN_SET_RANGE);
			if (ret != 0 && ret != VAN_NOTFOUND)
				CHK_RET(ret, "cursor->get (VAN_SET_RANGE)");
			if (ret == 0) {
				cmp = key_cmp(&key, near);
				if (cmp > 0 || (!near_incl && cmp == 0)) {
					ret = cursor->get(cursor, &key,
					    &value, VAN_PREV);
					if (ret != 0 && ret != VAN_NOTFOUND)
						CHK_RET(ret,
						    "cursor->get (PREV fixup)");
				}
			} else {
				/*
				 * Nothing is >= the high anchor, so every
				 * key of the tree is inside the range:
				 * start at the last one.
				 */
				ret = cursor->get(cursor, &key, &value,
				    VAN_LAST);
				if (ret != 0 && ret != VAN_NOTFOUND)
					CHK_RET(ret, "cursor->get (VAN_LAST)");
			}
		} else {
			ret = cursor->get(cursor, &key, &value, VAN_LAST);
			if (ret != 0 && ret != VAN_NOTFOUND)
				CHK_RET(ret, "cursor->get (VAN_LAST)");
		}
	} else {
		if (have_lo) {
			const RECORD *near = lorec;
			int near_incl = lo_incl;

			key.data = near->key;
			key.size = near->ksize;
			ret = cursor->get(cursor, &key, &value, VAN_SET_RANGE);
			if (ret != 0 && ret != VAN_NOTFOUND)
				CHK_RET(ret, "cursor->get (VAN_SET_RANGE)");
			if (ret == 0) {
				cmp = key_cmp(&key, near);
				if (!near_incl && cmp == 0) {
					ret = cursor->get(cursor, &key,
					    &value, VAN_NEXT);
					if (ret != 0 && ret != VAN_NOTFOUND)
						CHK_RET(ret,
						    "cursor->get (NEXT fixup)");
				}
			}
			/* forward NOTFOUND: nothing >= lo -> empty. */
		} else {
			ret = cursor->get(cursor, &key, &value, VAN_FIRST);
			if (ret != 0 && ret != VAN_NOTFOUND)
				CHK_RET(ret, "cursor->get (VAN_FIRST)");
		}
	}

	while (ret == 0) {
		if (backward) {
			if (have_lo) {
				cmp = key_cmp(&key, lorec);
				if (cmp < 0 || (!lo_incl && cmp == 0))
					break;
			}
		} else {
			if (have_hi) {
				cmp = key_cmp(&key, hirec);
				if (cmp > 0 || (!hi_incl && cmp == 0))
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

/* The walk has to hand the records out in key order. */
static void check_order(coll_t *c, int backward, const char *what) {
	size_t i;

	for (i = 1; i < c->n; i++) {
		int cmp = rec_keycmp(&c->recs[i - 1], &c->recs[i]);
		TEST_ASSERT2(backward ? (cmp > 0) : (cmp < 0),
		    "%s: order broken at %lu", what, (unsigned long)i);
	}
}

/* Compare a collected walk (sorted ascending) against the expected set. */
static void odt3_check(coll_t *got, TESTDATA *expected, const char *what) {
	size_t i;

	qsort(got->recs, got->n, sizeof(RECORD), rec_qsort_cmp);
	TEST_ASSERT2(got->n == expected->count,
	    "%s: expected %lu records, got %lu", what,
	    (unsigned long)expected->count, (unsigned long)got->n);
	for (i = 0; i < got->n; i++)
		TEST_ASSERT2(rec_full_equal(&got->recs[i],
		    &expected->records[i]), "%s: record %lu differs",
		    what, (unsigned long)i);
}

/* ---------- full + range (1a .. 1j) ---------- */

static int odt3_static(int backward, int have_range, int lo_incl,
    int hi_incl, const char *what) {
	odt3_ctx_t ctx;
	TESTDATA sorted, expect;
	coll_t got;
	RECORD lorec, hirec;
	int ret;

	ret = odt3_setup(&ctx, ODT3_N);
	CHK_RET(ret, "setup");

	/*
	 * Sort in place with the tree's compare function; this is the
	 * order a forward walk returns records in.  free_string_testdata
	 * only frees buffers, so reordering is harmless.
	 */
	ret = sort_testdata(&ctx.td, ctx.odtree->table->cmp_func);
	CHK_RET(ret, "sort_testdata");
	sorted = ctx.td;

	if (have_range) {
		size_t i = sorted.count / 4;
		size_t j = sorted.count - 1 - sorted.count / 4;
		TEST_ASSERT2(i < j, "%s: anchors unordered", what);
		lorec = sorted.records[i];
		hirec = sorted.records[j];
		odt3_range(&expect, &sorted, &lorec, lo_incl, &hirec,
		    hi_incl);
	} else {
		expect = sorted;
	}

	memset(&got, 0, sizeof(got));
	ret = odt3_walk(ctx.odtree, &got, backward,
	    have_range ? &lorec : NULL, lo_incl,
	    have_range ? &hirec : NULL, hi_incl);
	CHK_RET(ret, "walk");

	check_order(&got, backward, what);
	odt3_check(&got, &expect, what);

	coll_free(&got);
	if (have_range)
		free(expect.records);
	ret = odt3_teardown(&ctx);
	CHK_RET(ret, "teardown");
	return (0);
}

/* ---------- boundary cases (2a .. 2e) ---------- */

/* key of lo followed by one 0x00 byte: strictly greater than lo, and
 * strictly less than hi for any hi > lo (byte order), so a synthetic key
 * sitting between two existing keys. */
static void synth_between(const RECORD *lo, RECORD *out) {
	out->key = (char *)malloc(lo->ksize + 1);
	TEST_ASSERT(out->key != NULL);
	memcpy(out->key, lo->key, lo->ksize);
	out->key[lo->ksize] = 0;
	out->ksize = lo->ksize + 1;
	out->value = NULL;
	out->vsize = 0;
}

/* key of r with its last byte dropped: a strict prefix of r, hence
 * strictly less than r and therefore less than every key. */
static void synth_below(const RECORD *r, RECORD *out) {
	TEST_ASSERT2(r->ksize >= 2, "synth_below needs ksize >= 2");
	out->key = (char *)malloc(r->ksize - 1);
	TEST_ASSERT(out->key != NULL);
	memcpy(out->key, r->key, r->ksize - 1);
	out->ksize = r->ksize - 1;
	out->value = NULL;
	out->vsize = 0;
}

static int odt3_boundary(int which, const char *what) {
	odt3_ctx_t ctx;
	TESTDATA expect;
	coll_t got;
	RECORD lo, hi;
	const RECORD *lop, *hip;
	int have_lo, lo_incl, have_hi, hi_incl;
	int free_lo, free_hi;
	size_t N, i;
	int ret;

	ret = odt3_setup(&ctx, ODT3_N);
	CHK_RET(ret, "setup");
	ret = sort_testdata(&ctx.td, ctx.odtree->table->cmp_func);
	CHK_RET(ret, "sort_testdata");

	N = ctx.td.count;
	i = N / 2;
	lop = NULL;
	hip = NULL;
	have_lo = 0;
	lo_incl = 1;
	have_hi = 0;
	hi_incl = 1;
	free_lo = 0;
	free_hi = 0;

	switch (which) {
	case 2:	/* 2a: start_key absent (between two keys) -> from next. */
		synth_between(&ctx.td.records[i - 1], &lo);
		have_lo = 1;
		lo_incl = 1;
		lop = &lo;
		free_lo = 1;
		break;
	case 3:	/* 2b: end_key absent (between two keys) -> to previous. */
		synth_between(&ctx.td.records[i - 1], &hi);
		have_hi = 1;
		hi_incl = 1;
		hip = &hi;
		free_hi = 1;
		break;
	case 4:	/* 2c: start_key > every key -> empty. */
		synth_between(&ctx.td.records[N - 1], &lo);
		have_lo = 1;
		lo_incl = 1;
		lop = &lo;
		free_lo = 1;
		break;
	case 5:	/* 2d: end_key < every key -> empty. */
		synth_below(&ctx.td.records[0], &hi);
		have_hi = 1;
		hi_incl = 1;
		hip = &hi;
		free_hi = 1;
		break;
	case 6:	/* 2e: empty range (start > end) -> empty. */
		lo = ctx.td.records[N / 2];
		hi = ctx.td.records[0];
		have_lo = 1;
		lo_incl = 1;
		have_hi = 1;
		hi_incl = 1;
		lop = &lo;
		hip = &hi;
		break;
	default:
		TEST_ASSERT2(0, "%s: bad boundary selector %d", what, which);
		break;
	}

	odt3_range(&expect, &ctx.td, lop, lo_incl, hip, hi_incl);

	memset(&got, 0, sizeof(got));
	ret = odt3_walk(ctx.odtree, &got, 0, lop, lo_incl, hip, hi_incl);
	CHK_RET(ret, "walk");
	check_order(&got, 0, what);
	odt3_check(&got, &expect, what);

	coll_free(&got);
	free(expect.records);
	if (free_lo && lo.key != NULL)
		free(lo.key);
	if (free_hi && hi.key != NULL)
		free(hi.key);
	ret = odt3_teardown(&ctx);
	CHK_RET(ret, "teardown");
	return (0);
}

/* ---------- the cases ---------- */

int odt_t3_1a() {
	return odt3_static(0, 0, 0, 0, "1a forward full");
}

int odt_t3_1b() {
	return odt3_static(1, 0, 0, 0, "1b backward full");
}

int odt_t3_1c() {
	return odt3_static(0, 1, 1, 0, "1c forward [lo,hi)");
}

int odt_t3_1d() {
	return odt3_static(0, 1, 1, 1, "1d forward [lo,hi]");
}

int odt_t3_1e() {
	return odt3_static(0, 1, 0, 0, "1e forward (lo,hi)");
}

int odt_t3_1f() {
	return odt3_static(0, 1, 0, 1, "1f forward (lo,hi]");
}

int odt_t3_1g() {
	return odt3_static(1, 1, 1, 0, "1g backward [lo,hi)");
}

int odt_t3_1h() {
	return odt3_static(1, 1, 1, 1, "1h backward [lo,hi]");
}

int odt_t3_1i() {
	return odt3_static(1, 1, 0, 0, "1i backward (lo,hi)");
}

int odt_t3_1j() {
	return odt3_static(1, 1, 0, 1, "1j backward (lo,hi]");
}

int odt_t3_2a() {
	return odt3_boundary(2, "2a start missing");
}

int odt_t3_2b() {
	return odt3_boundary(3, "2b end missing");
}

int odt_t3_2c() {
	return odt3_boundary(4, "2c start > all");
}

int odt_t3_2d() {
	return odt3_boundary(5, "2d end < all");
}

int odt_t3_2e() {
	return odt3_boundary(6, "2e empty range");
}
