#include "van_int.h"
#include "van.h"
/*
 * _store_tabopen_recover --
 *	Recovery function for tabopen.
 *
 * PUBLIC: int _store_tabopen_recover
 * PUBLIC:   (VAN_STORE *, VAN_DATUM *, VAN_LSN *, van_recops, void *);
 */
int
_store_tabopen_recover(store, datp, lsnp, op, info)
	VAN_STORE *store;
	VAN_DATUM *datp;
	VAN_LSN *lsnp;
	van_recops op;
	void *info;
{
	_store_tabopen_args *argp;
	VAN_TABLE *tablep;
	int ret;

	ret = 0;

out:	
	return ret;
}

/*
 * _store_tabids_recover --
 *	Recovery function for tabids.
 *
 * PUBLIC: int _store_tabids_recover
 * PUBLIC:   (VAN_STORE *, VAN_DATUM *, VAN_LSN *, van_recops, void *);
 */
int
_store_tabids_recover(store, datp, lsnp, op, info)
	VAN_STORE *store;
	VAN_DATUM *datp;
	VAN_LSN *lsnp;
	van_recops op;
	void *info;
{
	_store_tabids_args *argp;
	VAN_TABLE *tablep;
	int ret;

	ret = 0;

out:	
	return ret;
}

