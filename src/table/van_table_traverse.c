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
#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"
#include "van_ondisk_tree.h"

/*
 * Table level full scan.
 *
 * Every data source is walked at once and merged with a binary heap:
 *
 *	active in-memory tree  -> frozen in-memory tree ->
 *	    ondisk trees, newest first
 *
 * A key that appears in more than one source is reported once, using the
 * version from the most recent source.  Deleted keys (tombstones) win
 * over older live versions, so a key that was deleted is not reported
 * at all -- exactly what table->get() does when it turns VAN_DELETED
 * into VAN_NOTFOUND.
 *
 * No cursor object is handed out: the state below lives entirely inside
 * this file and is torn down before traverse returns.
 *
 * Ranges are optional, see VAN_TRAVERSE_OPT.  When one is given every
 * source seeks instead of starting at its end, and the merge stops as soon
 * as the leading key leaves the range.  A range that cannot match anything
 * returns 0 without calling the callback -- "no rows" is not an error.
 *
 * Isolation is a hybrid, see the design notes:
 *   - everything that existed when the scan started is reported;
 *   - writes that land in the active tree while the scan runs are not
 *     guaranteed to be reported;
 *   - ondisk trees produced by a dump while the scan runs are not added;
 *   - no global lock is held while scanning, so dump / merge / freeze
 *     keep running.
 */

/*
 * One source of the merge.
 *
 * The sources are held in priority order: index 0 is the most recent.
 * That is why there is no separate priority field -- a lower index
 * always wins.  The heap relies on this when two sources sit on the
 * same key.
 */
struct _van_traverse_src {
	int		is_inmem;	/* 1 = in-memory tree, 0 = ondisk */

	/* The tree is only kept so the reference can be released. */
	INMEM_TREE	*im_tree;
	ONDISK_TREE	*od_tree;

	/* Only one of the two cursors is set, see is_inmem. */
	INMEM_TREECURSOR	*im_cur;
	ONDISK_TREECURSOR	*od_cur;

	/*
	 * This source's own key and value.
	 *
	 * The flags must be VAN_REALLOC.  With flags == 0 the tree's ret
	 * function does "*rdatum = *dat", which aliases our datum onto
	 * the cursor's internal buffer -- the next getraw overwrites it
	 * and every comparison in the merge would read a stale key.
	 * With VAN_MALLOC the ret function allocates a fresh buffer on
	 * every call and overwrites dst->data, leaking the previous one.
	 * VAN_REALLOC copies into a buffer we own and grows it in place.
	 *
	 * These datums are embedded in the array, so they must be
	 * released with _van_datum_copyfree() (frees data only), never
	 * with _van_datum_free() (which would free the datum itself).
	 */
	VAN_DATUM	key;
	VAN_DATUM	value;

	int		valid;		/* 1 = key/value hold a live item */
	int		deleted;	/* 1 = that item is a tombstone */
};

struct _van_traverse {
	VAN_TABLE		*table;
	VAN_TRAVERSE_FUNC	 func;
	void			*arg;
	VAN_DATUM_CMPFUNC	 cmp;
	int			 backward;

	/*
	 * Optional range, see VAN_TRAVERSE_OPT.
	 *
	 * The caller always hands over logical boundaries: start is where
	 * the traversal begins, end is where it stops.  Backward therefore
	 * receives them the other way round (start is the highest key),
	 * and every use below mirrors on tv->backward.  Both NULL means a
	 * plain full scan, exactly as before ranges existed.
	 */
	const VAN_DATUM		*start;		/* NULL = unbounded below */
	const VAN_DATUM		*end;		/* NULL = unbounded above */
	int			 start_inc;
	int			 end_inc;

	struct _van_traverse_src	*srcs;
	size_t			 nsrcs;

	/* Binary heap of indices into srcs[]. */
	size_t			*heap;
	size_t			 heapsize;
};

/*
 * Reserve the next source slot.  Capacity was sized up front, so this
 * cannot fail and the caller can take the tree reference immediately
 * after without an error path in between.
 */
static struct _van_traverse_src *trav_reserve(struct _van_traverse *tv) {
	struct _van_traverse_src *src;

	src = &tv->srcs[tv->nsrcs++];
	memset(src, 0, sizeof(*src));
	src->key.flags = VAN_REALLOC;
	src->value.flags = VAN_REALLOC;

	return (src);
}

/*
 * Read the next (or first) item of a source.
 *
 * getraw is used instead of get on purpose: get adds VAN_SKIP_DELETE and
 * hides tombstones, which would let an older live version of a deleted
 * key resurface.  Tombstones have to stay visible so the merge can
 * suppress the whole key.
 */
static int trav_fetch(struct _van_traverse *tv, size_t i, flags_t flag) {
	struct _van_traverse_src *src;
	int ret;

	src = &tv->srcs[i];
	if (src->is_inmem)
		ret = src->im_cur->getraw(src->im_cur, &src->key,
		    &src->value, flag);
	else
		ret = src->od_cur->getraw(src->od_cur, &src->key,
		    &src->value, flag);

	if (ret == 0) {
		src->valid = 1;
		src->deleted = 0;
		return (0);
	}
	if (ret == VAN_DELETED) {
		/*
		 * A tombstone.  The key was filled in, the value was
		 * not, and that is all the merge needs.
		 */
		src->valid = 1;
		src->deleted = 1;
		return (0);
	}
	/*
	 * VAN_NOTFOUND: the source is exhausted.
	 * VAN_STALE:    the tree is gone, for instance the active tree
	 *               was frozen and freed underneath us.  Treat it
	 *               the same way.
	 */
	if (ret == VAN_NOTFOUND || ret == VAN_STALE) {
		src->valid = 0;
		src->deleted = 0;
		return (0);
	}

	src->valid = 0;
	return (ret);
}

/*
 * Order two sources.  The smaller one sits at the top of the heap.
 *
 * When both hold the same key the lower index wins, which is the more
 * recent version -- that is what makes "report the newest version" fall
 * out of the heap ordering instead of needing a tie break later.
 */
static int trav_less(struct _van_traverse *tv, size_t a, size_t b) {
	int c;

	c = tv->cmp(&tv->srcs[a].key, &tv->srcs[b].key);
	if (c != 0)
		return (tv->backward ? c > 0 : c < 0);

	return (a < b);
}

/*
 * Only sift down is needed: the heap is rebuilt from scratch after every
 * reported key, see trav_heap_build().  There is no insert path, so
 * there is no sift up.
 */
static void trav_sift_down(struct _van_traverse *tv, size_t i) {
	size_t left, right, small;
	size_t tmp;

	for (;;) {
		left = 2 * i + 1;
		right = left + 1;
		small = i;

		if (left < tv->heapsize &&
		    trav_less(tv, tv->heap[left], tv->heap[small]))
			small = left;
		if (right < tv->heapsize &&
		    trav_less(tv, tv->heap[right], tv->heap[small]))
			small = right;
		if (small == i)
			break;
		tmp = tv->heap[i];
		tv->heap[i] = tv->heap[small];
		tv->heap[small] = tmp;
		i = small;
	}
}

/*
 * Rebuild the heap from every source that still holds an item.
 *
 * Several sources can move in one round (all of them sit on the key that
 * was just reported), so repairing the heap one entry at a time is easy
 * to get wrong.  The number of sources is small, so rebuilding is cheap
 * and far easier to reason about.
 */
static void trav_heap_build(struct _van_traverse *tv) {
	size_t i;

	tv->heapsize = 0;
	for (i = 0; i < tv->nsrcs; i++)
		if (tv->srcs[i].valid)
			tv->heap[tv->heapsize++] = i;

	/*
	 * Bottom up: every entry from heapsize / 2 on is already a leaf.
	 */
	if (tv->heapsize > 1) {
		i = tv->heapsize / 2;
		while (i > 0) {
			i--;
			trav_sift_down(tv, i);
		}
	}
}

/*
 * Does this key still fall inside the requested range?
 *
 * Both bounds are checked in every call, which keeps the two directions
 * symmetrical: whichever way the traversal walks, a key has to stay on the
 * far side of start (the bound it began at) and on the near side of end
 * (the bound it stops at).  Backward simply swaps which comparison means
 * "beyond".
 *
 * Called from trav_seek() when a source has just been positioned, and once
 * per key in the merge loop.
 */
static int trav_inrange(struct _van_traverse *tv, const VAN_DATUM *key) {
	int c;

	if (tv->start != NULL) {
		c = tv->cmp(key, tv->start);
		if (tv->backward) {
			/* start is the high end: nothing may sit above it. */
			if (tv->start_inc ? (c > 0) : (c >= 0))
				return (0);
		} else {
			if (tv->start_inc ? (c < 0) : (c <= 0))
				return (0);
		}
	}
	if (tv->end != NULL) {
		c = tv->cmp(key, tv->end);
		if (tv->backward) {
			/* end is the low end: nothing may fall below it. */
			if (tv->end_inc ? (c < 0) : (c <= 0))
				return (0);
		} else {
			if (tv->end_inc ? (c > 0) : (c >= 0))
				return (0);
		}
	}

	return (1);
}

/*
 * Position a source for the first time.
 *
 * Without a range this is the old VAN_FIRST / VAN_LAST pair.
 *
 * With a range the search always goes through VAN_SET_RANGE, and both
 * trees implement that flag as a lower bound: it lands on the first key
 * that is >= the search key, whatever direction the cursor is walked in
 * afterwards.  Forward that is exactly what a start bound needs.  Backward
 * it is not, because there we want the last key that is still inside the
 * start bound, so the landing point is corrected below.
 */
static int trav_seek(struct _van_traverse *tv, size_t i) {
	struct _van_traverse_src *src;
	int ret, c;

	src = &tv->srcs[i];

	if (tv->start == NULL)
		return (trav_fetch(tv, i, tv->backward ? VAN_LAST : VAN_FIRST));

	/*
	 * Every tree's getraw takes the search key in through the very same
	 * datum it later fills with the key it landed on, so prime our own
	 * key buffer with a copy of the start key and read the landing
	 * point back out of it.  src->key uses VAN_REALLOC, which is what
	 * lets _van_datum_copy() grow it in place.
	 */
	ret = _van_datum_copy(tv->start, &src->key);
	if (ret != 0)
		return (ret);

	ret = trav_fetch(tv, i, VAN_SET_RANGE);
	if (ret != 0)
		return (ret);

	if (!tv->backward) {
		if (!src->valid)
			return (0);
		/*
		 * The lower bound is what forward wants.  The only fixup is
		 * an exclusive start that landed exactly on the boundary
		 * key, which has to be stepped over.
		 */
		if (!tv->start_inc && tv->cmp(&src->key, tv->start) == 0)
			return (trav_fetch(tv, i, VAN_NEXT));
		return (0);
	}

	/*
	 * Backward, three cases after the lower bound search:
	 *
	 *   nothing is >= start -> every key of this source already lies
	 *                          below start, so begin at its last key;
	 *   it landed above start -> step back onto the largest key that is
	 *                          still within the bound.  Every key below
	 *                          a lower bound is below start, so one
	 *                          step is enough;
	 *   it landed on start  -> keep it when start is inclusive,
	 *                          otherwise step back as above.
	 */
	if (!src->valid)
		return (trav_fetch(tv, i, VAN_LAST));

	c = tv->cmp(&src->key, tv->start);
	if (c == 0 && tv->start_inc)
		return (0);

	return (trav_fetch(tv, i, VAN_PREV));
}

/*
 * Release everything a traversal holds.
 *
 * Two references were taken per tree and both have to go: the manual one
 * taken before the cursor was created, and the one the cursor took for
 * itself.  The cursor has to be cleaned up first, then the manual
 * reference is dropped.
 */
static void trav_cleanup(struct _van_traverse *tv) {
	struct _van_traverse_src *src;
	size_t i;

	for (i = 0; i < tv->nsrcs; i++) {
		src = &tv->srcs[i];
		if (src->is_inmem) {
			if (src->im_cur != NULL) {
				(void)src->im_cur->cleanup(src->im_cur);
				src->im_cur = NULL;
			}
			if (src->im_tree != NULL) {
				atomic_fetch_sub(&src->im_tree->refcount, 1);
				src->im_tree = NULL;
			}
		} else {
			if (src->od_cur != NULL) {
				(void)src->od_cur->cleanup(src->od_cur);
				src->od_cur = NULL;
			}
			if (src->od_tree != NULL) {
				atomic_fetch_sub(&src->od_tree->refcount, 1);
				src->od_tree = NULL;
			}
		}
		(void)_van_datum_copyfree(&src->key);
		(void)_van_datum_copyfree(&src->value);
	}
	tv->nsrcs = 0;
	tv->heapsize = 0;
}

/*
 * Attach an in-memory tree (active or frozen) as a source.
 */
static int trav_add_inmem(struct _van_traverse *tv, INMEM_TREE *tree) {
	struct _van_traverse_src *src;

	src = trav_reserve(tv);
	src->is_inmem = 1;
	src->im_tree = tree;
	/*
	 * Manual reference, taken before the cursor exists.  It covers
	 * the window between reading table->active / table->frozen and
	 * creating the cursor; a freeze can swap the pointer in there.
	 * The cursor takes a second reference for its own lifetime, see
	 * trav_cleanup() where both are dropped.
	 */
	atomic_fetch_add(&tree->refcount, 1);

	return (tree->cursor(tree, &src->im_cur, 0));
}

int _van_table_traverse(VAN_TABLE *table, flags_t flags,
    const VAN_TRAVERSE_OPT *opt, VAN_TRAVERSE_FUNC func, void *arg) {
	struct _van_traverse tv;
	struct _van_traverse_src *src;
	size_t *heap;
	size_t odt_top, capacity, i;
	INMEM_TREE *active, *frozen;
	ONDISK_TREE *odtree;
	VAN_TABLE_ODT_ITEM *item;
	flags_t nextflag;
	int empty, cmp;
	int ret, ret1;

	if (table == NULL || func == NULL)
		return (VAN_INVALID);
	if (table->cmp_func == NULL)
		return (VAN_INVALID);

	memset(&tv, 0, sizeof(tv));
	/*
	 * Must precede any goto done: the empty range check below can reach
	 * the cleanup before heap has been allocated, and the cleanup frees
	 * it unconditionally.
	 */
	heap = NULL;
	tv.table = table;
	tv.func = func;
	tv.arg = arg;
	tv.cmp = table->cmp_func;
	tv.backward = LF_ISSET(VAN_TRAVERSE_BACKWARD) ? 1 : 0;

	/*
	 * opt == NULL leaves every field below zeroed, which is the plain
	 * full scan this function has always done.
	 */
	if (opt != NULL) {
		tv.start = opt->start_key;
		tv.end = opt->end_key;
		tv.start_inc = opt->start_inclusive ? 1 : 0;
		tv.end_inc = opt->end_inclusive ? 1 : 0;
	}

	/*
	 * A range that cannot match anything selects no rows, and that is
	 * not an error: report nothing, leave the callback untouched and
	 * return 0.
	 *
	 * start beyond end is empty whichever way we walk -- the boundaries
	 * are logical ones, so it reads "start > end" forward and
	 * "start < end" backward.  Touching end points need both extremes
	 * to be inclusive to yield anything at all.
	 */
	if (tv.start != NULL && tv.end != NULL) {
		cmp = tv.cmp(tv.start, tv.end);
		empty = tv.backward ? (cmp < 0) : (cmp > 0);
		if (!empty && cmp == 0 && !(tv.start_inc && tv.end_inc))
			empty = 1;
		if (empty) {
			ret = 0;
			goto done;
		}
	}

	/*
	 * Snapshot the ondisk tree count.  Anything dumped from here on
	 * is published with odt_top++ and is therefore not part of this
	 * scan.
	 */
	VAN_MUTEX_LOCK(table, odt_mtx, ret);
	CHK_ERR(ret);
	odt_top = table->odt_top;
	VAN_MUTEX_UNLOCK(table, odt_mtx, ret1);
	SET_RET(ret1, ret);
	CHK_ERR(ret);

	/*
	 * At most every ondisk tree plus active and frozen.  odt_top is
	 * fixed from here, so no growth is ever needed.
	 */
	capacity = odt_top + 2;
	ret = _van_calloc(NULL, capacity, sizeof(*tv.srcs), &tv.srcs);
	CHK_ERR(ret);
	ret = _van_calloc(NULL, capacity, sizeof(*tv.heap), &heap);
	CHK_ERR(ret);
	tv.heap = heap;

	/* Sources are added newest first, index 0 has the highest priority. */
	active = table->active;
	if (active != NULL) {
		ret = trav_add_inmem(&tv, active);
		CHK_ERR(ret);
	}
	frozen = table->frozen;
	if (frozen != NULL) {
		ret = trav_add_inmem(&tv, frozen);
		CHK_ERR(ret);
	}

	for (i = odt_top; i > 0; i--) {
		item = &table->odt_items[i - 1];

		VAN_MUTEX_LOCK(item, mutex, ret);
		CHK_ERR(ret);
		/*
		 * Choosing the tree here follows the get path exactly:
		 * unused items are skipped, and a merged item serves the
		 * merged tree from trees[1] once it exists.
		 */
		if (F_ISSET(item, VAN_ITEM_UNUSED) ||
		    item->trees[0] == NULL) {
			VAN_MUTEX_UNLOCK(item, mutex, ret1);
			SET_RET(ret1, ret);
			continue;
		}
		odtree = item->trees[0];
		if (F_ISSET(item, VAN_ITEM_MERGED) &&
		    item->trees[1] != NULL)
			odtree = item->trees[1];

		src = trav_reserve(&tv);
		src->is_inmem = 0;
		src->od_tree = odtree;
		/*
		 * Manual reference, taken while the item lock is held.
		 * A merge finishing concurrently clears the item under
		 * this same lock and then closes the tree; the reference
		 * is what keeps it alive until we are done with it.  The
		 * cursor takes a second reference below.
		 */
		atomic_fetch_add(&odtree->refcount, 1);
		VAN_MUTEX_UNLOCK(item, mutex, ret1);
		SET_RET(ret1, ret);
		CHK_ERR(ret);

		ret = odtree->cursor(odtree, &src->od_cur, 0);
		CHK_ERR(ret);
	}

	/*
	 * Prime every source, then build the heap.
	 *
	 * trav_seek() collapses to VAN_FIRST / VAN_LAST when no start key
	 * was given, so the full scan path keeps doing what it always did.
	 */
	nextflag = tv.backward ? VAN_PREV : VAN_NEXT;
	for (i = 0; i < tv.nsrcs; i++) {
		ret = trav_seek(&tv, i);
		CHK_ERR(ret);
	}
	trav_heap_build(&tv);

	ret = 0;
	while (tv.heapsize > 0) {
		size_t top;

		top = tv.heap[0];

		/*
		 * Range scan: once the leading key has walked past the far
		 * end of the range nothing can be reported any more.  The
		 * heap always hands out the next key in traversal order, so
		 * every other source is past it too and stopping here loses
		 * nothing.
		 *
		 * A key outside the range is never handed to the callback,
		 * but it still takes part in the merge below so a tombstone
		 * keeps suppressing older versions of its key.
		 */
		if (tv.start != NULL || tv.end != NULL) {
			if (!trav_inrange(&tv, &tv.srcs[top].key)) {
				ret = 0;
				goto done;
			}
		}

		if (!tv.srcs[top].deleted) {
			ret = tv.func(tv.arg, &tv.srcs[top].key,
			    &tv.srcs[top].value);
			/*
			 * Non zero means the caller wants to stop.  Pass
			 * the value straight back so the caller can tell
			 * a normal end from an early stop.
			 */
			if (ret != 0)
				goto done;
		}

		/*
		 * Every source sitting on this key has to move, otherwise
		 * the older versions would be reported again on the next
		 * rounds.  The top source moves last: its key is what the
		 * others are compared against, and moving reuses that
		 * buffer.
		 */
		for (i = 0; i < tv.nsrcs; i++) {
			if (i == top || !tv.srcs[i].valid)
				continue;
			if (tv.cmp(&tv.srcs[i].key,
			    &tv.srcs[top].key) != 0)
				continue;
			ret = trav_fetch(&tv, i, nextflag);
			CHK_ERR(ret);
		}
		ret = trav_fetch(&tv, top, nextflag);
		CHK_ERR(ret);

		trav_heap_build(&tv);
	}

done:
err:
	trav_cleanup(&tv);

	if (tv.srcs != NULL) {
		_van_free(NULL, tv.srcs);
		tv.srcs = NULL;
	}
	if (heap != NULL)
		_van_free(NULL, heap);

	return (ret);
}
