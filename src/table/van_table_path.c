#include "van_int.h"
#include "van.h"

int _van_table_path(VAN_TABLE *table, const char *file, char **tp) {
	char *pathbuf;
	int ret;

	VAN_ASSERT(table != NULL);
	VAN_ASSERT(file != NULL);

	pathbuf = NULL;
	ret = _van_malloc(NULL, VAN_MAXPATHLEN, &pathbuf);
	CHK_ERR(ret);
	ret = _van_table_path_noalloc(table, pathbuf, VAN_MAXPATHLEN, file);
	if (ret == 0)
		*tp = pathbuf;
err:
	return ret;
}

int _van_table_path_noalloc(VAN_TABLE *table, 
    char *buf, size_t buflen, const char *file) {
	const char *tbdir, *rptr;
	int rsize;
	
	/*
	 * The dir is
	 * table->table_dir ---> store->store_dir --> .
	 */
	tbdir = table->table_dir;
	if (tbdir == NULL)
		tbdir = table->store->store_dir;
	if (tbdir == NULL)
		tbdir = ".";

	if (_van_path_isabsolute(file))
		rsize = snprintf(buf, buflen, file);
	else
		rsize = snprintf(buf, buflen, "%s/%s",
		    tbdir, file);

	if (rsize >= (buflen - 1))
		ret = VAN_BUFFER_SMALL;
	else if (rsize < 0)
		ret = _van_getsys_error();
	else
		ret = 0;

	return ret;
}


