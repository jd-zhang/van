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
#ifndef _DATUM_EXT_H
#define _DATUM_EXT_H

/* src/datum/datum.c */
extern size_t _van_datum_size(const VAN_DATUM *ptr);
extern int _van_datum_clone(const VAN_DATUM *src, VAN_DATUM **dstp);
extern int _van_datum_copy(const VAN_DATUM *src, VAN_DATUM *dst);
extern int _van_datum_copyfree(VAN_DATUM *dst);
extern int _van_datum_clone_once(const VAN_DATUM *src, VAN_DATUM **dstp);
extern void _van_datum_free(VAN_DATUM *ptr);
extern void _van_datum_free_once(VAN_DATUM *ptr);
extern void _van_data_free_data(VAN_DATUM *ptr);
extern int _van_datum_replace(VAN_DATUM *ptr, void *newd, size_t newlen);
extern int _van_datum_uercopy(void *d, size_t len, VAN_DATUM **vdp);
extern int _van_datum_defcmp(const VAN_DATUM *dt1, const VAN_DATUM *dt2);
extern int _van_table_datum_defcmp(const VAN_TABLE *table,
    const VAN_DATUM *dt1, const VAN_DATUM *dt2);

#endif
