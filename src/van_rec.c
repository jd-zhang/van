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

#include "van_rec.h"

/*
 * Record specs for the log records, mirroring what the logspec
 * generator produces. Currently maintained by hand.
 */

/* src/inmemtree/inmemtree.logspec */
VAN_LOG_RECSPEC _inmemtree_put_recspec[] = {
	{ LOGREC_TABLE,	offsetof(_inmemtree_put_args, tabid), "tabid", { 'l', 'u', 0, 0 } },
	{ LOGREC_DATUM,	offsetof(_inmemtree_put_args, key), "key", { 's', 0, 0, 0 } },
	{ LOGREC_DATUM,	offsetof(_inmemtree_put_args, data), "data", { 's', 0, 0, 0 } },
	{ LOGREC_Done, 0, NULL, { 0, 0, 0, 0 } }
};

VAN_LOG_RECSPEC _inmemtree_del_recspec[] = {
	{ LOGREC_TABLE,	offsetof(_inmemtree_del_args, tabid), "tabid", { 'l', 'u', 0, 0 } },
	{ LOGREC_DATUM,	offsetof(_inmemtree_del_args, key), "key", { 's', 0, 0, 0 } },
	{ LOGREC_Done, 0, NULL, { 0, 0, 0, 0 } }
};

/* src/txn/txn.logspec */
VAN_LOG_RECSPEC _txn_ckpt_recspec[] = {
	{ LOGREC_LSN,	offsetof(_txn_ckpt_args, ckplsn), "ckplsn", { 'l', 'u', 0, 0 } },
	{ LOGREC_LSN,	offsetof(_txn_ckpt_args, lastckp), "lastckp", { 'l', 'u', 0, 0 } },
	{ LOGREC_TIME,	offsetof(_txn_ckpt_args, timestamp), "timestamp", { 'l', 'u', 0, 0 } },
	{ LOGREC_Done, 0, NULL, { 0, 0, 0, 0 } }
};

VAN_LOG_RECSPEC _txn_begin_recspec[] = {
	{ LOGREC_TXN,	offsetof(_txn_begin_args, parent_id), "parent_id", { 'l', 'u', 0, 0 } },
	{ LOGREC_TIME,	offsetof(_txn_begin_args, timestamp), "timestamp", { 'l', 'u', 0, 0 } },
	{ LOGREC_Done, 0, NULL, { 0, 0, 0, 0 } }
};

VAN_LOG_RECSPEC _txn_end_recspec[] = {
	{ LOGREC_OP,	offsetof(_txn_end_args, op), "op", { 'l', 'u', 0, 0 } },
	{ LOGREC_TIME,	offsetof(_txn_end_args, timestamp), "timestamp", { 'l', 'u', 0, 0 } },
	{ LOGREC_Done, 0, NULL, { 0, 0, 0, 0 } }
};

/*
 * _van_calc_logrec_size --
 *	Compute the size of a log record, i.e. common header + record
 *	body. The varargs must be in the same order as _van_put_logrecord
 *	consumes them: one VAN_DATUM * for each LOGREC_DATUM field.
 */
uint32 _van_calc_logrec_size(VAN_LOG_RECSPEC *spec, ...) {
	va_list argp;
	uint32 size;
	VAN_DATUM *datav;

	va_start(argp, spec);
	size = VAN_LOGREC_COMSIZE;
	for (; spec->type != LOGREC_Done; spec++) {
		switch (spec->type) {
		case LOGREC_TABLE:
		case LOGREC_TXN:
		case LOGREC_ARG:
		case LOGREC_TIME:
		case LOGREC_OP:
			size += sizeof(uint32);
			break;
		case LOGREC_LSN:
			size += sizeof(VAN_LSN);
			break;
		case LOGREC_DATUM:
			datav = va_arg(argp, VAN_DATUM *);
			size += sizeof(uint32) + datav->size;
			break;
		default:
			break;
		}
	}
	va_end(argp);
	return (VAN_ALIGN(size, 8));
}
