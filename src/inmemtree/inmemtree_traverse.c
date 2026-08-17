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

int _van_inmemtree_process_leaf_forward(INMEM_BLEAF *leaf,
    INMEM_TRAVERSE_FUNC func, void *firstarg) {
	int ret;
	size_t i;
	INMEM_KEY **keys;
	INMEM_DATA **values;

	ret = 0;
	if (leaf == NULL || leaf->size == 0)
		return (ret);

	keys = leaf->keys;
	values = leaf->values;

	for (i = 0; i < leaf->size; i++) {
		ret = func(firstarg, keys[i], values[i]);
		if (ret != 0)
			break;
	}

	return (ret);
}


int _van_inmemtree_process_leaf_backward(INMEM_BLEAF *leaf,
    INMEM_TRAVERSE_FUNC func, void *firstarg) {
	int ret;
	int i;
	INMEM_KEY **keys;
	INMEM_DATA **values;

	ret = 0;
	if (leaf == NULL || leaf->size == 0)
		return (ret);

	keys = leaf->keys;
	values = leaf->values;

	for (i = (int)leaf->size; i > 0; i--) {
		ret = func(firstarg, keys[i-1], values[i-1]);
		if (ret != 0)
			break;
	}

	return (ret);
}

/*
 * Traverse the tree forward, from the first leaf to the last leaf.
 * The function will be called for each key-value pair in the tree.
 * If the function returns a non-zero value, the traversal will stop.
 *
 * This function is run without locks, because it is running on a
 * frozen tree, which is not modified by any other thread.
*/
int _van_inmemtree_traverse_forard(INMEM_TREE *tree,
    INMEM_TRAVERSE_FUNC func, void *firstarg) {

	int ret;
	INMEM_BLEAF *leaf;
	
	ret = 0;
	if (tree == NULL)
		return (ret);

	leaf = tree->first;
	while (leaf != NULL) {
		ret = _van_inmemtree_process_leaf_forward(leaf,
		    func, firstarg);
		if (ret != 0)
			break;
		leaf = leaf->next;
	}

	return (ret);

}

/*
 * Traverse the tree backward, from the last leaf to the first leaf.
 * The function will be called for each key-value pair in the tree.
 * If the function returns a non-zero value, the traversal will stop.
 *
 * This function is run without locks, because it is running on a
 * frozen tree, which is not modified by any other thread.
*/
int _van_inmemtree_traverse_backward(INMEM_TREE *tree,
    INMEM_TRAVERSE_FUNC func, void *firstarg) {

	int ret;
	INMEM_BLEAF *leaf;
	
	ret = 0;
	if (tree == NULL)
		return (ret);

	leaf = tree->last;
	while (leaf != NULL) {
		ret = _van_inmemtree_process_leaf_backward(leaf,
		    func, firstarg);
		if (ret != 0)
			break;
		leaf = leaf->prev;
	}

	return (ret);

}
