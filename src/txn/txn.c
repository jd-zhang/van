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
#include "van_int.h"
#include "van.h"

#include "van_txn.h"
#include "van_rec.h"

/*
 * __van_txn_timestamp --
 *	Return the table's current tick as the record timestamp.
 */
static int __van_txn_timestamp(VAN_TABLE *table, uint32 *timestamp) {
	int ret;
	ticks_t ticks;

	ret = _van_table_curticks(table, &ticks);
	if (ret != 0)
		return (ret);
	*timestamp = (uint32)ticks;

	return (ret);
}

/*
 * van_txn_begin --
 *	Begin a transaction and write a begin log record.
 *
 *	Unless VAN_TXN_TRANSIENT is set in flags, a VAN_TXN_BEGIN record
 *	is written so that the transaction survives a crash until commit
 *	or abort. The txn id is allocated store-wide to keep log records
 *	unambiguous when multiple tables share one log.
 */
int van_txn_begin(VAN_TABLE *table, flags_t flags, VAN_TXN **txnpp) {
	int ret;
	VAN_TXN *txnp;
	VAN_LSN lsn;
	uint32 timestamp;
	uint32 size;

	ret = 0;
	txnp = NULL;
	INIT_LSN(&lsn);

	if (table == NULL || txnpp == NULL)
		return (VAN_INVALID);

	ret = _van_calloc(NULL, 1, sizeof(VAN_TXN), &txnp);
	if (ret != 0)
		return (ret);

	txnp->table = table;
	txnp->tree = table->active;
	txnp->txnid = atomic_fetch_add(&table->store->txnid, 1);

	if (!(flags & VAN_TXN_TRANSIENT)) {
		ret = __van_txn_timestamp(table, &timestamp);
		if (ret != 0)
			goto err;
		size = _van_calc_logrec_size(_txn_begin_recspec, 0, timestamp);
		ret = _van_put_logrecord(table->store, table, txnp, &lsn,
		    flags, VAN_TXN_BEGIN, size, _txn_begin_recspec,
		    0, timestamp);
		if (ret != 0)
			goto err;
		txnp->first_lsn = lsn;
		txnp->last_lsn = lsn;
	}

	*txnpp = txnp;
	return (ret);

err:
	if (txnp != NULL)
		_van_free(NULL, txnp);
	return (ret);
}

/*
 * van_txn_commit --
 *	Commit a transaction: write an end(COMMIT) log record and flush
 *	the log so the commit is durable. Then free the transaction.
 */
int van_txn_commit(VAN_TXN *txnp, flags_t flags) {
	int ret;
	VAN_TABLE *table;
	VAN_LSN lsn;
	uint32 timestamp;
	uint32 size;

	ret = 0;
	if (txnp == NULL)
		return (VAN_INVALID);
	table = txnp->table;
	INIT_LSN(&lsn);

	if (!(flags & VAN_TXN_TRANSIENT)) {
		ret = __van_txn_timestamp(table, &timestamp);
		if (ret != 0)
			goto err;
		size = _van_calc_logrec_size(_txn_end_recspec,
		    VAN_OP_COMMIT, timestamp);
		ret = _van_put_logrecord(table->store, table, txnp, &lsn,
		    flags, VAN_TXN_END, size, _txn_end_recspec,
		    VAN_OP_COMMIT, timestamp);
		if (ret != 0)
			goto err;

		/* Flush the log so that the commit is durable. */
		ret = _van_log_flush(table->store->log_handle);
		if (ret != 0)
			goto err;
	}

	_van_free(NULL, txnp);
	return (ret);

err:
	return (ret);
}

/*
 * van_txn_abort --
 *	Abort a transaction: write an end(ABORT) log record and free the
 *	transaction. The record does not need to be flushed, recovery
 *	never replays a transaction without end(COMMIT).
 */
int van_txn_abort(VAN_TXN *txnp, flags_t flags) {
	int ret;
	VAN_TABLE *table;
	VAN_LSN lsn;
	uint32 timestamp;
	uint32 size;

	ret = 0;
	if (txnp == NULL)
		return (VAN_INVALID);
	table = txnp->table;
	INIT_LSN(&lsn);

	if (!(flags & VAN_TXN_TRANSIENT)) {
		ret = __van_txn_timestamp(table, &timestamp);
		if (ret != 0)
			goto err;
		size = _van_calc_logrec_size(_txn_end_recspec,
		    VAN_OP_ABORT, timestamp);
		ret = _van_put_logrecord(table->store, table, txnp, &lsn,
		    flags, VAN_TXN_END, size, _txn_end_recspec,
		    VAN_OP_ABORT, timestamp);
		if (ret != 0)
			goto err;
	}

	_van_free(NULL, txnp);
	return (ret);

err:
	return (ret);
}

/*
 * van_txn_put --
 *	Insert a key/data pair within a transaction. The operation is
 *	binded to the transaction so its WAL record carries the txnid.
 */
int van_txn_put(VAN_TXN *txnp, VAN_DATUM *key, VAN_DATUM *data,
    flags_t flags) {
	int ret, ret1;
	VAN_TABLE *table;
	VAN_OPERATION *op;
	ticks_t tk;

	ret = 0;
	op = NULL;
	if (txnp == NULL)
		return (VAN_INVALID);
	table = txnp->table;

	ret = _van_table_curticks(table, &tk);
	if (ret != 0)
		return (ret);
	ret = _van_table_op_create(table, &op, 0, tk);
	if (ret != 0)
		return (ret);
	op->txn = txnp;

	ret = _van_table_op_put(op, key, data, flags);
	ret1 = _van_table_op_close(op);
	SET_RET(ret1, ret);
	return (ret);
}

/*
 * van_txn_get --
 *	Fetch a record within a transaction.
 */
int van_txn_get(VAN_TXN *txnp, VAN_DATUM *key, VAN_DATUM *data,
    flags_t flags) {
	int ret, ret1;
	VAN_TABLE *table;
	VAN_OPERATION *op;
	ticks_t tk;

	ret = 0;
	op = NULL;
	if (txnp == NULL)
		return (VAN_INVALID);
	table = txnp->table;

	ret = _van_table_curticks(table, &tk);
	if (ret != 0)
		return (ret);
	ret = _van_table_op_create(table, &op, 0, tk);
	if (ret != 0)
		return (ret);
	op->txn = txnp;

	ret = _van_table_op_get(op, key, data, flags);
	ret1 = _van_table_op_close(op);
	SET_RET(ret1, ret);
	if (ret == VAN_DELETED)
		ret = VAN_NOTFOUND;
	return (ret);
}

/*
 * van_txn_del --
 *	Delete a record within a transaction.
 */
int van_txn_del(VAN_TXN *txnp, VAN_DATUM *key, flags_t flags) {
	int ret, ret1;
	VAN_TABLE *table;
	VAN_OPERATION *op;
	ticks_t tk;

	ret = 0;
	op = NULL;
	if (txnp == NULL)
		return (VAN_INVALID);
	table = txnp->table;

	ret = _van_table_curticks(table, &tk);
	if (ret != 0)
		return (ret);
	ret = _van_table_op_create(table, &op, 0, tk);
	if (ret != 0)
		return (ret);
	op->txn = txnp;

	ret = _van_table_op_del(op, key, flags);
	ret1 = _van_table_op_close(op);
	SET_RET(ret1, ret);
	if (ret == VAN_DELETED)
		ret = VAN_NOTFOUND;
	return (ret);
}
