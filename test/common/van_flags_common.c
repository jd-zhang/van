#include "test_common.h"

FLAGINFO VAN_ALLOC_FLAGS[] = {
	{"VAN_MALLOC", VAN_MALLOC},
	{"VAN_REALLOC", VAN_REALLOC},
	{"VAN_USERMEM", VAN_USERMEM},
	{NULL, 0}
};
DEFINE_CNT(VAN_ALLOC_FLAGS);

char *get_van_alloc_flag_string(flags_t flag) {
	return get_unique_flag_string(VAN_ALLOC_FLAGS, flag);
}

FLAGINFO VAN_CURSOR_ALLOC_FLAGS[] = {
	{"VAN_TEMPCURSOR", VAN_TEMPCURSOR},
	{"VAN_WRITECURSOR", VAN_WRITECURSOR},
	{NULL, 0}
};
DEFINE_CNT(VAN_CURSOR_ALLOC_FLAGS);

char *get_van_cursor_alloc_flags_string(flags_t flags, char *buff) {
	return get_flags_string(VAN_CURSOR_ALLOC_FLAGS, flags, buff);
}

FLAGINFO VAN_CURSOR_GET_FLAGS[] = {
	{"VAN_SET", VAN_SET},
	{"VAN_SET_RANGE", VAN_SET_RANGE},
	{"VAN_NEXT", VAN_NEXT},
	{"VAN_PREV", VAN_PREV},
	{"VAN_FIRST", VAN_FIRST},
	{"VAN_LAST", VAN_LAST},
	{"VAN_CURRENT", VAN_CURRENT},
	{NULL, 0}
};
DEFINE_CNT(VAN_CURSOR_GET_FLAGS);

char *get_van_cursor_get_flag_string(flags_t flag) {
	return get_unique_flag_string(VAN_CURSOR_GET_FLAGS, flag);
}

FLAGINFO VAN_CURSOR_MIXGET_FLAGS[] = {
	{"VAN_CURSOR_WRITE", VAN_CURSOR_WRITE},
	{"VAN_CURSOR_READ", VAN_CURSOR_READ},
	{"VAN_SKIP_DELETE", VAN_SKIP_DELETE},
	{NULL, 0}
};
DEFINE_CNT(VAN_CURSOR_MIXGET_FLAGS);

char *get_van_cursor_mixget_flags_string(flags_t flags, char *buff) {
	return get_flags_string(VAN_CURSOR_MIXGET_FLAGS, flags, buff);
}

FLAGINFO VAN_INMEMTREE_PUT_FLAGS[] = {
	{"VAN_NOOVERWRITE", VAN_NOOVERWRITE},
	{NULL, 0}
};
DEFINE_CNT(VAN_INMEMTREE_PUT_FLAGS);

char *get_van_inmemtree_put_flag_string(flags_t flag) {
	return get_unique_flag_string(VAN_INMEMTREE_PUT_FLAGS, flag);
}


FLAGINFO VAN_ODTREE_OPEN_FLAGS[] = {
	{"VAN_OPEN_CREAT", VAN_OPEN_CREAT},
	{"VAN_OPEN_TRUNC", VAN_OPEN_TRUNC},
	{"VAN_OPEN_DIRECT", VAN_OPEN_DIRECT},
	{"VAN_OPEN_APPEND", VAN_OPEN_APPEND},
	{"VAN_OPEN_NONBLOCK", VAN_OPEN_NONBLOCK},
	{"VAN_OPEN_RDONLY", VAN_OPEN_RDONLY},
	{"VAN_OPEN_EXCL", VAN_OPEN_EXCL},
	{NULL, 0}
};
DEFINE_CNT(VAN_ODTREE_OPEN_FLAGS);

char *get_van_odtree_open_flags_string(flags_t flags, char *buff) {
	return get_flags_string(VAN_ODTREE_OPEN_FLAGS, flags, buff);
}

FLAGINFO ERR_RETURN_CODES[] = {
	{"VAN_NOTFOUND", VAN_NOTFOUND},
	{"VAN_INVALID", VAN_INVALID},
	{"VAN_BUFFER_SMALL", VAN_BUFFER_SMALL},
	{"VAN_STALE", VAN_STALE},
	{"VAN_BAD", VAN_BAD},
	{"VAN_DELETED", VAN_DELETED},
	{"VAN_READONLY", VAN_READONLY},
	{"VAN_OVERFLOW", VAN_OVERFLOW},
	{"VAN_FROZEN", VAN_FROZEN},
	{"VAN_INMEM_FULL", VAN_INMEM_FULL},
	{NULL, 0}
};
DEFINE_CNT(ERR_RETURN_CODES);

char *get_van_err_return_code_string(flags_t flag) {
	return get_unique_flag_string(ERR_RETURN_CODES, flag);
}

