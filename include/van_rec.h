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
#ifndef _VAN_REC_H
#define _VAN_REC_H

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Argument structures and record specs for the log records.
 *
 * These mirror what the logspec generator would produce for
 * src/txn/txn.logspec and src/inmemtree/inmemtree.logspec. Since the
 * generator is not in-tree, they are maintained by hand (see
 * src/van_rec.c). They are used when writing records (via
 * _van_put_logrecord) and will be used when reading them back during
 * recovery.
 */

/* src/txn/txn.logspec: ckpt -- ckplsn, lastckp, timestamp */
typedef struct _txn_ckpt_args {
	VAN_LSN	ckplsn;
	VAN_LSN	lastckp;
	uint32	timestamp;
} _txn_ckpt_args;

/* src/txn/txn.logspec: begin -- parent_id, timestamp */
typedef struct _txn_begin_args {
	uint32	parent_id;
	uint32	timestamp;
} _txn_begin_args;

/* src/txn/txn.logspec: end -- op, timestamp */
typedef struct _txn_end_args {
	uint32	op;
	uint32	timestamp;
} _txn_end_args;

/* src/inmemtree/inmemtree.logspec: put -- tabid, key, data */
typedef struct _inmemtree_put_args {
	uint32	tabid;
	VAN_DATUM key;
	VAN_DATUM data;
} _inmemtree_put_args;

/* src/inmemtree/inmemtree.logspec: del -- tabid, key */
typedef struct _inmemtree_del_args {
	uint32	tabid;
	VAN_DATUM key;
} _inmemtree_del_args;

/* Record specs */
extern VAN_LOG_RECSPEC _inmemtree_put_recspec[];
extern VAN_LOG_RECSPEC _inmemtree_del_recspec[];
extern VAN_LOG_RECSPEC _txn_ckpt_recspec[];
extern VAN_LOG_RECSPEC _txn_begin_recspec[];
extern VAN_LOG_RECSPEC _txn_end_recspec[];

/*
 * Compute the total size (common header + record body) of a log record.
 * The varargs must match the arguments passed to _van_put_logrecord:
 * a VAN_DATUM * for each LOGREC_DATUM field.
 */
extern uint32 _van_calc_logrec_size(VAN_LOG_RECSPEC *spec, ...);

#ifdef __cplusplus
}
#endif

#endif
