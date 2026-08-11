#include "van_int.h"
#include "van.h"
/*
 * _inmemtree_put_recover --
 *	Recovery function for put.
 *
 * PUBLIC: int _inmemtree_put_recover
 * PUBLIC:   (VAN_STORE *, VAN_DATUM *, VAN_LSN *, van_recops, void *);
 */
int
_inmemtree_put_recover(store, datp, lsnp, op, info)
	VAN_STORE *store;
	VAN_DATUM *datp;
	VAN_LSN *lsnp;
	van_recops op;
	void *info;
{
	_inmemtree_put_args *argp;
	VAN_TABLE *tablep;
	int ret;

	ret = 0;

out:	
	return ret;
}

/*
 * _inmemtree_del_recover --
 *	Recovery function for del.
 *
 * PUBLIC: int _inmemtree_del_recover
 * PUBLIC:   (VAN_STORE *, VAN_DATUM *, VAN_LSN *, van_recops, void *);
 */
int
_inmemtree_del_recover(store, datp, lsnp, op, info)
	VAN_STORE *store;
	VAN_DATUM *datp;
	VAN_LSN *lsnp;
	van_recops op;
	void *info;
{
	_inmemtree_del_args *argp;
	VAN_TABLE *tablep;
	int ret;

	ret = 0;

out:	
	return ret;
}

