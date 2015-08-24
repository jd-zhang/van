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
		func(firstarg, keys[i], values[i]);
	}

	return (ret);
}


int _van_inmemtree_process_leaf_backward(INMEM_BLEAF *leaf,
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

	for (i = leaf->size; i > 0; i--) {
		func(firstarg, keys[i-1], values[i-1]);
	}

	return (ret);
}

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
		leaf = leaf->next;
	}

	return (ret);

}

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
		leaf = leaf->prev;
	}

	return (ret);

}
