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
static int populate_data(void *obj, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt, int otype);
static int verify_data(void *obj, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt, int otype);
static int verify_data_withcursor(void *obj, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt, int otype);
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
			ret = _van_inmemtree_cursor_init(imtree, &imcursor, 0);
			break;
		case OP_ONDISKTREE:
			ret = _van_ondisktree_cursor_init(odtree, &odcursor, 0);
			break;
		case OP_TABLE:
			ret = 0; /* TODO: Add table cursor test here */
			table = table;
			break;
		default:
			ret = _van_error_print("BAD OP_TYPE");
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
				ret = _van_inmemtree_cursor_getraw(imcursor,
				    &key, &value, VAN_NEXT);
				break;
			case OP_ONDISKTREE:
				ret = _van_ondisktree_cursor_getraw(odcursor,
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
				ret1 = _van_inmemtree_cursor_cleanup(imcursor);
				break;
			case OP_ONDISKTREE:
				ret1 = _van_ondisktree_cursor_cleanup(odcursor);
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

static int populate_data(void *obj, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt, int otype) {
	size_t i;
	int ret;
	VAN_DATUM key, value;
	time_t t1, t2;
	VAN_TABLE *table;
	INMEM_TREE *tree;
	const char *info;

	table = (VAN_TABLE *)obj;
	tree = (INMEM_TREE *)obj;

	time(&t1);
	for (i = 0; i < cnt; i++) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = keys[i];
		key.size = ksizes[i];
		value.data = values[i];
		value.size = vsizes[i];
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
				ret = _van_error_print("BAD OP_TYPE");
				break;
		}
		CHK_RET(ret, info);
	}
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

	return (ret);
}

int populate_table(VAN_TABLE *table, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt) {
	return populate_data(table,
	    keys, ksizes, values, vsizes, cnt, OP_TABLE);
}

int populate_inmemtree(INMEM_TREE *tree, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt) {
	return populate_data(tree,
	    keys, ksizes, values, vsizes, cnt, OP_INMEMTREE);
}

/*
verify the records using the inmemtree/odisktree/table get function,
and verify the records are the same as the input data.
*/
static int verify_data(void *obj, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt, int otype) {
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
	for (i = 0; i < cnt; i++) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = keys[i];
		key.size = ksizes[i];
		
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
				ret = _van_error_print("BAD OP_TYPE");
		}
		CHK_RET(ret, info);
		VAN_ASSERT(value.size == vsizes[i]);
		VAN_ASSERT((memcmp(value.data, values[i], value.size) == 0));
	}
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

	return (ret);
}

int verify_inmemtree_data(INMEM_TREE *tree, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt) {
	return verify_data(tree, keys, ksizes,
	    values, vsizes, cnt, OP_INMEMTREE);
}

int verify_ondisktree_data(ONDISK_TREE *tree, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt) {
	return verify_data(tree, keys, ksizes,
	    values, vsizes, cnt, OP_ONDISKTREE);
}

int verify_table_data(VAN_TABLE *table, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt) {
	return verify_data(table, keys, ksizes,
	    values, vsizes, cnt, OP_TABLE);
}

static int verify_data_withcursor(void *obj, char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt, int otype) {
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
			ret = _van_inmemtree_cursor_init(imtree, &imcursor, 0);
			break;
		case OP_ONDISKTREE:
			ret = _van_ondisktree_cursor_init(odtree, &odcursor, 0);
			break;
		case OP_TABLE:
			ret = 0; /* TODO: Add table cursor test here */
			table = table;
			break;
		default:
			ret = _van_error_print("BAD OP_TYPE");
			break;
	}
	if (ret != 0)
		goto err;

	time(&t1);
	for (i = 0; i < cnt; i++) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = keys[i];
		key.size = ksizes[i];
		
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
				ret = _van_error_print("BAD OP_TYPE");
		}
		CHK_RET(ret, info);
		VAN_ASSERT(value.size == vsizes[i]);
		VAN_ASSERT((memcmp(value.data, values[i], value.size) == 0));
	}
	time(&t2);
	fprintf(stderr, "time consuming: %g seconds\n", difftime(t2, t1));

err:
	if (imcursor != NULL || odcursor != NULL) {
		switch (otype) {
			case OP_INMEMTREE:
				ret1 = _van_inmemtree_cursor_cleanup(imcursor);
				break;
			case OP_ONDISKTREE:
				ret1 = _van_ondisktree_cursor_cleanup(odcursor);
				break;
			case OP_TABLE:
				ret1 = 0; /* TODO: For table */
				break;
		}
		SET_RET(ret1, ret);
	}

	return (ret);
}

int verify_inmemtree_data_withcursor(INMEM_TREE *tree, char **keys,
    size_t *ksizes, char **values, size_t *vsizes, size_t cnt) {
	return verify_data_withcursor(tree, keys, ksizes,
	    values, vsizes, cnt, OP_INMEMTREE);
}

int verify_ondisktree_data_withcursor(ONDISK_TREE *tree, char **keys,
    size_t *ksizes, char **values, size_t *vsizes, size_t cnt) {
	return verify_data_withcursor(tree, keys, ksizes,
	    values, vsizes, cnt, OP_ONDISKTREE);
}

int verify_table_data_withcursor(VAN_TABLE *table, char **keys,
    size_t *ksizes, char **values, size_t *vsizes, size_t cnt) {
	return verify_data_withcursor(table, keys, ksizes,
	    values, vsizes, cnt, OP_TABLE);
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
	ret = _van_ondisktree_cursor_init(odtree, &odc, 0);
	CHK_RET(ret, "_van_ondisktree_cursor_init");
	ret = _van_inmemtree_cursor_init(imtree, &imc, 0);
	CHK_RET(ret, "_van_inmemtree_cursor_init");
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

	ret1 = _van_ondisktree_cursor_cleanup(odc);
	CHK_RET(ret1, "_van_ondisktree_cursor_cleanup");
	ret1 = _van_inmemtree_cursor_cleanup(imc);
	CHK_RET(ret1, "_van_inmemtree_cursor_cleanup");

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
			ret = _van_inmemtree_cursor_init(imtree, &imcursor, cflags);
			break;
		case OP_ONDISKTREE:
			ret = _van_ondisktree_cursor_init(odtree, &odcursor, cflags);
			break;
		case OP_TABLE:
			ret = 0; /* TODO: Add table cursor test here */
			table = table;
			break;
		default:
			ret = _van_error_print("BAD OP_TYPE");
			break;
	}
	if (ret != 0)
		goto err;

	time(&t1);
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
				cmp = _van_datum_defcmp(&key, &prekey);
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
				ret1 = _van_inmemtree_cursor_cleanup(imcursor);
				break;
			case OP_ONDISKTREE:
				ret1 = _van_ondisktree_cursor_cleanup(odcursor);
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
			ret = _van_error_print("BAD direction");
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
			ret = _van_inmemtree_cursor_init(imtree, &imcursor1, cflags);
			CHK_RET(ret, "_van_inmemtree_cursor_init(imcursor1)");

			ret1 = imcursor1->get(imcursor1, &key1, &value1, direction);
			if (ret1 == 0) {
				memcpy(kbuf1, key1.data, key1.size);
				memcpy(vbuf1, value1.data, value1.size);
			}

			ret = _van_inmemtree_cursor_cleanup(imcursor1);
			CHK_RET(ret, "_van_inmemtree_cursor_cleanup(imcursor1)");

			ret = _van_inmemtree_cursor_init(imtree, &imcursor2, cflags);
			CHK_RET(ret, "_van_inmemtree_cursor_init(imcursor2)");

			ret2 = imcursor2->get(imcursor2, &key2, &value2, direction2);
			if (ret2 == 0) {
				memcpy(kbuf2, key2.data, key2.size);
				memcpy(vbuf2, value2.data, value2.size);
			}

			ret = _van_inmemtree_cursor_cleanup(imcursor2);
			CHK_RET(ret, "_van_inmemtree_cursor_cleanup(imcursor2)");

			TEST_ASSERT(ret1 == ret2);
			if (ret1 == VAN_NOTFOUND)
				ret1 = 0;
			SET_RET(ret1, ret);
			break;
		case OP_ONDISKTREE:
			ret = _van_ondisktree_cursor_init(odtree, &odcursor1, cflags);
			CHK_RET(ret, "_van_ondisktree_cursor_init(odcursor1)");

			ret1 = odcursor1->get(odcursor1, &key1, &value1, direction);
			if (ret1 == 0) {
				memcpy(kbuf1, key1.data, key1.size);
				memcpy(vbuf1, value1.data, value1.size);
			}

			ret = _van_ondisktree_cursor_cleanup(odcursor1);
			CHK_RET(ret, "_van_inmemtree_cursor_cleanup(imcursor1)");

			ret = _van_ondisktree_cursor_init(odtree, &odcursor2, cflags);
			CHK_RET(ret, "_van_ondisktree_cursor_init(odcursor2)");
	
			ret2 = odcursor2->get(odcursor2, &key2, &value2, direction2);
			if (ret2 == 0) {
				memcpy(kbuf2, key2.data, key2.size);
				memcpy(vbuf2, value2.data, value2.size);
			}

			ret = _van_ondisktree_cursor_cleanup(odcursor2);
			CHK_RET(ret, "_van_inmemtree_cursor_cleanup(imcursor1)");

			TEST_ASSERT(ret1 == ret2);
			if (ret1 == VAN_NOTFOUND)
				ret1 = 0;
			SET_RET(ret1, ret);
			break;
		case OP_TABLE:
			ret = 0; /* TODO: Add table cursor test here */
			table = table;
			break;
		default:
			ret = _van_error_print("BAD OP_TYPE");
			break;
	}
	if (ret != 0 && ret != VAN_NOTFOUND)
		goto err;

	cmp = _van_datum_defcmp(&key1, &key2);
	TEST_ASSERT(cmp == 0);
	cmp = _van_datum_defcmp(&value1, &value2);
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

int generate_string_testdata(char **keys, size_t *ksizes, int kmin, int kmax,
    char **values, size_t *vsizes, int vmin, int vmax, size_t cnt) {
	size_t i;
	char kbuf[MAX_BUF_LEN], vbuf[MAX_BUF_LEN];

	for (i = 0; i < cnt; i++) {
		getrandomstring2(kbuf, kmin, kmax);
		getrandomstring2(vbuf, vmin, vmax);
		ksizes[i] = strlen(kbuf) + 1;
		vsizes[i] = strlen(vbuf) + 1;
		keys[i] = (char *)malloc(ksizes[i]);
		TEST_ASSERT(keys[i] != NULL);
		memcpy(keys[i], kbuf, ksizes[i]);
		values[i] = (char *)malloc(vsizes[i]);
		TEST_ASSERT(values[i] != NULL);
		memcpy(values[i], vbuf, vsizes[i]);
	}

	return (0);
}

int generate_string_testdata2(char **keys, size_t *ksizes, char *kpref,
    char **values, size_t *vsizes, char *vpref, size_t cnt, int start) {
	size_t i;
	char kbuf[MAX_BUF_LEN], vbuf[MAX_BUF_LEN];

	for (i = 0; i < cnt; i++) {
		sprintf(kbuf, "%s_%d", kpref, (int)i+start);
		sprintf(vbuf, "%s_%d", vpref, (int)i+start);
		ksizes[i] = strlen(kbuf) + 1;
		vsizes[i] = strlen(vbuf) + 1;
		keys[i] = (char *)malloc(ksizes[i]);
		TEST_ASSERT(keys[i] != NULL);
		memcpy(keys[i], kbuf, ksizes[i]);
		values[i] = (char *)malloc(vsizes[i]);
		TEST_ASSERT(values[i] != NULL);
		memcpy(values[i], vbuf, vsizes[i]);
	}

	return (0);
}

int free_string_testdata(char **keys, size_t *ksizes,
    char **values, size_t *vsizes, size_t cnt) {
	size_t i;
	for (i = 0; i < cnt; i++) {
		if (keys[i] != NULL) {
			free(keys[i]);
			keys[i] = NULL;
		}
		if (values[i] != NULL) {
			free(values[i]);
			values[i] = NULL;
		}
	}
	free(keys);
	free(values);
	free(ksizes);
	free(vsizes);

	return (0);
}

