/*
 * Copyright (c) 2026 VanDB Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _VAN_TXN_H
#define _VAN_TXN_H

#if defined(__cplusplus)
extern "C" {
#endif

struct _van_txn {
	uint32	txnid;
	VAN_LSN	first_lsn;
	VAN_LSN	last_lsn;

	VAN_TABLE *table;
	INMEM_TREE *tree;
};

#define VAN_TXN_TRANSIENT	0x01	/* Do not write log records */

/* Transaction interfaces */
int van_txn_begin(VAN_TABLE *table, flags_t flags, VAN_TXN **txnpp);
int van_txn_commit(VAN_TXN *txnp, flags_t flags);
int van_txn_abort(VAN_TXN *txnp, flags_t flags);
int van_txn_put(VAN_TXN *txnp, VAN_DATUM *key, VAN_DATUM *data,
    flags_t flags);
int van_txn_get(VAN_TXN *txnp, VAN_DATUM *key, VAN_DATUM *data,
    flags_t flags);
int van_txn_del(VAN_TXN *txnp, VAN_DATUM *key, flags_t flags);

#ifdef __cplusplus
}
#endif

#endif
