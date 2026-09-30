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
#ifndef _VAN_TEST_COMMON_H
#define _VAN_TEST_COMMON_H

/* Internal headers */
#include "van_int.h"

/*
 * Create a store with an enlarged on-disk cache (1G instead of the
 * default 64MB).  The table/ondisktree tests push a lot of data past the
 * in-memory threshold and the small default cache makes them thrash / fail,
 * so we bump it here.  Only used by the table and ondisktree test suites.
 */
static inline int
van_test_store_create(VAN_STORE **storep)
{
	int ret;

	ret = van_store_create(storep);
	if (ret == 0 && storep != NULL && *storep != NULL)
		(*storep)->set_cachesize(*storep, GIGABYTE);
	return (ret);
}

#define DEF_DIRMODE (S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)
#define DEF_FILEMODE (S_IRWXU|S_IRGRP|S_IROTH)
#define CMD_BUF_LEN 500
#define MAX_BUF_LEN 500
#define MAX_STRBUF_LEN 1000

#define TYPES_NUM 5

/* Used for del and others*/
#define TYPE_INT 1
#define TYPE_STRING 2
#define TYPE_STRUCT 3
#define TYPE_FBYTES 4
#define TYPE_VBYTES 5

/* Used for put */
#define KEY_INT_DATA_INT 1
#define KEY_INT_DATA_STRING 2
#define KEY_INT_DATA_STRUCT 3
#define KEY_INT_DATA_FBYTES 4
#define KEY_INT_DATA_VBYTES 5
#define KEY_STRING_DATA_INT 6
#define KEY_STRING_DATA_STRING 7
#define KEY_STRING_DATA_STRUCT 8
#define KEY_STRING_DATA_FBYTES 9
#define KEY_STRING_DATA_VBYTES 10
#define KEY_STRUCT_DATA_INT 11
#define KEY_STRUCT_DATA_STRING 12
#define KEY_STRUCT_DATA_STRUCT 13
#define KEY_STRUCT_DATA_FBYTES 14
#define KEY_STRUCT_DATA_VBYTES 15
#define KEY_FBYTES_DATA_INT 16
#define KEY_FBYTES_DATA_STRING 17
#define KEY_FBYTES_DATA_STRUCT 18
#define KEY_FBYTES_DATA_FBYTES 19
#define KEY_FBYTES_DATA_VBYTES 20
#define KEY_VBYTES_DATA_INT 21
#define KEY_VBYTES_DATA_STRING 22
#define KEY_VBYTES_DATA_STRUCT 23
#define KEY_VBYTES_DATA_FBYTES 24
#define KEY_VBYTES_DATA_VBYTES 25

#define MAX_RECORD_LEN 	5242880

#define OP_NOOP		0
#define OP_UPDATE	1
#define OP_DELETE	2

/*
 * One record of a test data set: a key and a value, each a byte string
 * with its length.  The buffers are owned by the test data set, see
 * generate_string_testdata() and free_string_testdata().
 */
typedef struct _record {
	char  *key;
	size_t ksize;
	char  *value;
	size_t vsize;
} RECORD;

/*
 * A whole test data set: count records, in the order the test generated
 * or sorted them.
 *
 * Every test helper takes one of these instead of the four parallel
 * arrays and the count it used to take.  A helper that only needs the
 * keys, delete_xxx() for instance, reads td->records[i].key and
 * td->records[i].ksize and ignores the value half.
 */
typedef struct _testdata {
	RECORD *records;
	size_t  count;
} TESTDATA;

/*
 * Allocate the record array of a test data set, all fields zeroed.  The
 * records themselves are filled in afterwards, by
 * generate_string_testdata() or by the test itself.
 */
#define TESTDATA_INIT(tdp, n) do { \
	(tdp)->records = (RECORD *)calloc((n), sizeof(RECORD)); \
	TEST_ASSERT((tdp)->records != NULL); \
	(tdp)->count = (n); \
} while (0)

/*
 * Point a test data set at n records of another one, starting at record
 * off.  Nothing is copied, both share the same records, so freeing one
 * of them frees the records of both.
 */
#define TESTDATA_SLICE(dst, src, off, n) do { \
	(dst)->records = (src)->records + (off); \
	(dst)->count = (n); \
} while (0)

/* Records Number Settings */
#ifdef __CYGWIN__
#define TEST_NUM 5000
#else
#define TEST_NUM 5000
#endif

#define CHK_RET(ret, str) do { \
	int _ret = (ret); \
	if (_ret) { \
		fprintf(stderr, "[%lu][%lu] %s:%d Error(%d, %s)\n", \
		    (unsigned long)getpid(), (unsigned long)pthread_self(), \
		    __FILE__, __LINE__, _ret, (str)); \
		exit(1); \
	} \
} while(0)

#define CHK_RET_NOEXIT(ret, str) do { \
	int _ret = (ret); \
	if (_ret) { \
		fprintf(stderr, "[%lu][%lu] %s:%d Error(%d, %s)\n", \
		    (unsigned long)getpid(), (unsigned long)pthread_self(), \
		    __FILE__, __LINE__, _ret, (str)); \
	} \
} while(0)

#define TEST_ASSERT(expr) do { \
	if(!(expr)) { \
		printf("Assert Failure(%s:%d): "#expr"\n", __FILE__, __LINE__); \
		exit(1); \
	} \
	}while(0)

#define TEST_ASSERT2(expr, ...) do { \
	if(!(expr)) { \
		printf(__VA_ARGS__); \
		printf("Assert Failure(%s:%d): "#expr"\n", __FILE__, __LINE__); \
		exit(1); \
	} \
	}while(0)

extern int is_cygwin();

extern int getrandomstring(char *buf, int fixedlen);
extern int getrandomstring2(char *buf, int minlen, int maxlen);

extern char *getstrtime(char *buf, int len);
extern char *getstrtime2(char *buf, int len, time_t t1);

extern int check_ondisktree_duplicate(ONDISK_TREE *tree,
    size_t maxksz, size_t *totalp, size_t *dupcountp);
extern int check_inmemtree_duplicate(INMEM_TREE *tree,
    size_t maxksz, size_t *totalp, size_t *dupcountp);
extern int check_table_duplicate(VAN_TABLE *table,
    size_t maxksz, size_t *totalp, size_t *dupcountp);

extern int populate_table(VAN_TABLE *table, TESTDATA *td);
extern int populate_table_with_expected(VAN_TABLE *table, TESTDATA *td,
    int expected);
extern int populate_inmemtree(INMEM_TREE *tree, TESTDATA *td);
extern int populate_inmemtree_with_expected(INMEM_TREE *tree, TESTDATA *td,
    int expected);
int delete_inmemtree_with_expected(INMEM_TREE *tree, TESTDATA *td,
	int start, int step, int expected);
int delete_inmemtree(INMEM_TREE *tree, TESTDATA *td, int step);
int delete_table_with_expected(VAN_TABLE *table, TESTDATA *td,
	int start, int step, int expected);
int delete_table(VAN_TABLE *table, TESTDATA *td, int step);

extern int verify_inmemtree_data(INMEM_TREE *tree, TESTDATA *td);
extern int verify_ondisktree_data(ONDISK_TREE *tree, TESTDATA *td);
extern int verify_table_data(VAN_TABLE *table, TESTDATA *td);
extern int verify_inmemtree_with_expected(INMEM_TREE *tree, TESTDATA *td,
    int start, int step, int expected);
extern int verify_ondisktree_with_expected(ONDISK_TREE *tree, TESTDATA *td,
    int start, int step, int expected);
extern int verify_table_with_expected(VAN_TABLE *table, TESTDATA *td,
    int start, int step, int expected);

extern int verify_inmemtree_data_withcursor(INMEM_TREE *tree, TESTDATA *td);
extern int verify_ondisktree_data_withcursor(ONDISK_TREE *tree, TESTDATA *td);
extern int verify_table_data_withcursor(VAN_TABLE *table, TESTDATA *td);

extern int verify_ondisktreecursor_with_inmemtreecursor(INMEM_TREE *imtree,
    ONDISK_TREE *odtree, flags_t direction, size_t *rcntp) ;

extern int verify_inmemtree_cursor_direction(INMEM_TREE *tree,
    flags_t direction, flags_t cflags, size_t *rcntp);
extern int verify_ondisktree_cursor_direction(ONDISK_TREE *tree,
    flags_t direction, flags_t cflags, size_t *rcntp);
extern int verify_table_cursor_direction(VAN_TABLE *table,
    flags_t direction, flags_t cflags, size_t *rcntp);

extern int verify_inmemtree_cursor_border(INMEM_TREE *tree,
    flags_t direction, flags_t cflags);
extern int verify_ondisktree_cursor_border(ONDISK_TREE *tree,
    flags_t direction, flags_t cflags);
extern int verify_table_cursor_border(VAN_TABLE *table,
    flags_t direction, flags_t cflags);

/*
 * Same as verify_xxx_cursor_direction(), but every record is also compared
 * against the expected one.
 *
 * td describes the expected records and must be sorted ascending by key,
 * using the compare function the tree or table was built with (see
 * sort_testdata()).
 *
 * Walking with VAN_NEXT starts at td->records[0] and ends at
 * td->records[td->count - 1], walking with VAN_PREV starts at the last
 * record and ends at the first one.  A single mismatch aborts the test.
 */
extern int verify_inmemtree_cursor_direction_full(INMEM_TREE *tree,
    flags_t direction, flags_t cflags, size_t *rcntp, TESTDATA *td);
extern int verify_ondisktree_cursor_direction_full(ONDISK_TREE *tree,
    flags_t direction, flags_t cflags, size_t *rcntp, TESTDATA *td);
extern int verify_table_cursor_direction_full(VAN_TABLE *table,
    flags_t direction, flags_t cflags, size_t *rcntp, TESTDATA *td);

/*
 * Same as verify_xxx_cursor_border(), but the record that VAN_FIRST /
 * VAN_LAST returns is also compared against the expected first / last
 * record, passed in as a single RECORD.
 */
extern int verify_inmemtree_cursor_border_full(INMEM_TREE *tree,
    flags_t direction, flags_t cflags, RECORD *rec);
extern int verify_ondisktree_cursor_border_full(ONDISK_TREE *tree,
    flags_t direction, flags_t cflags, RECORD *rec);
extern int verify_table_cursor_border_full(VAN_TABLE *table,
    flags_t direction, flags_t cflags, RECORD *rec);

/*
 * Sort the records of a test data set ascending by key, using cmp -- the
 * compare function of the tree or table under test, so that the resulting
 * order is exactly the one a forward walk of that object produces.
 */
extern int sort_testdata(TESTDATA *td, VAN_DATUM_CMPFUNC cmp);

/*
 * Run check in a child process and tell whether it failed.
 *
 * The verify_xxx_*_full() helpers report a mismatch with TEST_ASSERT2(),
 * which exits the process, so a negative test cannot call one directly and
 * still go on.  Running it in a child keeps the test alive:
 *
 *	1 = the child did not come back with 0, which is what a negative
 *	    test wants to see;
 *	0 = the check passed, so a wrong record went undetected.
 *
 * The child gets its own copy of the test data (fork is copy on write), so
 * it may corrupt the expected records without touching the parent's.  Its
 * output is thrown away, the failure it reports is the whole point.
 */
extern int verify_child_fails(int (*check)(void *), void *arg);

/*
 * Fill the records of an already allocated test data set, see
 * TESTDATA_INIT().  Both key and value are random strings, the key of
 * kmin .. kmax characters and the value of vmin .. vmax characters.
 */
extern int generate_string_testdata(TESTDATA *td,
    int kmin, int kmax, int vmin, int vmax);
extern int generate_string_testdata2(TESTDATA *td, char *kpref, char *vpref,
    int start);
extern int free_string_testdata(TESTDATA *td);

/* Other headers */
#include "flags_common.h"
#include "van_flags_common.h"

#endif
