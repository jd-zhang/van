#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"

int _van_inmemtree_config_alloc(INMEM_CONFIG **dstp) {
	int ret;
	INMEM_CONFIG *dst;

	VAN_ASSERT(dstp != NULL);
	/* Just do allocation current */
	ret = _van_calloc(NULL, 1, sizeof(INMEM_CONFIG), &dst);
	if (ret != 0)
		return (ret);

	dst->node_capacity = IM_MAX_NENTRIES;
	dst->base_level = IM_BASE_LEVEL;
	dst->split_point = IM_DEF_SPLIT_POINT;
	dst->cmp_func = _van_datum_defcmp;

	*dstp = dst;
	return (ret);
}

int _van_inmemtree_config_destroy(INMEM_CONFIG *dst) {
	int ret;
	
	ret = 0;
	if (dst == NULL)
		return (ret);
	/* Just do feee currently */
	_van_free(NULL, dst);

	return (ret);
}

int _van_inmemtree_config_copy(INMEM_CONFIG *src, INMEM_CONFIG *dst) {
	int ret;

	VAN_ASSERT(src != NULL && dst != NULL);
	ret = 0;

	dst->node_capacity = src->node_capacity;
	dst->base_level = src->base_level;
	dst->split_point = src->split_point;
	dst->cmp_func = src->cmp_func;

	return (ret);
}

int _van_inmemtree_config_alloccopy(INMEM_CONFIG *src, INMEM_CONFIG **dstp) {
	int ret;
	INMEM_CONFIG *dst;
	VAN_ASSERT(src != NULL && dstp != NULL);

	ret = _van_calloc(NULL, 1, sizeof(INMEM_CONFIG), &dst);
	if (ret != 0)
		return (ret);

	ret = _van_inmemtree_config_copy(src, dst);
	/* Must return 0 currently */
	VAN_ASSERT(ret == 0);
	*dstp = dst;

	return (ret);
}

int _van_inmemtree_set_max_entries(INMEM_TREE *tree, size_t max) {
	int ret;
	INMEM_CONFIG *config;

	config = tree->config;
	VAN_ASSERT(config != NULL);

	ret = 0;
	config->node_capacity = max;

	return (ret);
}

int _van_inmemtree_set_split_point(INMEM_TREE *tree, size_t point) {
	int ret;
	INMEM_CONFIG *config;

	config = tree->config;
	VAN_ASSERT(config != NULL);

	ret = 0;
	config->split_point = point;

	return (ret);
}

int _van_inmemtree_set_key_compare(INMEM_TREE *tree,
    VAN_DATUM_CMPFUNC func) {
	int ret;
	INMEM_CONFIG *config;

	config = tree->config;
	VAN_ASSERT(config != NULL);

	ret = 0;
	/* 
	 * Passing null means to set it back to default.
	 * But in fact, it does not make sense.
	 */
	if (func == NULL)
		ret = VAN_INVALID;
	else
		config->cmp_func = func;

	return (ret);
}
