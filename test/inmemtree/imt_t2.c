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
/* Multi-threaded inmemtree test */

/*
 * What the populate threads get: the tree plus the slice of the test data
 * they have to put.
 */
typedef struct {
	INMEM_TREE *tree;
	TESTDATA    td;
} imt_t2_pop_t;

static void *imt_t2_1_popthread(void *obj) {
	imt_t2_pop_t *dataobj;
	INMEM_TREE *tree;
	int ret;

	dataobj = (imt_t2_pop_t *)obj;
	tree = dataobj->tree;
	ret = populate_inmemtree(tree, &dataobj->td);
	CHK_RET(ret, "populate_inmemtree");

	return (NULL);
}

/*
 * One slice of the update or of the delete phase below.
 *
 * Which records a phase works on is decided by their index in the whole
 * data set (i % 3), but a thread only sees a piece of the arrays, so it
 * carries the index of its first record to work the modulo out.
 */
typedef struct {
	INMEM_TREE *tree;
	TESTDATA    td;
	size_t      base;   /* index of td.records[0] in the whole set */
	int         mod;    /* act on the records whose index is this % 3 */
} imt_t2_op_t;

/*
 * Split the data set into n contiguous slices, the last one taking the
 * records the division leaves over.
 */
static void imt_t2_1_slices(imt_t2_op_t *objs, int n, INMEM_TREE *tree,
    TESTDATA *td, int mod) {
	int i, each;

	each = (int)td->count / n;
	for (i = 0; i < n; i++) {
		objs[i].tree = tree;
		TESTDATA_SLICE(&objs[i].td, td, (size_t)(i * each),
		    (size_t)each);
		objs[i].base = (size_t)(i * each);
		objs[i].mod = mod;
	}
	objs[n - 1].td.count += (size_t)((int)td->count - each * n);
}

/*
 * Update: the same key as the record that is already there, with the value
 * that was generated for it before the threads were started.
 */
static void *imt_t2_1_updthread(void *obj) {
	imt_t2_op_t *d;
	VAN_DATUM k, v;
	size_t j;
	int ret;

	d = (imt_t2_op_t *)obj;
	for (j = 0; j < d->td.count; j++) {
		if ((int)((d->base + j) % 3) != d->mod)
			continue;

		memset(&k, 0, sizeof(k));
		memset(&v, 0, sizeof(v));
		k.data = d->td.records[j].key;
		k.size = d->td.records[j].ksize;
		v.data = d->td.records[j].value;
		v.size = d->td.records[j].vsize;
		ret = d->tree->put(d->tree, &k, &v, 0);
		CHK_RET(ret, "tree->put (update)");
	}

	return (NULL);
}

static void *imt_t2_1_delthread(void *obj) {
	imt_t2_op_t *d;
	VAN_DATUM k;
	size_t j;
	int ret;

	d = (imt_t2_op_t *)obj;
	for (j = 0; j < d->td.count; j++) {
		if ((int)((d->base + j) % 3) != d->mod)
			continue;

		memset(&k, 0, sizeof(k));
		k.data = d->td.records[j].key;
		k.size = d->td.records[j].ksize;
		ret = d->tree->del(d->tree, &k, 0);
		CHK_RET(ret, "tree->del");
	}

	return (NULL);
}

/*
 * Check every record once the update and the delete phase are done:
 *
 *      i % 3 == 0  untouched, still holds its original value;
 *      i % 3 == 1  updated, must hold the value generated for it;
 *      i % 3 == 2  deleted, the tree must report VAN_NOTFOUND.
 *
 * verify_inmemtree_with_expected() is not used here: it only prints a
 * record whose return value differs from the expected one and then goes on,
 * so a wrong result would not fail the test.
 */
static int imt_t2_1_verify_ops(INMEM_TREE *tree, TESTDATA *td) {
	VAN_DATUM k, v;
	size_t i, alive, gone;
	int ret, expect;

	alive = 0;
	gone = 0;
	for (i = 0; i < td->count; i++) {
		memset(&k, 0, sizeof(k));
		memset(&v, 0, sizeof(v));
		k.data = td->records[i].key;
		k.size = td->records[i].ksize;

		ret = tree->get(tree, &k, &v, 0);
		expect = ((i % 3) == 2) ? VAN_NOTFOUND : 0;
		TEST_ASSERT2(ret == expect,
		    "record %zu (i %% 3 = %d): expected %d, got %d",
		    i, (int)(i % 3), expect, ret);

		if (ret == 0) {
			TEST_ASSERT2(v.size == td->records[i].vsize,
			    "record %zu: value size %lu, expected %lu", i,
			    (unsigned long)v.size,
			    (unsigned long)td->records[i].vsize);
			TEST_ASSERT2(memcmp(v.data, td->records[i].value,
			    v.size) == 0,
			    "record %zu: value does not match", i);
			alive++;
		} else
			gone++;
	}

	fprintf(stderr, " -- %lu records present, %lu deleted --\n",
	    (unsigned long)alive, (unsigned long)gone);

	return (0);
}

/*
 * Put every record using several threads, then update a third of them and
 * delete another third, each of those two phases again using several
 * threads.
 */
int imt_t2_1() {
#define IMT_T2_1_NUM 5     /* threads of the initial populate */
#define IMT_T2_1_OPNUM 3   /* threads of the update and of the delete */
	VAN_TABLE table;
	INMEM_TREE *tree;
	pthread_t threads[IMT_T2_1_NUM];
	pthread_t opthreads[IMT_T2_1_OPNUM];
	imt_t2_pop_t objs[IMT_T2_1_NUM], *objp;
	imt_t2_op_t opobjs[IMT_T2_1_OPNUM];
	size_t each_cnt;
	int i, ret, cnt;
	void *rptr;
	time_t t1, t2;
	TESTDATA td;

	memset(&table, 0, sizeof(VAN_TABLE));
	ret = van_inmemtree_create(&table, &table.active);
	CHK_RET(ret, "van_inmemtree_create");
	tree = table.active;
	/* we set this to avoid inmemtree to dump leaves,
	 since we don't want to test that here */
	tree->threshold = 1*GIGABYTE;

	memset(threads, 0, sizeof(threads));
	cnt = 100000;
	TESTDATA_INIT(&td, (size_t)cnt);
	each_cnt = cnt / IMT_T2_1_NUM;
	memset(objs, 0, sizeof(objs));
	for (i = 0; i < IMT_T2_1_NUM; i++) {
		objp = &objs[i];
		objp->tree = tree;
		TESTDATA_SLICE(&objp->td, &td, (size_t)(i * each_cnt),
		    each_cnt);
	}

	(void)generate_string_testdata(&td, 5, 15, 10, 20);

	fprintf(stderr, "============ 1 Try to put %lu records using %d threads =========\n",
	    (unsigned long)cnt, IMT_T2_1_NUM);
	for (i = 0; i < IMT_T2_1_NUM; i++) {
		ret = pthread_create(&threads[i], NULL,
		    imt_t2_1_popthread, &objs[i]);
	}

	for (i = 0; i < IMT_T2_1_NUM; i++) {
		ret = pthread_join(threads[i], &rptr);
	}

	tree->statprint(tree, stderr, "inemtree stat: ");

	fprintf(stderr, "========== 2 Get using tree->get() ============\n");
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_data(tree, &td);
	CHK_RET(ret, "verify_inmemtree_data");
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");

	fprintf(stderr, "========== 3 Get using cursor->get(VAN_SET) ============\n");
	van_alloc_stat_reset();
	van_rwlock_stat_reset();
	ret = verify_inmemtree_data_withcursor(tree, &td);
	CHK_RET(ret, "verify_inmemtree_data_withcursor");
	van_rwlock_stat_print(stderr, " ");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");

	/*
	 * 4 Give a new value to every record with i % 3 == 1.
	 *
	 * Done before the threads start: getrandomstring2() is built on
	 * rand(), so it is better kept to one thread.
	 */
	fprintf(stderr,
	    "========== 4 New value for the records with i %% 3 == 1 ==\n");
	for (i = 1; i < cnt; i += 3) {
		char vbuf[MAX_BUF_LEN];
		char *p;
		int len;

		len = getrandomstring2(vbuf, 10, 20);
		TEST_ASSERT(len > 0 && len < MAX_BUF_LEN);
		/*
		 * The buffer the old value came in is exactly the size of
		 * that value, so take a new one instead of writing over it.
		 */
		p = (char *)malloc((size_t)len + 1);
		TEST_ASSERT(p != NULL);
		memcpy(p, vbuf, (size_t)len);
		p[len] = '\0';
		free(td.records[i].value);
		td.records[i].value = p;
		/* Like the initial data, the value carries its NUL. */
		td.records[i].vsize = (size_t)len + 1;
	}

	fprintf(stderr,
	    "========== 5 Update them using %d threads =========\n",
	    IMT_T2_1_OPNUM);
	imt_t2_1_slices(opobjs, IMT_T2_1_OPNUM, tree, &td, 1);
	for (i = 0; i < IMT_T2_1_OPNUM; i++) {
		ret = pthread_create(&opthreads[i], NULL,
		    imt_t2_1_updthread, &opobjs[i]);
	}
	for (i = 0; i < IMT_T2_1_OPNUM; i++) {
		ret = pthread_join(opthreads[i], &rptr);
	}

	fprintf(stderr,
	    "========== 6 Delete the records with i %% 3 == 2 using %d threads =========\n",
	    IMT_T2_1_OPNUM);
	imt_t2_1_slices(opobjs, IMT_T2_1_OPNUM, tree, &td, 2);
	for (i = 0; i < IMT_T2_1_OPNUM; i++) {
		ret = pthread_create(&opthreads[i], NULL,
		    imt_t2_1_delthread, &opobjs[i]);
	}
	for (i = 0; i < IMT_T2_1_OPNUM; i++) {
		ret = pthread_join(opthreads[i], &rptr);
	}

	tree->statprint(tree, stderr, "inemtree stat after update/delete: ");

	fprintf(stderr,
	    "========== 7 Verify the update and the delete ============\n");
	ret = imt_t2_1_verify_ops(tree, &td);
	CHK_RET(ret, "imt_t2_1_verify_ops");

	(void)free_string_testdata(&td);

	fprintf(stderr, "========== 8 Destroy the tree ============\n");
	time(&t1);
	van_alloc_stat_reset();
	ret = tree->destroy(tree);
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
	CHK_RET(ret, "tree->destroy");
	fprintf(stderr, "**** Malloc Statistics ****\n");
	van_alloc_stat_print(stderr, " ");

	return (ret);
}
