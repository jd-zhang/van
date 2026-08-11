#ifndef _VAN_TXN_H
#define _VAN_TXN_H

struct _van_txn {
	uint32	txnid;
	VAN_LSN	first_lsn;
	VAN_LSN	last_lsn;

	INMEM_TREE *tree;
};

#endif
