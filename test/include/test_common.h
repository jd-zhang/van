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

extern int populate_table(VAN_TABLE *table, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt);
extern int populate_inmemtree(INMEM_TREE *tree, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt);

extern int verify_inmemtree_data(INMEM_TREE *tree, char **keys, 
    size_t *ksizes, char **values, size_t *vsizes, size_t cnt);
extern int verify_ondisktree_data(ONDISK_TREE *tree, char **keys,
    size_t *ksizes, char **values, size_t *vsizes, size_t cnt);
extern int verify_table_data(VAN_TABLE *table, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt);

extern int verify_inmemtree_data_withcursor(INMEM_TREE *tree, char **keys,
    size_t *ksizes, char **values, size_t *vsizes, size_t cnt);
extern int verify_ondisktree_data_withcursor(ONDISK_TREE *tree, char **keys,
    size_t *ksizes, char **values, size_t *vsizes, size_t cnt);
extern int verify_table_data_withcursor(VAN_TABLE *table, char **keys,
    size_t *ksizes, char **values, size_t *vsizes, size_t cnt);

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

#define STRING_TESTDATA_DEFINE \
	char **keys, **values; \
	size_t *ksizes, *vsizes

#define STRING_TESTDATA_INIT do { \
	keys = (char **)malloc(cnt * sizeof(char *)); \
	TEST_ASSERT(keys != NULL); \
	ksizes = (size_t *)malloc(cnt * sizeof(size_t)); \
	TEST_ASSERT(ksizes != NULL); \
	values = (char **)malloc(cnt * sizeof(char *)); \
	TEST_ASSERT(values != NULL); \
	vsizes = (size_t *)malloc(cnt * sizeof(size_t)); \
	TEST_ASSERT(vsizes != NULL); \
} while (0)

typedef struct _populate_data {
	void *handle;
	char **keys;
	size_t *ksizes;
	char **values;
	size_t *vsizes;
	size_t cnt;
} POPULATE_DATA;

extern int generate_string_testdata(char **keys, size_t *ksizes, 
    int kmin, int kmax, char **values, size_t *vsizes, int vmin, 
    int vmax, size_t cnt);
extern int generate_string_testdata2(char **keys, size_t *ksizes, char *kpref,
    char **values, size_t *vsizes, char *vpref, size_t cnt, int start);
extern int free_string_testdata(char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt);

/* Other headers */
#include "flags_common.h"
#include "van_flags_common.h"

#endif
