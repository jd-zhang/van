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
