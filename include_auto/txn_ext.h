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
