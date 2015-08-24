#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"
#include "van_ondisk_tree.h"

int _van_ondiskitem_from_inmemkey(ONDISK_TREE *tree, const INMEM_KEY *key,
    ONDISK_ITEM **itemp) {
	ONDISK_ITEM *item;
	VAN_DATUM datum;
	int ret, isfile;
	size_t size;

	isfile = 0;
	ret = 0;
	memset(&datum, 0, sizeof(VAN_DATUM));
	item = NULL;

	if (key == NULL) {
		/* Just use the empty datum */
	} else if (key->v->size >= tree->blob_threshold) {
		isfile = 1;
		/* TODO: Save the data to a file */
		/* Should set the datum. */
	} else {
		datum = *(key->v);
	}
	size = P_ITEMSIZE(datum.size);
	
	ret = _van_malloc(NULL, size, &item);
	if (ret != 0)
		goto clean;
	if (isfile)
		item->type = OD_FILE;
	else
		item->type = OD_VARCHAR;
	item->flags = key->flags;
	item->len = datum.size;
	if (datum.size > 0)
		memcpy(item->data, datum.data, datum.size);

	*itemp = item;

	if (0) {
clean:		if (isfile) {
			/* TODO: Delete the file here */
		}
	}
	return (ret);
}

/* 
 * Currently, it has same implementation with 
 * _van_ondiskitem_from_inmemkey, but it may change later.
 */
int _van_ondiskitem_from_inmemdata(ONDISK_TREE *tree, const INMEM_DATA *data,
    ONDISK_ITEM **itemp) {
	ONDISK_ITEM *item;
	VAN_DATUM datum;
	int ret, isfile;
	size_t size;

	isfile = 0;
	ret = 0;
	memset(&datum, 0, sizeof(VAN_DATUM));

	if (data == NULL) {
		/* Just use the empty datum. */
	} else if (data->v->size >= tree->blob_threshold) {
		isfile = 1;
		/* TODO: Save the data to a file */
		/* Should set the datum */
	} else {
		datum = *(data->v);
	}
	size = P_ITEMSIZE(datum.size);
	
	ret = _van_malloc(NULL, size, &item);
	if (ret != 0)
		goto clean;
	if (isfile)
		item->type = OD_FILE;
	else
		item->type = OD_VARCHAR;
	item->flags = data->flags;
	item->len = datum.size;
	if (datum.size > 0)
		memcpy(item->data, datum.data, datum.size);

	*itemp = item;

	if (0) {
clean:		if (isfile) {
			/* Delete the file here */
		}
	}
	return (ret);
}

int _van_ondiskitem_alloccopy(const ONDISK_ITEM *srcitem,
    ONDISK_ITEM **destitemp) {
	size_t size;
	int ret;
	ONDISK_ITEM *item;

	if (srcitem == NULL)
		size = P_ITEMSIZE(0);
	else
		size = P_ITEMSIZE(srcitem->len);
	
	ret = _van_malloc(NULL, size, &item);
	if (ret != 0)
		return (ret);
	memcpy(item, srcitem, size);

	*destitemp = item;

	return(ret);
}


