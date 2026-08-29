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
#ifndef _VAN_CURSOR_H
#define _VAN_CURSOR_H

#if defined(__cplusplus)
extern "C" {
#endif

struct _van_operation {
	VAN_TABLE *table;
	VAN_TXN *txn;	/* The owning transaction, NULL if non-transactional */
	TAILQ_ENTRY(_van_operation) links;

	ticks_t create_ticks;

	VAN_DATUM retkey;	/* UNUSED currently */
	VAN_DATUM retdata;	/* UNUSED currently */

	int (*get)(VAN_OPERATION *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*put)(VAN_OPERATION *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*del)(VAN_OPERATION *, VAN_DATUM *, flags_t);

#define	VAN_OP_INITED		0x01
#define VAN_OP_TRANSIENT	0x02
	flags_t flags;
};

#ifdef __cplusplus
}
#endif

#endif
