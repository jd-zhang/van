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
#ifndef _VAN_DATUM_H
#define _VAN_DATUM_H

struct _van_datum {
	void *data;
	size_t size;
	size_t ulen;
	flags_t flags;
};
/* Comparison function definition */
typedef int (*VAN_DATUM_CMPFUNC)(const VAN_DATUM *dt1, const VAN_DATUM *dt2);
typedef int (*VAN_TABLE_CMPFUNC)(const VAN_TABLE *table,
    const VAN_DATUM *dt1, const VAN_DATUM *dt2);

#endif
