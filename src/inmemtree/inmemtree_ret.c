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

#include "van_inmem_tree.h"

int _van_inmemtree_ret(INMEM_TREECURSOR *cursor, const VAN_DATUM *datum,
    VAN_DATUM *rdatum, int retype, flags_t flags) {
	int handled, ret;
	VAN_DATUM *dat;
	size_t limit;

	handled = 0;
	ret = 0;
	dat = NULL;
	limit = 0;

	/* Processing VAN_MALLOC, VAN_REALLOC and VAN_USERMEM. */
	switch (rdatum->flags & VAN_MEM_MASK) {
		case VAN_MALLOC:
		case VAN_REALLOC:
		case VAN_USERMEM:
			ret = _van_datum_copy(datum, rdatum);
			handled = 1;
			break;
		case 0:
			break;
		default:
			handled = 1;
			ret = _van_error_path(
			    "Invalid flag to _van_inmemtree_ret!");
			break;
	}

	if (handled || ret != 0)
		return (ret);

	switch (retype) {
		case RET_KEY:
			limit = cursor->defretkey->ulen;
			if (datum->size > limit)
				dat = cursor->reakey;
			else
				dat = cursor->defretkey;
			break;
		case RET_VALUE:
			limit = cursor->defretdata->ulen;
			if (datum->size > limit)
				dat = cursor->readata;
			else
				dat = cursor->defretdata;
			break;
		default:
			ret = _van_error_path(
			    "Invalid retype to _van_inmemtree_ret!");
			break;
	}

	if (ret != 0)
		return (ret);

	/*
	 * This means if the size is first too big, then smaller, the
	 * used memory will shrinks. The bad is that, if the sizes is
	 * just a little larger, it will also shrink, not really
	 * necessary.
	 */
	if (datum->size > limit) {
		ret = _van_realloc(NULL, dat->data, datum->size, &dat->data);
		if (ret != 0)
			return (ret);
		dat->ulen = datum->size;
	}
	
	dat->size = datum->size;
	memcpy(dat->data, datum->data, datum->size);
	*rdatum = *dat;

	return (ret);
}
