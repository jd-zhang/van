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

int _van_inmemtree_key_docmp(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, INMEM_KEY *k2) {
	VAN_DATUM_CMPFUNC datum_cmp;
	int cmp;

	datum_cmp = cursor->im_tree->config->cmp_func;
	cmp = datum_cmp(key, k2->v);
	return (cmp);
}

int _van_inmemtree_compare(INMEM_TREECURSOR *cursor, VAN_DATUM *key) {
	INMEM_KEY *kc;
	INMEM_BINTERNAL *internal;
	INMEM_BLEAF *leaf;
	size_t indx;

	indx = cursor->indx;
	if (cursor->node->type == NT_IM_BINTERNAL) {
		internal = (INMEM_BINTERNAL *)cursor->node;
		kc = internal->keys[indx];
	} else {
		leaf = (INMEM_BLEAF *)cursor->node;
		kc = leaf->keys[indx];
	}
	return _van_inmemtree_key_docmp(cursor,
	    key, kc == NULL ? NULL : kc);
}
