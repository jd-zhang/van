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

static char LETTERS[] = {
	'a', 'b', 'c', 'd', 'e', 'f', 'g',
	'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't',
	'u', 'v', 'w', 'x', 'y', 'z',
	'A', 'B', 'C', 'D', 'E', 'F', 'G',
	'H', 'I', 'J', 'K', 'L', 'M', 'N',
	'O', 'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y', 'Z', '_',
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
};
static int LETTERS_SIZE = sizeof(LETTERS)/sizeof(LETTERS[0]);

static int check_duplicate(void *obj, size_t maxksz, size_t *totalp,
    size_t *dupcountp, int otype);
static int populate_data(void *obj, TESTDATA *td, int otype);
static int delete_data_with_expected(void *obj, TESTDATA *td,
    int otype, int start, int step, int expected) ;
static int verify_data_withcursor(void *obj, TESTDATA *td, int otype);
static int verify_cursor_direction(void *obj, flags_t direction,
    flags_t cflags, size_t *rcntp, int otype);
static int verify_cursor_border(void *obj, flags_t direction,
    flags_t cflags, int otype);

int is_cygwin(){
#ifdef __CYGWIN__
	return 1;
#endif
	return 0;
}

int getrandomstring(char *buf, int fixedlen) {
	int len = 0, i;
	len = fixedlen;
	for (i = 0; i < len; i++) {
		buf[i] = LETTERS[rand() % LETTERS_SIZE];
	}
	return len;
}

int getrandomstring2(char *buf, int minlen, int maxlen) {
	int len = 0, i;

	if (minlen == maxlen)
		len = maxlen;
	else
		len = minlen + rand() % (maxlen - minlen + 1);

	if (len == 0)
		len = 1;
	for (i = 0; i < len; i++) {
		buf[i] = LETTERS[rand() % LETTERS_SIZE];
	}
	return len;
}

int init_buf(char *buf, int len) {
	int i = 0;

	for(i = 0; i < len; i++) {
		buf[i] = LETTERS[i % LETTERS_SIZE];
	}
	return len;
}

unsigned int get_2power(unsigned int indx) {
	return 1 << indx;
}


char *getstrtime(char *buf, int len) {
	time_t cur_time;
	struct tm *tmp = NULL;
	(void)time(&cur_time);
	tmp = localtime(&cur_time);
	strftime(buf, len, "%Y:%m:%d:%H:%M:%S", tmp);
	return buf;
}

char *getstrtime2(char *buf, int len, time_t t1) {
	struct tm *tmp = NULL;
	tmp = localtime(&t1);
	strftime(buf, len, "%Y:%m:%d:%H:%M:%S", tmp);
	return buf;
}

static int check_duplicate(void *obj, size_t maxksz, size_t *totalp,
    size_t *dupcountp, int otype) {
	char *kbuf;
	VAN_DATUM key, value, keyprev;
	INMEM_TREE *imtree;
	INMEM_TREECURSOR *imcursor;
	ONDISK_TREE *odtree;
	ONDISK_TREECURSOR *odcursor;
	VAN_TABLE *table;
	/* TODO: table cursor ? */
	int ret, ret1, start, cmp;
	size_t total, dupcount;
	time_t t1, t2;
	/* const char *info; */

	start = 0;
	total = 0;
	dupcount = 0;
	imcursor = NULL;
	odcursor = NULL;
	memset(&keyprev, 0, sizeof(VAN_DATUM));
	kbuf = (char *)malloc(maxksz);
	TEST_ASSERT(kbuf != NULL);
	keyprev.data = kbuf;

	imtree = (INMEM_TREE *)obj;
	odtree = (ONDISK_TREE *)obj;
	table = (VAN_TABLE *)obj;

	switch (otype) {
		case OP_INMEMTREE:
			ret = imtree->cursor(imtree, &imcursor, 0);
			break;
		case OP_ONDISKTREE:
			ret = odtree->cursor(odtree, &odcursor, 0);
			break;
		case OP_TABLE:
			ret = 0; /* TODO: Add table cursor test here */
			table = table;
			break;
		default:
			ret = van_error_print("BAD OP_TYPE");
			break;
	}
	if (ret != 0)
		goto err;

	time(&t1);
	for (;;) {
		memset(&key, 0, sizeof(VAN_DATUM));
		memset(&value, 0, sizeof(VAN_DATUM));
		switch (otype) {
			case OP_INMEMTREE:
				ret = imcursor->getraw(imcursor,
				    &key, &value, VAN_NEXT);
				break;
			case OP_ONDISKTREE:
				ret = odcursor->getraw(odcursor,
				    &key, &value, VAN_NEXT);
				break;
			case OP_TABLE:
				ret = 0;  /* TODO: table */
				break;
			/* No other types */
		}
		if (ret == VAN_NOTFOUND)
			break;
		if (ret != 0 && ret != VAN_DELETED)
			goto err;
		if (start && key.size == keyprev.size) {
			cmp = memcmp(key.data, keyprev.data, key.size);
			TEST_ASSERT(cmp >= 0);
			if (cmp == 0)
				dupcount++;
		}
		start = 1;
		memcpy(kbuf, key.data, key.size);
		keyprev.size = key.size;
		total++;
	}
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

	if (ret == VAN_NOTFOUND)
		ret = 0;
	if (totalp != NULL)
		*totalp = total;
	if (dupcountp != NULL)
		*dupcountp = dupcount;
err:
	if (imcursor != NULL || odcursor != NULL) {
		switch (otype) {
			case OP_INMEMTREE:
				ret1 = imcursor->cleanup(imcursor);
				break;
			case OP_ONDISKTREE:
				ret1 = odcursor->cleanup(odcursor);
				break;
			case OP_TABLE:
				ret1 = 0; /* TODO: For table */
				break;
		}
		SET_RET(ret1, ret);
	}
	free(kbuf);

	return (ret);
}

int check_ondisktree_duplicate(ONDISK_TREE *tree,
    size_t maxksz, size_t *totalp, size_t *dupcountp) {
	return check_duplicate(tree, maxksz, totalp,
	    dupcountp, OP_ONDISKTREE);
}

int check_inmemtree_duplicate(INMEM_TREE *tree,
    size_t maxksz, size_t *totalp, size_t *dupcountp) {
	return check_duplicate(tree, maxksz, totalp,
	    dupcountp, OP_INMEMTREE);
}

int check_table_duplicate(VAN_TABLE *table,
    size_t maxksz, size_t *totalp, size_t *dupcountp) {
	return check_duplicate(table, maxksz, totalp,
	    dupcountp, OP_TABLE);
}

static int populate_data_with_expected(void *obj, TESTDATA *td,
    int otype, int expected) {	
	size_t i;
	int ret, rret;
	VAN_DATUM key, value;
	time_t t1, t2;
	VAN_TABLE *table;
	INMEM_TREE *tree;
	const char *info;

	table = (VAN_TABLE *)obj;
	tree = (INMEM_TREE *)obj;
	rret = 0;

	time(&t1);
	for (i = 0; i < td->count; i++) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = td->records[i].key;
		key.size = td->records[i].ksize;
		value.data = td->records[i].value;
		value.size = td->records[i].vsize;
		switch (otype) {
			case OP_TABLE:
				info = "table->put";
				ret = table->put(table, &key, &value, 0);
				break;
			case OP_INMEMTREE:
				info = "tree->put";
				ret = tree->put(tree, &key, &value, 0);
				break;
			default:
				info = "Unknown op_type";
				ret = van_error_print("BAD OP_TYPE");
				break;
		}
		if (ret != expected) {
			fprintf(stderr, "loop:%d Expected return: %d, got: %d\n",
			    (int)i, expected, ret);
			if(rret == 0)
				rret = ret;
		}
		CHK_RET(ret != expected, info);

	}
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

	return (rret);
}


static int populate_data(void *obj, TESTDATA *td, int otype){
	return populate_data_with_expected(obj, td, otype, 0);
	}

int populate_table(VAN_TABLE *table, TESTDATA *td) {
	return populate_data(table, td, OP_TABLE);
}

int populate_table_with_expected(VAN_TABLE *table, TESTDATA *td,
    int expected) {
	return populate_data_with_expected(table, td, OP_TABLE, expected);
}

int populate_inmemtree(INMEM_TREE *tree, TESTDATA *td) {
	return populate_data(tree, td, OP_INMEMTREE);
}

int populate_inmemtree_with_expected(INMEM_TREE *tree, TESTDATA *td,
    int expected) {
	return populate_data_with_expected(tree, td, OP_INMEMTREE, expected);
}

static int delete_data_with_expected(void *obj, TESTDATA *td,
    int otype, int start, int step, int expected) {	
	size_t i;
	int ret, rret;
	VAN_DATUM key;
	time_t t1, t2;
	VAN_TABLE *table;
	INMEM_TREE *tree;
	const char *info;

	table = (VAN_TABLE *)obj;
	tree = (INMEM_TREE *)obj;
	rret = 0;

	time(&t1);
	for (i = (size_t)start; i < td->count; i+=(size_t)step) {
		memset(&key, 0, sizeof(key));
		key.data = td->records[i].key;
		key.size = td->records[i].ksize;
		switch (otype) {
			case OP_TABLE:
				info = "table->del";
				ret = table->del(table, &key, 0);
				break;
			case OP_INMEMTREE:
				info = "tree->del";
				ret = tree->del(tree, &key, 0);
				break;
			default:
				info = "Unknown op_type";
				ret = van_error_print("BAD OP_TYPE");
				break;
		}
		if (ret != expected) {
			fprintf(stderr, "Expected return: %d, got: %d\n",
			    expected, ret);
			if(rret == 0)
				rret = ret;
		}
		CHK_RET(ret != expected, info);
	}
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

	return (rret);
}

int delete_inmemtree_with_expected(INMEM_TREE *tree, TESTDATA *td,
	int start, int step, int expected) {
	return delete_data_with_expected(tree,
	    td, OP_INMEMTREE, start, step, expected);
}

int delete_inmemtree(INMEM_TREE *tree, TESTDATA *td, int step) {
	return delete_data_with_expected(tree,
	    td, OP_INMEMTREE, 0, step, 0);
}

int delete_table_with_expected(VAN_TABLE *table, TESTDATA *td,
	int start, int step, int expected) {
	return delete_data_with_expected(table,
	    td, OP_TABLE, start, step, expected);
}

int delete_table(VAN_TABLE *table, TESTDATA *td, int step) {
	return delete_data_with_expected(table,
	    td, OP_TABLE, 0,step, 0);
}

/*
 * Print a datum in a readable way:
 * - If it looks like a string (NUL-terminated), print it as a string.
 * - Otherwise, print the bytes in hex.
 */
static void print_datum(FILE *f, const char *label, const VAN_DATUM *datum) {
	size_t i;
	const unsigned char *p;

	fprintf(f, "%s", label);
	if (datum == NULL || datum->data == NULL) {
		fprintf(f, "= (null)\n");
		return;
	}
	if (datum->size == 0) {
		fprintf(f, "= (empty, size=0)\n");
		return;
	}
	p = (const unsigned char *)datum->data;
	if (p[datum->size - 1] == '\0') {
		fprintf(f, "= \"%.*s\" (str, size=%zu)\n",
		    (int)datum->size - 1, (const char *)datum->data,
		    datum->size);
	} else {
		fprintf(f, "= hex[%zu]: ", datum->size);
		for (i = 0; i < datum->size; i++)
			fprintf(f, "%02x%s", p[i],
			    (i + 1 < datum->size) ? " " : "");
		fprintf(f, "\n");
	}
}

/*
verify the records using the inmemtree/odisktree/table get function,
and verify the records are the same as the input data.
*/
static int verify_data_with_expected(void *obj, TESTDATA *td, int otype,
    int start, int step, int expected) {
	INMEM_TREE *imtree;
	ONDISK_TREE *odtree;
	VAN_TABLE *table;
	int ret;
	size_t i;
	VAN_DATUM key, value;
	const char *info;
	time_t t1, t2;

	imtree = (INMEM_TREE *)obj;
	odtree = (ONDISK_TREE *)obj;
	table = (VAN_TABLE *)obj;

	time(&t1);
	for (i = (size_t)start; i < td->count; i+= (size_t)step) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = td->records[i].key;
		key.size = td->records[i].ksize;

		switch (otype) {
			case OP_INMEMTREE:
				info = "imtree->get";
				ret = imtree->get(imtree, &key, &value, 0);
				break;
			case OP_ONDISKTREE:
				info = "odtree->get";
				ret = odtree->get(odtree, &key, &value, 0);
				break;
			case OP_TABLE:
				info = "table->get";
				ret = table->get(table, &key, &value, 0);
				break;
			default:
				/* ERROR */
				info = "bad otype";
				ret = van_error_print("BAD OP_TYPE");
		}
		if (ret == 0) {
			VAN_ASSERT(value.size == td->records[i].vsize);
			VAN_ASSERT((memcmp(value.data, td->records[i].value,
			    value.size) == 0));
		}
		if (ret != expected) {
			VAN_DATUM expval, retval;
			char wherebuf[MAX_BUF_LEN];

			fprintf(stderr,
			    "==== verify_data_with_expected mismatch ====\n");
			fprintf(stderr,
			    "record indx: %zu, expected return: %d, got: %d (%s)\n",
			    i, expected, ret, info);

			memset(&expval, 0, sizeof(expval));
			if (expected == 0) {
				expval.data = td->records[i].value;
				expval.size = td->records[i].vsize;
				print_datum(stderr, "key", &key);
				print_datum(stderr, "expected value", &expval);
			} else {
				print_datum(stderr, "key", &key);
				fprintf(stderr, "expected value: (none, expected=%d)\n",
				    expected);
			}

			memset(&retval, 0, sizeof(retval));
			if (ret == 0) {
				retval.data = value.data;
				retval.size = value.size;
				print_datum(stderr, "returned value", &retval);
			} else {
				fprintf(stderr, "returned value: (none, ret=%d)\n",
				    ret);
			}

			switch (otype) {
				case OP_ONDISKTREE:
					if (odtree->file_path != NULL)
						fprintf(stderr, "odtree file: %s\n",
						    odtree->file_path);
					else
						fprintf(stderr,
						    "odtree file: (unknown)\n");
					break;
				case OP_TABLE:
					/*
					 * Re-search the key in the order
					 * active -> frozen -> ondisk trees,
					 * using table->test_get, and show
					 * where it is found (or not).
					 */
					if (table->test_get != NULL) {
						int ret2;

						memset(&retval, 0, sizeof(retval));
						wherebuf[0] = '\0';
						ret2 = table->test_get(table, &key,
						    &retval, 0, wherebuf,
						    sizeof(wherebuf));
						fprintf(stderr,
						    "second lookup(table->test_get): ret=%d, where=%s\n",
						    ret2, wherebuf);
						if (ret2 == 0)
							print_datum(stderr,
							    "value found at second lookup",
							    &retval);
					} else {
						fprintf(stderr,
						    "second lookup: table->test_get is NULL\n");
					}
					break;
				default:
					break;
			}
			fprintf(stderr,
			    "==== end of mismatch info ====\n");
		}
		/*
		CHK_RET_NOEXIT(ret != expected, info);
		if (ret != expected)
			break;
			*/
	}
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

	return (ret);
}

int verify_inmemtree_data(INMEM_TREE *tree, TESTDATA *td) {
	return verify_data_with_expected(tree, td, OP_INMEMTREE, 0, 1, 0);
}

int verify_inmemtree_with_expected(INMEM_TREE *tree, TESTDATA *td,
    int start, int step, int expected) {
	return verify_data_with_expected(tree, td,
	    OP_INMEMTREE, start, step, expected);
}

int verify_ondisktree_data(ONDISK_TREE *tree, TESTDATA *td) {
	return verify_data_with_expected(tree, td, OP_ONDISKTREE, 0, 1, 0);
}

int verify_ondisktree_with_expected(ONDISK_TREE *tree, TESTDATA *td,
    int start, int step, int expected) {
	return verify_data_with_expected(tree, td,
	    OP_ONDISKTREE, start, step, expected);
}

int verify_table_data(VAN_TABLE *table, TESTDATA *td) {
	return verify_data_with_expected(table, td, OP_TABLE, 0, 1, 0);
}

int verify_table_with_expected(VAN_TABLE *table, TESTDATA *td,
	int start, int step, int expected) {
	return verify_data_with_expected(table, td,
	    OP_TABLE, start, step, expected);
}

static int verify_data_withcursor(void *obj, TESTDATA *td, int otype) {
	INMEM_TREE *imtree;
	INMEM_TREECURSOR *imcursor;
	ONDISK_TREE *odtree;
	ONDISK_TREECURSOR *odcursor;
	VAN_TABLE *table;
	/* TODO: Add table cursor */
	int ret, ret1;
	size_t i;
	VAN_DATUM key, value;
	const char *info;
	time_t t1, t2;

	imcursor = NULL;
	odcursor = NULL;

	imtree = (INMEM_TREE *)obj;
	odtree = (ONDISK_TREE *)obj;
	table = (VAN_TABLE *)obj;

	switch (otype) {
		case OP_INMEMTREE:
			ret = imtree->cursor(imtree, &imcursor, 0);
			break;
		case OP_ONDISKTREE:
			ret = odtree->cursor(odtree, &odcursor, 0);
			break;
		case OP_TABLE:
			ret = 0; /* TODO: Add table cursor test here */
			table = table;
			break;
		default:
			ret = van_error_print("BAD OP_TYPE");
			break;
	}
	if (ret != 0)
		goto err;

	time(&t1);
	for (i = 0; i < td->count; i++) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = td->records[i].key;
		key.size = td->records[i].ksize;

		switch (otype) {
			case OP_INMEMTREE:
				info = "imcursor->get";
				ret = imcursor->get(imcursor, &key, &value, VAN_SET);
				break;
			case OP_ONDISKTREE:
				info = "odcursor->get";
				ret = odcursor->get(odcursor, &key, &value, VAN_SET);
				break;
			case OP_TABLE:
				info = "tablecursor->get";
				ret = 0;
				/* TODO: Add table cursor here */
				break;
			default:
				/* ERROR */
				info = "bad otype";
				ret = van_error_print("BAD OP_TYPE");
		}
		CHK_RET(ret, info);
		VAN_ASSERT(value.size == td->records[i].vsize);
		VAN_ASSERT((memcmp(value.data, td->records[i].value,
		    value.size) == 0));
	}
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

err:
	if (imcursor != NULL || odcursor != NULL) {
		switch (otype) {
			case OP_INMEMTREE:
				ret1 = imcursor->cleanup(imcursor);
				break;
			case OP_ONDISKTREE:
				ret1 = odcursor->cleanup(odcursor);
				break;
			case OP_TABLE:
				ret1 = 0; /* TODO: For table */
				break;
		}
		SET_RET(ret1, ret);
	}

	return (ret);
}

int verify_inmemtree_data_withcursor(INMEM_TREE *tree, TESTDATA *td) {
	return verify_data_withcursor(tree, td, OP_INMEMTREE);
}

int verify_ondisktree_data_withcursor(ONDISK_TREE *tree, TESTDATA *td) {
	return verify_data_withcursor(tree, td, OP_ONDISKTREE);
}

int verify_table_data_withcursor(VAN_TABLE *table, TESTDATA *td) {
	return verify_data_withcursor(table, td, OP_TABLE);
}

/*
 * Helpers shared by the cursor verification functions below.
 *
 * A table does not hand out a cursor, so every walk of a table goes through
 * table->traverse() instead.  The cursor direction flags map onto the
 * traversal like this:
 *
 *	VAN_NEXT, VAN_FIRST -> VAN_TRAVERSE_FORWARD
 *	VAN_PREV, VAN_LAST  -> VAN_TRAVERSE_BACKWARD
 */
static int traverse_backward(flags_t direction) {
	switch (direction & VAN_MASKMODE) {
		case VAN_PREV:
		case VAN_LAST:
			return (1);
		default:
			return (0);
	}
}

typedef struct {
	size_t    rcnt;
	int       backward;
	int       haspre;
	char      prebuf[MAX_BUF_LEN];
	VAN_DATUM prekey;
} dir_ctx_t;

/*
 * Count the records of a traversal and check that they keep arriving in
 * order.  Used as the table->traverse() callback.
 */
static int dir_cb(void *arg, const VAN_DATUM *key, const VAN_DATUM *value) {
	dir_ctx_t *c;
	int cmp;

	c = (dir_ctx_t *)arg;
	c->rcnt++;
	if (c->haspre) {
		cmp = van_datum_defcmp(key, &c->prekey);
		TEST_ASSERT2(c->backward ? (cmp < 0) : (cmp > 0),
		    "direction broken at record %lu",
		    (unsigned long)c->rcnt);
	}
	TEST_ASSERT(key->size <= sizeof(c->prebuf));
	memcpy(c->prebuf, key->data, key->size);
	c->prekey.data = c->prebuf;
	c->prekey.size = key->size;
	c->haspre = 1;

	return (0);
}

typedef struct {
	size_t    rcnt;
	int       backward;
	TESTDATA *td;
} dirfull_ctx_t;

/*
 * Same, but every record is compared with the expected one as well.
 *
 * The records are sorted ascending by key, so walking forward starts at
 * records[0] and walking backward starts at records[count - 1].
 */
static int dirfull_cb(void *arg, const VAN_DATUM *key,
    const VAN_DATUM *value) {
	dirfull_ctx_t *c;
	RECORD *rec;
	size_t idx, cnt;

	c = (dirfull_ctx_t *)arg;
	cnt = c->td->count;
	TEST_ASSERT2(c->rcnt < cnt,
	    "more records than expected (%lu)", (unsigned long)cnt);
	idx = c->backward ? (cnt - 1 - c->rcnt) : c->rcnt;
	rec = &c->td->records[idx];

	TEST_ASSERT2(key->size == rec->ksize,
	    "record %lu: key size %lu, expected %lu", (unsigned long)idx,
	    (unsigned long)key->size, (unsigned long)rec->ksize);
	TEST_ASSERT2(memcmp(key->data, rec->key, key->size) == 0,
	    "record %lu: key does not match", (unsigned long)idx);

	if (value != NULL && value->data != NULL) {
		TEST_ASSERT2(value->size == rec->vsize,
		    "record %lu: value size %lu, expected %lu",
		    (unsigned long)idx, (unsigned long)value->size,
		    (unsigned long)rec->vsize);
		TEST_ASSERT2(memcmp(value->data, rec->value,
		    value->size) == 0,
		    "record %lu: value does not match", (unsigned long)idx);
	}

	c->rcnt++;

	return (0);
}

typedef struct {
	int      got;
	char     kbuf[MAX_BUF_LEN];
	size_t   ksize;
	char     vbuf[MAX_BUF_LEN];
	size_t   vsize;
} first_ctx_t;

/*
 * Keep the first record of a traversal, then stop it.
 */
static int first_cb(void *arg, const VAN_DATUM *key,
    const VAN_DATUM *value) {
	first_ctx_t *c;

	c = (first_ctx_t *)arg;
	if (c->got)
		return (1);
	c->got = 1;

	TEST_ASSERT(key->size <= sizeof(c->kbuf));
	memcpy(c->kbuf, key->data, key->size);
	c->ksize = key->size;
	if (value != NULL && value->data != NULL) {
		TEST_ASSERT(value->size <= sizeof(c->vbuf));
		memcpy(c->vbuf, value->data, value->size);
		c->vsize = value->size;
	} else
		c->vsize = 0;

	/*
	 * A non zero return stops the traversal and is handed back to the
	 * caller of table->traverse(), see table_first_record().
	 */
	return (1);
}

static int table_first_record(VAN_TABLE *table, flags_t direction,
    first_ctx_t *ctx) {
	int ret;

	memset(ctx, 0, sizeof(*ctx));
	ret = table->traverse(table, traverse_backward(direction) ?
	    VAN_TRAVERSE_BACKWARD : VAN_TRAVERSE_FORWARD,
	    NULL, first_cb, ctx);
	/* first_cb stops the traversal with 1, that is not an error. */
	if (ret == 1)
		ret = 0;

	return (ret);
}

/*
 * Get the single record a direction yields on a fresh cursor, whatever the
 * object is: the first record walking forward for VAN_FIRST / VAN_NEXT, the
 * first record walking backward for VAN_LAST / VAN_PREV.
 *
 * The record is copied out, because the datum a cursor returns aliases its
 * own buffer and is gone once the cursor is cleaned up.
 */
static int border_record(void *obj, flags_t direction, flags_t cflags,
    int otype, char *kbuf, size_t *ksizep, char *vbuf, size_t *vsizep) {
	INMEM_TREE *imtree;
	INMEM_TREECURSOR *imcursor;
	ONDISK_TREE *odtree;
	ONDISK_TREECURSOR *odcursor;
	VAN_TABLE *table;
	VAN_DATUM key, value;
	first_ctx_t fctx;
	int ret, ret1;

	imtree = (INMEM_TREE *)obj;
	odtree = (ONDISK_TREE *)obj;
	table = (VAN_TABLE *)obj;
	imcursor = NULL;
	odcursor = NULL;
	memset(&key, 0, sizeof(key));
	memset(&value, 0, sizeof(value));
	*ksizep = 0;
	*vsizep = 0;

	if (otype == OP_TABLE) {
		ret = table_first_record(table, direction, &fctx);
		if (ret != 0)
			return (ret);
		memcpy(kbuf, fctx.kbuf, fctx.ksize);
		*ksizep = fctx.ksize;
		memcpy(vbuf, fctx.vbuf, fctx.vsize);
		*vsizep = fctx.vsize;

		return (0);
	}

	switch (otype) {
		case OP_INMEMTREE:
			ret = imtree->cursor(imtree, &imcursor, cflags);
			break;
		case OP_ONDISKTREE:
			ret = odtree->cursor(odtree, &odcursor, cflags);
			break;
		default:
			return (van_error_print("BAD OP_TYPE"));
	}
	if (ret != 0)
		return (ret);

	switch (otype) {
		case OP_INMEMTREE:
			ret = imcursor->get(imcursor, &key, &value, direction);
			break;
		case OP_ONDISKTREE:
			ret = odcursor->get(odcursor, &key, &value, direction);
			break;
	}
	if (ret == 0) {
		TEST_ASSERT(key.size <= MAX_BUF_LEN);
		memcpy(kbuf, key.data, key.size);
		*ksizep = key.size;
		if (value.data != NULL) {
			TEST_ASSERT(value.size <= MAX_BUF_LEN);
			memcpy(vbuf, value.data, value.size);
			*vsizep = value.size;
		} else
			*vsizep = 0;
	}

	switch (otype) {
		case OP_INMEMTREE:
			ret1 = imcursor->cleanup(imcursor);
			break;
		case OP_ONDISKTREE:
			ret1 = odcursor->cleanup(odcursor);
			break;
		default:
			ret1 = 0;
			break;
	}
	SET_RET(ret1, ret);

	return (ret);
}

/*
 * The compare function has to live in file scope: qsort_r() is not
 * portable across the platforms we build on, and these helpers are never
 * called concurrently.
 */
static VAN_DATUM_CMPFUNC sort_cmp;

static int sort_rec_cmp(const void *a, const void *b) {
	const RECORD *r1, *r2;
	VAN_DATUM d1, d2;

	r1 = (const RECORD *)a;
	r2 = (const RECORD *)b;
	memset(&d1, 0, sizeof(d1));
	memset(&d2, 0, sizeof(d2));
	d1.data = r1->key;
	d1.size = r1->ksize;
	d2.data = r2->key;
	d2.size = r2->ksize;

	return (sort_cmp(&d1, &d2));
}

/*
 * A record holds both its key and its value, so qsort() can reorder the
 * array in place; no permutation of parallel arrays is needed.
 */
int sort_testdata(TESTDATA *td, VAN_DATUM_CMPFUNC cmp) {
	if (td == NULL || td->count == 0)
		return (0);
	if (cmp == NULL)
		cmp = van_datum_defcmp;

	sort_cmp = cmp;
	qsort(td->records, td->count, sizeof(RECORD), sort_rec_cmp);

	return (0);
}

int verify_child_fails(int (*check)(void *), void *arg) {
	pid_t pid;
	int status, fd;

	/*
	 * Flush first: the child inherits the buffered output of the test
	 * and would write it a second time when it exits.
	 */
	fflush(NULL);
	pid = fork();
	TEST_ASSERT(pid >= 0);

	if (pid == 0) {
		/*
		 * Child.  The check is supposed to fail, and the failure is
		 * printed with TEST_ASSERT2(), so send its output to
		 * /dev/null instead of into the log of a passing test.
		 */
		fd = open("/dev/null", O_WRONLY);
		if (fd >= 0) {
			(void)dup2(fd, STDOUT_FILENO);
			(void)dup2(fd, STDERR_FILENO);
			if (fd > STDERR_FILENO)
				(void)close(fd);
		}
		(void)check(arg);
		/* No mismatch was reported: that is the bug we look for. */
		_exit(0);
	}

	TEST_ASSERT(waitpid(pid, &status, 0) == pid);
	if (WIFEXITED(status))
		return (WEXITSTATUS(status) != 0);
	/* Dying on a signal counts as "did not pass" too. */
	return (1);
}

/*
verify the records in the in-memory tree and on-disk tree are the same,
and the order is the same, and the number of records is the same.

do the verification using specific direction,
the caller should specify the direction,
and the function will traverse the tree in that direction.
*/
int verify_ondisktreecursor_with_inmemtreecursor(INMEM_TREE *imtree,
    ONDISK_TREE *odtree, flags_t direction, size_t *rcntp) {
	INMEM_TREECURSOR *imc;
	ONDISK_TREECURSOR *odc;
	int ret, ret1;
	size_t rcnt;
	time_t t1, t2;
	VAN_DATUM key, value, key1, value1;
	char infobuf[MAX_BUF_LEN];

	ret = 0;

	sprintf(infobuf, "cursor->get(%s)",
	    get_van_cursor_get_flag_string(direction & VAN_MASKMODE));

	time(&t1);
	rcnt = 0;
	ret = odtree->cursor(odtree, &odc, 0);
	CHK_RET(ret, "odtree->cursor");
	ret = imtree->cursor(imtree, &imc, 0);
	CHK_RET(ret, "imtree->cursor");
	for (ret = 0; ret == 0;) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		memset(&key1, 0, sizeof(key1));
		memset(&value1, 0, sizeof(value1));

		ret = odc->get(odc, &key, &value, direction);
		if (ret == 0) {
			rcnt++;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, infobuf);
		}

		ret1 = imc->get(imc, &key1, &value1, direction);
		VAN_ASSERT(ret == ret1);

		if (ret != 0)
			break;
		/* Compare */
		VAN_ASSERT(key.size == key1.size);
		VAN_ASSERT((memcmp(key.data, key1.data, key.size) == 0));
		VAN_ASSERT(value.size == value1.size);
		VAN_ASSERT((memcmp(value.data, value1.data, value.size) == 0));
	}

	if (ret == VAN_NOTFOUND)
		ret = 0;

	time(&t2);
	fprintf(stderr, "time consuming: %g seconds, get %lu records\n",
	    difftime(t2, t1), (unsigned long)rcnt);

	ret1 = odc->cleanup(odc);
	CHK_RET(ret1, "odc->cleanup");
	ret1 = imc->cleanup(imc);
	CHK_RET(ret1, "imc->cleanup");

	if (rcntp != NULL)
		*rcntp = rcnt;

	return (ret);
}

/*
 * Verify that the cursor can traverse the tree in the specified direction.
 * If rcntp is not NULL, it will return the number of records traversed.
 *
 * it verifies that the cursor can traverse the tree in the specified direction,
 * and that the records are in the correct order.
 */

static int verify_cursor_direction(void *obj, flags_t direction,
    flags_t cflags, size_t *rcntp, int otype) {
	int ret, ret1, cmp;
	INMEM_TREE *imtree;
	INMEM_TREECURSOR *imcursor;
	ONDISK_TREE *odtree;
	ONDISK_TREECURSOR *odcursor;
	VAN_TABLE *table;
	/* TODO: Table cursor */
	char prebuf[MAX_BUF_LEN];
	VAN_DATUM key, value, prekey;
	int haspre;
	time_t t1, t2;
	const char *info;
	size_t rcnt;

	rcnt = 0;

	imtree = (INMEM_TREE *)obj;
	odtree = (ONDISK_TREE *)obj;
	table = (VAN_TABLE *)obj;

	imcursor = NULL;
	odcursor = NULL;
	switch (otype) {
		case OP_INMEMTREE:
			ret = imtree->cursor(imtree, &imcursor, cflags);
			break;
		case OP_ONDISKTREE:
			ret = odtree->cursor(odtree, &odcursor, cflags);
			break;
		case OP_TABLE:
			ret = 0; /* TODO: Add table cursor test here */
			table = table;
			break;
		default:
			ret = van_error_print("BAD OP_TYPE");
			break;
	}
	if (ret != 0)
		goto err;

	time(&t1);

	/*
	 * A table has no cursor to walk, so it is traversed instead:
	 * VAN_NEXT / VAN_FIRST read it forward, VAN_PREV / VAN_LAST read it
	 * backward.  dir_cb() counts the records and checks their order,
	 * which is exactly what the cursor loop below does for a tree.
	 */
	if (otype == OP_TABLE) {
		dir_ctx_t dctx;

		memset(&dctx, 0, sizeof(dctx));
		dctx.prekey.data = dctx.prebuf;
		dctx.backward = traverse_backward(direction);
		ret = table->traverse(table, dctx.backward ?
		    VAN_TRAVERSE_BACKWARD : VAN_TRAVERSE_FORWARD,
		    NULL, dir_cb, &dctx);
		if (ret != 0)
			goto err;
		rcnt = dctx.rcnt;

		time(&t2);
		fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
		if (rcntp != NULL)
			*rcntp = rcnt;

		return (0);
	}

	haspre = 0;
	memset(&prekey, 0, sizeof(prekey));
	prekey.data = prebuf;
	for (ret = 0; ret == 0;) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		switch (otype) {
			case OP_INMEMTREE:
				info = "imcursor->get";
				ret = imcursor->get(imcursor, &key, &value, direction);
				break;
			case OP_ONDISKTREE:
				info = "odcursor->get";
				ret = odcursor->get(odcursor, &key, &value, direction);
				break;
			case OP_TABLE:
				info = "tablecursor->get";
				ret = 0;
				break;
		}

		if (ret == 0) {
			rcnt++;
			if (haspre) {
				cmp = van_datum_defcmp(&key, &prekey);
				if (direction == VAN_NEXT) {
					TEST_ASSERT(cmp > 0);
				} else {
					TEST_ASSERT(cmp < 0);
				}
			}
			memcpy(prebuf, key.data, key.size);
			prekey.size = key.size;
			haspre = 1;
		} else if (ret != VAN_NOTFOUND) {
			CHK_RET(ret, info);
		}
	}

	if (ret == VAN_NOTFOUND)
		ret = 0;

	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
err:
	if (imcursor != NULL || odcursor != NULL) {
		switch (otype) {
			case OP_INMEMTREE:
				ret1 = imcursor->cleanup(imcursor);
				break;
			case OP_ONDISKTREE:
				ret1 = odcursor->cleanup(odcursor);
				break;
			case OP_TABLE:
				ret1 = 0; /* TODO: For table */
				break;
		}
		SET_RET(ret1, ret);
	}
	if (rcntp != NULL)
		*rcntp = rcnt;

	return (ret);
}

int verify_inmemtree_cursor_direction(INMEM_TREE *tree,
    flags_t direction, flags_t cflags, size_t *rcntp) {
	return verify_cursor_direction(tree, direction,
	    cflags, rcntp, OP_INMEMTREE);
}

int verify_ondisktree_cursor_direction(ONDISK_TREE *tree,
    flags_t direction, flags_t cflags, size_t *rcntp) {
	return verify_cursor_direction(tree, direction,
	    cflags, rcntp, OP_ONDISKTREE);
}

int verify_table_cursor_direction(VAN_TABLE *table,
    flags_t direction, flags_t cflags, size_t *rcntp) {
	return verify_cursor_direction(table, direction,
	    cflags, rcntp, OP_TABLE);
}

/*
verify that, for unitialized cursor,
VAN_FIRST equals VAN_NEXT, and VAN_LAST equals VAN_PREV.
*/
static int verify_cursor_border(void *obj, flags_t direction,
    flags_t cflags, int otype) {
	int ret, ret1, ret2, cmp;
	INMEM_TREE *imtree;
	INMEM_TREECURSOR *imcursor1, *imcursor2;
	ONDISK_TREE *odtree;
	ONDISK_TREECURSOR *odcursor1, *odcursor2;
	VAN_TABLE *table;
	/* TODO: Table cursor */
	VAN_DATUM key1, value1, key2, value2;
	char kbuf1[MAX_BUF_LEN], vbuf1[MAX_BUF_LEN];
	char kbuf2[MAX_BUF_LEN], vbuf2[MAX_BUF_LEN];
	size_t klen1, vlen1, klen2, vlen2;
	flags_t direction2;

	imtree = (INMEM_TREE *)obj;
	odtree = (ONDISK_TREE *)obj;
	table = (VAN_TABLE *)obj;

	ret = 0;
	switch (direction) {
		case VAN_LAST:
			direction2 = VAN_PREV;
			break;
		case VAN_FIRST:
			direction2 = VAN_NEXT;
			break;
		default:
			ret = van_error_print("BAD direction");
			break;
	}
	if (ret != 0)
		goto err;

	memset(&key1, 0, sizeof(key1));
	memset(&value1, 0, sizeof(value1));
	memset(&key2, 0, sizeof(key2));
	memset(&value2, 0, sizeof(value2));

	imcursor1 = imcursor2 = NULL;
	odcursor1 = odcursor2 = NULL;
	switch (otype) {
		case OP_INMEMTREE:
			ret = imtree->cursor(imtree, &imcursor1, cflags);
			CHK_RET(ret, "imtree->cursor(imcursor1)");

			ret1 = imcursor1->get(imcursor1, &key1, &value1, direction);
			if (ret1 == 0) {
				memcpy(kbuf1, key1.data, key1.size);
				memcpy(vbuf1, value1.data, value1.size);
			}

			ret = imcursor1->cleanup(imcursor1);
			CHK_RET(ret, "imcursor1->cleanup(imcursor1)");

			ret = imtree->cursor(imtree, &imcursor2, cflags);
			CHK_RET(ret, "imtree->cursor(imcursor2)");

			ret2 = imcursor2->get(imcursor2, &key2, &value2, direction2);
			if (ret2 == 0) {
				memcpy(kbuf2, key2.data, key2.size);
				memcpy(vbuf2, value2.data, value2.size);
			}

			ret = imcursor2->cleanup(imcursor2);
			CHK_RET(ret, "imcursor2->cleanup(imcursor2)");

			TEST_ASSERT(ret1 == ret2);
			if (ret1 == VAN_NOTFOUND)
				ret1 = 0;
			SET_RET(ret1, ret);
			break;
		case OP_ONDISKTREE:
			ret = odtree->cursor(odtree, &odcursor1, cflags);
			CHK_RET(ret, "odtree->cursor(odcursor1)");

			ret1 = odcursor1->get(odcursor1, &key1, &value1, direction);
			if (ret1 == 0) {
				memcpy(kbuf1, key1.data, key1.size);
				memcpy(vbuf1, value1.data, value1.size);
			}

			ret = odcursor1->cleanup(odcursor1);
			CHK_RET(ret, "odcursor1->cleanup(odcursor1)");

			ret = odtree->cursor(odtree, &odcursor2, cflags);
			CHK_RET(ret, "odtree->cursor(odcursor2)");

			ret2 = odcursor2->get(odcursor2, &key2, &value2, direction2);
			if (ret2 == 0) {
				memcpy(kbuf2, key2.data, key2.size);
				memcpy(vbuf2, value2.data, value2.size);
			}

			ret = odcursor2->cleanup(odcursor2);
			CHK_RET(ret, "odcursor2->cleanup(odcursor2)");

			TEST_ASSERT(ret1 == ret2);
			if (ret1 == VAN_NOTFOUND)
				ret1 = 0;
			SET_RET(ret1, ret);
			break;
		case OP_TABLE:
			/*
			 * No cursor for a table either: both gets become a
			 * traversal, forward for VAN_FIRST / VAN_NEXT and
			 * backward for VAN_LAST / VAN_PREV, and only the
			 * first record of each is compared.  border_record()
			 * copies the record out, so it stays valid after the
			 * traversal is gone.
			 */
			ret1 = border_record(table, direction, cflags, otype,
			    kbuf1, &klen1, vbuf1, &vlen1);
			ret2 = border_record(table, direction2, cflags, otype,
			    kbuf2, &klen2, vbuf2, &vlen2);

			memset(&key1, 0, sizeof(key1));
			key1.data = kbuf1;
			key1.size = klen1;
			memset(&value1, 0, sizeof(value1));
			value1.data = vbuf1;
			value1.size = vlen1;
			memset(&key2, 0, sizeof(key2));
			key2.data = kbuf2;
			key2.size = klen2;
			memset(&value2, 0, sizeof(value2));
			value2.data = vbuf2;
			value2.size = vlen2;

			TEST_ASSERT(ret1 == ret2);
			if (ret1 == VAN_NOTFOUND)
				ret1 = 0;
			SET_RET(ret1, ret);
			break;
		default:
			ret = van_error_print("BAD OP_TYPE");
			break;
	}
	if (ret != 0 && ret != VAN_NOTFOUND)
		goto err;

	cmp = van_datum_defcmp(&key1, &key2);
	TEST_ASSERT(cmp == 0);
	cmp = van_datum_defcmp(&value1, &value2);
	TEST_ASSERT(cmp == 0);
err:
	return (ret);

}

int verify_inmemtree_cursor_border(INMEM_TREE *tree,
    flags_t direction, flags_t cflags) {
	return verify_cursor_border(tree, direction, cflags, OP_INMEMTREE);
}

int verify_ondisktree_cursor_border(ONDISK_TREE *tree,
    flags_t direction, flags_t cflags) {
	return verify_cursor_border(tree, direction, cflags, OP_ONDISKTREE);
}

int verify_table_cursor_border(VAN_TABLE *table,
    flags_t direction, flags_t cflags) {
	return verify_cursor_border(table, direction, cflags, OP_TABLE);
}

/*
 * Walk the object in the given direction and check both the number of
 * records and every single record against the expected ones.
 *
 * td must be sorted ascending by key with the compare function of the
 * object under test, see sort_testdata().
 *
 * For a table the walk is table->traverse(): VAN_NEXT reads it forward,
 * VAN_PREV backward.
 */
static int verify_cursor_direction_full(void *obj, flags_t direction,
    flags_t cflags, size_t *rcntp, TESTDATA *td, int otype) {
	INMEM_TREE *imtree;
	INMEM_TREECURSOR *imcursor;
	ONDISK_TREE *odtree;
	ONDISK_TREECURSOR *odcursor;
	VAN_TABLE *table;
	VAN_DATUM key, value;
	const char *info;
	int ret, ret1, backward;
	size_t rcnt, idx, cnt;
	RECORD *rec;
	time_t t1, t2;

	cnt = td->count;

	imtree = (INMEM_TREE *)obj;
	odtree = (ONDISK_TREE *)obj;
	table = (VAN_TABLE *)obj;
	imcursor = NULL;
	odcursor = NULL;
	info = "cursor->get";
	rcnt = 0;
	backward = traverse_backward(direction);

	time(&t1);

	if (otype == OP_TABLE) {
		dirfull_ctx_t dctx;

		memset(&dctx, 0, sizeof(dctx));
		dctx.backward = backward;
		dctx.td = td;
		ret = table->traverse(table, backward ?
		    VAN_TRAVERSE_BACKWARD : VAN_TRAVERSE_FORWARD,
		    NULL, dirfull_cb, &dctx);
		if (ret != 0)
			goto err;
		rcnt = dctx.rcnt;
		TEST_ASSERT2(rcnt == cnt,
		    "expected %lu records, walked %lu",
		    (unsigned long)cnt, (unsigned long)rcnt);

		time(&t2);
		fprintf(stderr, "time consuming: %g seconds\n",
		    difftime(t2, t1));
		if (rcntp != NULL)
			*rcntp = rcnt;

		return (0);
	}

	switch (otype) {
		case OP_INMEMTREE:
			ret = imtree->cursor(imtree, &imcursor, cflags);
			info = "imcursor->get";
			break;
		case OP_ONDISKTREE:
			ret = odtree->cursor(odtree, &odcursor, cflags);
			info = "odcursor->get";
			break;
		default:
			ret = van_error_print("BAD OP_TYPE");
			break;
	}
	if (ret != 0)
		goto err;

	for (ret = 0; ret == 0;) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		switch (otype) {
			case OP_INMEMTREE:
				ret = imcursor->get(imcursor, &key, &value,
				    direction);
				break;
			case OP_ONDISKTREE:
				ret = odcursor->get(odcursor, &key, &value,
				    direction);
				break;
		}

		if (ret == 0) {
			TEST_ASSERT2(rcnt < cnt,
			    "more records than expected (%lu)",
			    (unsigned long)cnt);
			idx = backward ? (cnt - 1 - rcnt) : rcnt;
			rec = &td->records[idx];

			TEST_ASSERT2(key.size == rec->ksize,
			    "record %lu: key size %lu, expected %lu",
			    (unsigned long)idx, (unsigned long)key.size,
			    (unsigned long)rec->ksize);
			TEST_ASSERT2(memcmp(key.data, rec->key,
			    key.size) == 0,
			    "record %lu: key does not match",
			    (unsigned long)idx);
			if (value.data != NULL) {
				TEST_ASSERT2(value.size == rec->vsize,
				    "record %lu: value size %lu, "
				    "expected %lu", (unsigned long)idx,
				    (unsigned long)value.size,
				    (unsigned long)rec->vsize);
				TEST_ASSERT2(memcmp(value.data, rec->value,
				    value.size) == 0,
				    "record %lu: value does not match",
				    (unsigned long)idx);
			}
			rcnt++;
		} else if (ret != VAN_NOTFOUND)
			CHK_RET(ret, info);
	}

	if (ret == VAN_NOTFOUND)
		ret = 0;
	TEST_ASSERT2(rcnt == cnt, "expected %lu records, walked %lu",
	    (unsigned long)cnt, (unsigned long)rcnt);

	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));
err:
	if (imcursor != NULL) {
		ret1 = imcursor->cleanup(imcursor);
		SET_RET(ret1, ret);
	}
	if (odcursor != NULL) {
		ret1 = odcursor->cleanup(odcursor);
		SET_RET(ret1, ret);
	}
	if (rcntp != NULL)
		*rcntp = rcnt;

	return (ret);
}

int verify_inmemtree_cursor_direction_full(INMEM_TREE *tree,
    flags_t direction, flags_t cflags, size_t *rcntp, TESTDATA *td) {
	return verify_cursor_direction_full(tree, direction, cflags, rcntp,
	    td, OP_INMEMTREE);
}

int verify_ondisktree_cursor_direction_full(ONDISK_TREE *tree,
    flags_t direction, flags_t cflags, size_t *rcntp, TESTDATA *td) {
	return verify_cursor_direction_full(tree, direction, cflags, rcntp,
	    td, OP_ONDISKTREE);
}

int verify_table_cursor_direction_full(VAN_TABLE *table,
    flags_t direction, flags_t cflags, size_t *rcntp, TESTDATA *td) {
	return verify_cursor_direction_full(table, direction, cflags, rcntp,
	    td, OP_TABLE);
}

/*
 * Like verify_xxx_cursor_border(), but the record VAN_FIRST / VAN_LAST
 * returns is also compared with the expected first / last record.
 */
static int verify_cursor_border_full(void *obj, flags_t direction,
    flags_t cflags, RECORD *rec, int otype) {
	char kbuf[MAX_BUF_LEN], vbuf[MAX_BUF_LEN];
	size_t klen, vlen;
	int ret;

	/*
	 * First the plain border check: on a fresh cursor the first
	 * VAN_NEXT is VAN_FIRST, and the first VAN_PREV is VAN_LAST.
	 */
	ret = verify_cursor_border(obj, direction, cflags, otype);
	if (ret != 0)
		return (ret);

	/* Then check it really is the record the caller expects. */
	ret = border_record(obj, direction, cflags, otype,
	    kbuf, &klen, vbuf, &vlen);
	if (ret == VAN_NOTFOUND)
		ret = 0;
	if (ret != 0)
		return (ret);

	TEST_ASSERT2(klen == rec->ksize && memcmp(kbuf, rec->key,
	    rec->ksize) == 0,
	    "border record: key does not match the expected one "
	    "(got %lu bytes, expected %lu)", (unsigned long)klen,
	    (unsigned long)rec->ksize);
	TEST_ASSERT2(vlen == rec->vsize && memcmp(vbuf, rec->value,
	    rec->vsize) == 0,
	    "border record: value does not match the expected one "
	    "(got %lu bytes, expected %lu)", (unsigned long)vlen,
	    (unsigned long)rec->vsize);

	return (0);
}

int verify_inmemtree_cursor_border_full(INMEM_TREE *tree,
    flags_t direction, flags_t cflags, RECORD *rec) {
	return verify_cursor_border_full(tree, direction, cflags,
	    rec, OP_INMEMTREE);
}

int verify_ondisktree_cursor_border_full(ONDISK_TREE *tree,
    flags_t direction, flags_t cflags, RECORD *rec) {
	return verify_cursor_border_full(tree, direction, cflags,
	    rec, OP_ONDISKTREE);
}

int verify_table_cursor_border_full(VAN_TABLE *table,
    flags_t direction, flags_t cflags, RECORD *rec) {
	return verify_cursor_border_full(table, direction, cflags,
	    rec, OP_TABLE);
}

int generate_string_testdata(TESTDATA *td,
    int kmin, int kmax, int vmin, int vmax) {
	size_t i;
	char kbuf[MAX_BUF_LEN], vbuf[MAX_BUF_LEN];

	for (i = 0; i < td->count; i++) {
		getrandomstring2(kbuf, kmin, kmax);
		getrandomstring2(vbuf, vmin, vmax);
		td->records[i].ksize = strlen(kbuf) + 1;
		td->records[i].vsize = strlen(vbuf) + 1;
		td->records[i].key = (char *)malloc(td->records[i].ksize);
		TEST_ASSERT(td->records[i].key != NULL);
		memcpy(td->records[i].key, kbuf, td->records[i].ksize);
		td->records[i].value = (char *)malloc(td->records[i].vsize);
		TEST_ASSERT(td->records[i].value != NULL);
		memcpy(td->records[i].value, vbuf, td->records[i].vsize);
	}

	return (0);
}

int generate_string_testdata2(TESTDATA *td, char *kpref, char *vpref,
    int start) {
	size_t i;
	char kbuf[MAX_BUF_LEN], vbuf[MAX_BUF_LEN];

	for (i = 0; i < td->count; i++) {
		sprintf(kbuf, "%s_%d", kpref, (int)i+start);
		sprintf(vbuf, "%s_%d", vpref, (int)i+start);
		td->records[i].ksize = strlen(kbuf) + 1;
		td->records[i].vsize = strlen(vbuf) + 1;
		td->records[i].key = (char *)malloc(td->records[i].ksize);
		TEST_ASSERT(td->records[i].key != NULL);
		memcpy(td->records[i].key, kbuf, td->records[i].ksize);
		td->records[i].value = (char *)malloc(td->records[i].vsize);
		TEST_ASSERT(td->records[i].value != NULL);
		memcpy(td->records[i].value, vbuf, td->records[i].vsize);
	}

	return (0);
}

int free_string_testdata(TESTDATA *td) {
	size_t i;

	if (td == NULL || td->records == NULL)
		return (0);
	for (i = 0; i < td->count; i++) {
		if (td->records[i].key != NULL) {
			free(td->records[i].key);
			td->records[i].key = NULL;
		}
		if (td->records[i].value != NULL) {
			free(td->records[i].value);
			td->records[i].value = NULL;
		}
	}
	free(td->records);
	td->records = NULL;
	td->count = 0;

	return (0);
}

/*
 * ---------- odlist / ondisktree file consistency ----------
 *
 * verify_logfiles_from_scratch() replays the VAN_ODLIST_UPDATE records
 * of a store's WAL and compares the result with the odlist file that is
 * actually on disk.
 *
 * The records use "direction C": each one carries the *full* file list
 * after a dump or a merge, so replaying is simply "the last record
 * wins" and the expected list is well defined even when the odlist file
 * is stale or missing.
 */

/* The rectype, the first field of VAN_LOGREC_COMMON. */
static int vlf_rectype(const VAN_DATUM *rec) {
	uint32 type;

	memcpy(&type, (char *)rec->data + VAN_LOGREC_HDRSIZE, sizeof(type));
	return ((int)type);
}

/*
 * Append the new_files list of one VAN_ODLIST_UPDATE record to 'out'.
 *
 * body:	tabid, nfiles, ndels, new blob, del blob
 * blob:	the concatenation of "uint32 len + len bytes", where len
 *		includes the trailing '\0'.
 */
static int vlf_new_files(const VAN_DATUM *rec, VAN_STRING_ARRAY *out) {
	char *p, *q;
	uint32 tabid, nfiles, ndels, blen, nlen;
	size_t i, left;
	int ret;

	p = (char *)rec->data + VAN_LOGREC_HDRSIZE + VAN_LOGREC_COMSIZE;
	memcpy(&tabid, p, sizeof(tabid));
	p += sizeof(tabid);
	memcpy(&nfiles, p, sizeof(nfiles));
	p += sizeof(nfiles);
	memcpy(&ndels, p, sizeof(ndels));
	p += sizeof(ndels);

	/* The new file list blob. */
	memcpy(&blen, p, sizeof(blen));
	p += sizeof(blen);
	q = p;
	left = (size_t)blen;
	for (i = 0; i < (size_t)nfiles; i++) {
		TEST_ASSERT2(left >= sizeof(nlen),
		    "odlist record: new list truncated at %lu\n",
		    (unsigned long)i);
		memcpy(&nlen, q, sizeof(nlen));
		q += sizeof(nlen);
		left -= sizeof(nlen);
		TEST_ASSERT2(left >= (size_t)nlen,
		    "odlist record: new name %lu truncated\n",
		    (unsigned long)i);
		ret = _van_strarray_add(out, q);
		CHK_RET(ret, "_van_strarray_add");
		q += nlen;
		left -= nlen;
	}
	TEST_ASSERT2(left == 0,
	    "odlist record: %lu trailing bytes in the new list\n",
	    (unsigned long)left);

	return (0);
}

/* Read <data_dir>/<table_name>.odlist, one file name per line. */
static int vlf_read_odlist(const char *data_dir, const char *table_name,
    VAN_STRING_ARRAY *out) {
	char path[VAN_MAXPATHLEN], line[VAN_MAXPATHLEN];
	FILE *f;
	size_t len;
	int ret, num;

	num = snprintf(path, sizeof(path), "%s/%s.odlist", data_dir,
	    table_name);
	TEST_ASSERT2(num >= 0 && num < (int)sizeof(path),
	    "odlist path does not fit: %s/%s.odlist\n", data_dir,
	    table_name);

	f = NULL;
	ret = _van_file_fopen(path, "r", &f);
	if (ret != 0) {
		/*
		 * No odlist at all: the table never dumped, so the
		 * expected list is empty.
		 */
		if (ret == ENOENT || ret == VAN_NOTFOUND)
			return (0);
		return (ret);
	}

	while (_van_file_freadline(f, line, VAN_MAXPATHLEN) == 0) {
		len = strlen(line);
		while (len > 0 &&
		    (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';
		if (len == 0)
			continue;
		ret = _van_strarray_add(out, line);
		if (ret != 0) {
			(void)_van_file_fclose(f);
			return (ret);
		}
	}

	return (_van_file_fclose(f));
}

static int vlf_file_exists(const char *path) {
	struct stat st;

	return (stat(path, &st) == 0);
}

/*
 * Reopen a store and one of its tables from scratch and verify that every
 * expected record is still readable.
 *
 * This tells "the data is gone" apart from "the odlist still lists trees
 * that should have been dropped": if every record survives a fresh open,
 * the file list the WAL ends with is the one that matters and whatever
 * else the odlist lists is a leftover whose removal did not happen.  A
 * failure here, on the other hand, would mean real data loss.
 */
int verify_table_after_reopen(const char *data_dir,
    const char *table_name, TESTDATA *td) {
	VAN_STORE *store;
	VAN_TABLE *table;
	int ret;

	store = NULL;
	table = NULL;

	ret = van_test_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, data_dir, 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	/*
	 * No background threads: this is about what is on disk right now,
	 * a dump or a merge started here would change it.
	 */
	ret = table->open(table, table_name, VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");

	ret = verify_table_data(table, td);
	CHK_RET(ret, "verify_table_data (after reopen)");

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close (after reopen)");
	ret = store->close(store);
	CHK_RET(ret, "store->close (after reopen)");

	return (0);
}

/*
 * Walk the table once to count the rows it holds and once more to copy
 * them, so that a test which built its data by hand still has an
 * expected set to compare a fresh open against.
 */
typedef struct {
	TESTDATA *td;
	size_t    n;
} walk_snap_t;

static int walk_count_cb(void *arg, const VAN_DATUM *k, const VAN_DATUM *v) {
	(*(size_t *)arg)++;
	return (0);
}

/*
 * The datum buffers only live for the duration of the callback, so both
 * sides are duplicated - which is also what free_string_testdata()
 * expects to release.
 */
static int walk_collect_cb(void *arg, const VAN_DATUM *k,
    const VAN_DATUM *v) {
	walk_snap_t *s;
	RECORD *r;

	s = (walk_snap_t *)arg;
	if (s->n >= s->td->count)
		return (-1);
	r = &s->td->records[s->n];
	r->key = strndup(k->data, k->size);
	r->ksize = k->size;
	r->value = strndup(v->data, v->size);
	r->vsize = v->size;
	TEST_ASSERT(r->key != NULL && r->value != NULL);
	s->n++;
	return (0);
}

/*
 * verify_table_after_reopen_walk --
 *	Close is not enough on its own: this walks 'table' to capture
 *	what it currently holds, and checks after a fresh open that
 *	every one of those rows is still readable.
 *
 *	Built for the traverse tests, which generate their data by hand
 *	or fill it from a second thread and therefore have no TESTDATA
 *	of their own to hand to verify_table_after_reopen().  The caller
 *	has to have joined every writer before calling this, otherwise
 *	the snapshot is a moving target.
 *
 *	An empty table is fine: there is nothing to check, and the open
 *	itself still has to work.
 */
int verify_table_after_reopen_walk(VAN_TABLE *table, const char *data_dir,
    const char *table_name) {
	TESTDATA td;
	walk_snap_t snap;
	size_t n;
	int ret;

	memset(&td, 0, sizeof(td));
	n = 0;
	ret = table->traverse(table, VAN_TRAVERSE_FORWARD, NULL,
	    walk_count_cb, &n);
	CHK_RET(ret, "traverse (snapshot count)");

	if (n > 0) {
		TESTDATA_INIT(&td, n);
		snap.td = &td;
		snap.n = 0;
		ret = table->traverse(table, VAN_TRAVERSE_FORWARD, NULL,
		    walk_collect_cb, &snap);
		CHK_RET(ret, "traverse (snapshot collect)");
	}

	ret = verify_table_after_reopen(data_dir, table_name, &td);
	if (n > 0)
		(void)free_string_testdata(&td);

	return (ret);
}

int verify_logfiles_from_scratch(const char *data_dir,
    const char *table_name) {
	VAN_STORE *store;
	VAN_LOG *logp;
	VAN_LOG_CURSOR *lgc;
	VAN_STRING_ARRAY *replayed, *ondisk;
	VAN_DATUM rec;
	VAN_LSN lsn;
	size_t i;
	int ret, found;

	store = NULL;
	lgc = NULL;
	replayed = NULL;
	ondisk = NULL;
	found = 0;

	/*
	 * Open the store only.  Opening the table would read the odlist
	 * and open the trees, which is exactly what has to be derived
	 * from the log instead.
	 */
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, data_dir, 0);
	CHK_RET(ret, "store->open");

	ret = _van_strarray_alloc(&replayed, 0);
	CHK_RET(ret, "_van_strarray_alloc(replayed)");
	ret = _van_strarray_alloc(&ondisk, 0);
	CHK_RET(ret, "_van_strarray_alloc(ondisk)");

	/* Replay: every record replaces the whole list, last one wins. */
	logp = store->log_handle;
	ret = logp->cursor(logp, &lgc);
	CHK_RET(ret, "logp->cursor");

	memset(&rec, 0, sizeof(rec));
	ret = lgc->get(lgc, &lsn, &rec, VAN_FIRST);
	while (ret == 0) {
		if (vlf_rectype(&rec) == VAN_ODLIST_UPDATE) {
			found++;
			(void)_van_strarray_free(replayed);
			replayed = NULL;
			ret = _van_strarray_alloc(&replayed, 0);
			CHK_RET(ret, "_van_strarray_alloc(replayed)");
			ret = vlf_new_files(&rec, replayed);
			CHK_RET(ret, "vlf_new_files");
		}
		_van_free(NULL, rec.data);
		memset(&rec, 0, sizeof(rec));
		ret = lgc->get(lgc, &lsn, &rec, VAN_NEXT);
	}
	if (ret != VAN_NOTFOUND)
		CHK_RET(ret, "lgc->get");
	if (rec.data != NULL)
		_van_free(NULL, rec.data);

	/* What is actually on disk. */
	ret = vlf_read_odlist(data_dir, table_name, ondisk);
	CHK_RET(ret, "vlf_read_odlist");

	fprintf(stderr,
	    "verify_logfiles_from_scratch: %d odlist record(s), "
	    "replayed %lu file(s), on disk %lu file(s)\n",
	    found, (unsigned long)replayed->size,
	    (unsigned long)ondisk->size);

	TEST_ASSERT2(replayed->size == ondisk->size,
	    "replayed %lu file(s) but the odlist has %lu\n",
	    (unsigned long)replayed->size, (unsigned long)ondisk->size);

	for (i = 0; i < ondisk->size; i++) {
		TEST_ASSERT2(strcmp(replayed->array[i],
		    ondisk->array[i]) == 0,
		    "file %lu differs: replayed %s, odlist %s\n",
		    (unsigned long)i, replayed->array[i],
		    ondisk->array[i]);

		/* Every listed tree has to exist and verify. */
		TEST_ASSERT2(vlf_file_exists(ondisk->array[i]),
		    "ondisktree file is missing: %s\n", ondisk->array[i]);
		ret = _van_ondisktree_verify(ondisk->array[i]);
		CHK_RET(ret, "_van_ondisktree_verify");
	}

	(void)lgc->close(lgc);
	lgc = NULL;
	(void)_van_strarray_free(replayed);
	replayed = NULL;
	(void)_van_strarray_free(ondisk);
	ondisk = NULL;
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	return (0);
}

