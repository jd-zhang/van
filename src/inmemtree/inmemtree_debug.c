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

int _van_inmemtree_dump_leaf(INMEM_TREE *im_tree,
    INMEM_BLEAF *leaf, FILE *f) {
	int ret;
	size_t i;
	INMEM_KEY **keys;
	INMEM_DATA **values;

	ret = 0;
	fprintf(f, "=== leaf(0x%lx) ===\n", (unsigned long)leaf);
	if (leaf == NULL || leaf->size == 0)
		return (ret);

	keys = leaf->keys;
	values = leaf->values;
	for (i = 0; i < leaf->size; i++) {
		fprintf(f, "(key)%s:(value)%s\n",
		    (keys[i] == NULL || keys[i]->v.data == NULL) ? "(null)" :
		    (char *)keys[i]->v.data, (values[i] == NULL ||
		    values[i]->v.data == NULL) ? "(null)" :
		    (char *)values[i]->v.data);
	}

	return (ret);
}

int _van_inmemtree_dump_internal(INMEM_TREE *im_tree,
    INMEM_BINTERNAL *internal, int recursive, FILE *f) {
	int ret;
	size_t i;
	INMEM_KEY **keys;
	INMEM_CHILD **childs;

	ret = 0;
	fprintf(f, "=== internal(0x%lx) ===\n", (unsigned long)internal);
	if (internal == NULL || internal->size == 0)
		return (ret);

	keys = internal->keys;
	childs = internal->childs;
	for (i = 0; i < internal->size; i++) {
		fprintf(f, "(key)%s:(value)0x%lx\n",
		    (keys[i] == NULL || keys[i]->v.data == NULL) ? "(null)" :
		    (char *)keys[i]->v.data, (childs[i] == NULL ||
		    childs[i]->pointer == NULL) ? 0 :
		    (unsigned long)childs[i]->pointer);
		if (recursive && (childs[i] != NULL) && childs[i]->pointer != NULL) {
			ret = _van_inmemtree_dump_node(im_tree,
			    childs[i]->pointer, recursive, f);
			if (ret != 0)
				break;
		}
	}

	return (ret);

}

int _van_inmemtree_dump_node(INMEM_TREE *im_tree,
    INMEM_BNODE *node, int recursive, FILE *f) {
	int ret;
	INMEM_BINTERNAL *internal;
	INMEM_BLEAF *leaf;

	ret = 0;
	if (node == NULL || node->size == 0)
		return (ret);

	if (node->type == NT_IM_BINTERNAL) {
		internal = (INMEM_BINTERNAL *)node;
		ret = _van_inmemtree_dump_internal(im_tree,
		    internal, recursive, f);
	} else if (node->type == NT_IM_BLEAF) {
		leaf = (INMEM_BLEAF *)node;
		ret = _van_inmemtree_dump_leaf(im_tree, leaf, f);

	} else {
		ret = _van_error_print("unknown node type!");
	}

	return (ret);
}

int _van_inmemtree_dump_all_leaves(INMEM_TREE *im_tree, FILE *f) {
	int ret;
	INMEM_BLEAF *leaf;

	ret = 0;
	if (im_tree == NULL)
		return (ret);

	leaf = im_tree->first;
	while (leaf != NULL) {
		ret = _van_inmemtree_dump_leaf(im_tree, leaf, f);
		if (ret != 0)
			break;
		leaf = leaf->next;
	}

	return (ret);
}

int _van_inmemtree_dump_tree(INMEM_TREE *im_tree, FILE *f) {
	int ret;

	ret = 0;
	if (im_tree == NULL)
		return (ret);

	ret = _van_inmemtree_dump_internal(im_tree, im_tree->root,
	    1, f);

	return (ret);
}

