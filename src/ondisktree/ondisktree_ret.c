#include "van_int.h"
#include "van.h"

#include "van_ondisk_tree.h"

int _van_ondisktree_ret(ONDISK_TREECURSOR *cursor, const ONDISK_ITEM *k2,
    VAN_DATUM *rdatum, int retype, flags_t flags) {
	int handled, ret, isfile;
	VAN_DATUM datum, *dat;
	size_t limit, fsize;

	handled = 0;
	ret = 0;
	dat = NULL;
	limit = 0;
	isfile = 0;
	memset(&datum, 0, sizeof(VAN_DATUM));
	    
	switch(k2->type) {
		case OD_VARCHAR:
			datum.data = (void *)k2->data;
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
		return (ret);

	/* Processing VAN_MALLOC, VAN_REALLOC and VAN_USERMEM. */
	switch (rdatum->flags & VAN_MEM_MASK) {
		case VAN_MALLOC:
		case VAN_REALLOC:
		case VAN_USERMEM:
			/* 
			 * TODO: optimization,
			 * when it is file, there is no need
			 * to copy, since it is malloc'd,
			 * we can just return it.
			 * But this means some changes
			 * to the clean action, and maybe others.
			 */
			ret = _van_datum_copy(&datum, rdatum);
			handled = 1;
			break;
		case 0:
			break;
		default: 
			handled = 1;
			ret = _van_error_path(
			    "Invalid flag to _van_ondisktree_ret!");
			break;
	}
	if (handled || ret != 0)
		goto clean;

	switch (retype) {
		case RET_KEY:
			limit = cursor->defretkey->ulen;
			if (datum.size > limit)
				dat = cursor->reakey;
			else
				dat = cursor->defretkey;
			break;
		case RET_VALUE:
			limit = cursor->defretdata->ulen;
			if (datum.size > limit)
				dat = cursor->readata;
			else
				dat = cursor->defretdata;
			break;
		default:
			ret = _van_error_path(
			    "Invalid retype to _van_ondisktree_ret!");
			break;
	}
	if (ret != 0)
		goto clean;

	if (datum.size > limit) {
		ret = _van_realloc(NULL, dat->data, datum.size, &dat->data);
		if (ret != 0)
			goto clean;
		dat->ulen = datum.size;
	}
	
	dat->size = datum.size;
	memcpy(dat->data, datum.data, datum.size);
	*rdatum = *dat;

clean:
	if (isfile) {
		_van_free(NULL, datum.data);
		datum.data = NULL;
	}

	return (ret);
}

