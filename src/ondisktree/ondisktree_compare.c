#include "van_int.h"
#include "van.h"

#include "van_ondisk_tree.h"

int _van_ondisktree_key_docmp(ONDISK_TREECURSOR *cursor,
    VAN_DATUM *key, ONDISK_ITEM *k2, int *cmpp) {
	VAN_DATUM_CMPFUNC datum_cmp;
	VAN_DATUM datum;
	int ret, cmp, isfile;
	size_t fsize;
	
	ret = 0;
	isfile = 0;
	datum_cmp = cursor->table->active->config->cmp_func;
	memset(&datum, 0, sizeof(VAN_DATUM));

	switch(k2->type) {
		case OD_VARCHAR:
			datum.data = k2->data;
			datum.size = k2->len;
			break;
		case OD_FILE:
			ret = _van_file_openreadall(k2->data,
			    &datum.data, &fsize);
			if (ret != 0)
				break;
			datum.size = fsize;
			isfile = 1;
			break;
		default:
			ret = _van_error_path(
			    "Invalid type of data item!");
			break;

	}
	if (ret != 0)
		goto clean;

	cmp = datum_cmp(key, &datum);
	*cmpp = cmp;
clean:
	if (isfile) {
		_van_free(NULL, datum.data);
		datum.data = NULL;
	}
	return (ret);
}

int _van_ondisktree_compare(ONDISK_TREECURSOR *cursor,
    VAN_DATUM *key, int *cmpp) {
	ONDISK_ITEM *kc;
	ONDISK_HEADER *page;
	size_t indx;

	indx = cursor->indx;
	page = cursor->page;
	kc = P_KEY(page, indx);
	return _van_ondisktree_key_docmp(cursor,
	    key, kc->len == 0 ? NULL : kc, cmpp);
}

