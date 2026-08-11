#ifndef _TXN_EXT_H
#define _TXN_EXT_H

/* src/txn/txn_rec.c */
extern int _txn_ckpt_recover(VAN_STORE *, VAN_DATUM *,
    VAN_LSN *, van_recops, void *);
extern int _txn_begin_recover(VAN_STORE *, VAN_DATUM *,
    VAN_LSN *, van_recops, void *);
extern int _txn_end_recover(VAN_STORE *, VAN_DATUM *,
    VAN_LSN *, van_recops, void *);

#endif
