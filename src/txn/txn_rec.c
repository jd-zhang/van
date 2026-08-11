#include "van_int.h"
#include "van.h"
/*
 * _txn_ckpt_recover --
 *	Recovery function for ckpt.
 *
 * PUBLIC: int _txn_ckpt_recover
 * PUBLIC:   (VAN_STORE *, VAN_DATUM *, VAN_LSN *, van_recops, void *);
 */
int
_txn_ckpt_recover(store, datp, lsnp, op, info)
	VAN_STORE *store;
	VAN_DATUM *datp;
	VAN_LSN *lsnp;
	van_recops op;
	void *info;
{
	_txn_ckpt_args *argp;
	VAN_TABLE *tablep;
	int ret;

	ret = 0;

out:	
	return ret;
}

/*
 * _txn_begin_recover --
 *	Recovery function for begin.
 *
 * PUBLIC: int _txn_begin_recover
 * PUBLIC:   (VAN_STORE *, VAN_DATUM *, VAN_LSN *, van_recops, void *);
 */
int
_txn_begin_recover(store, datp, lsnp, op, info)
	VAN_STORE *store;
	VAN_DATUM *datp;
	VAN_LSN *lsnp;
	van_recops op;
	void *info;
{
	_txn_begin_args *argp;
	VAN_TABLE *tablep;
	int ret;

	ret = 0;

out:	
	return ret;
}

/*
 * _txn_end_recover --
 *	Recovery function for end.
 *
 * PUBLIC: int _txn_end_recover
 * PUBLIC:   (VAN_STORE *, VAN_DATUM *, VAN_LSN *, van_recops, void *);
 */
int
_txn_end_recover(store, datp, lsnp, op, info)
	VAN_STORE *store;
	VAN_DATUM *datp;
	VAN_LSN *lsnp;
	van_recops op;
	void *info;
{
	_txn_end_args *argp;
	VAN_TABLE *tablep;
	int ret;

	ret = 0;

out:	
	return ret;
}

