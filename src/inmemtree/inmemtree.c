#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"

static int _van_inmemtree_put_invalid(INMEM_TREE *tree,
    VAN_DATUM *k, VAN_DATUM *v, flags_t flags);
static int _van_inmemtree_del_invalid(INMEM_TREE *tree,
    VAN_DATUM *k, flags_t flags);
static int _van_inmemtree_get(INMEM_TREE *tree,
    VAN_DATUM *k, VAN_DATUM *v, flags_t flags);
static int _van_inmemtree_put(INMEM_TREE *tree,
    VAN_DATUM *k, VAN_DATUM *v, flags_t flags);
static int _van_inmemtree_del(INMEM_TREE *tree,
    VAN_DATUM *k, flags_t flags);

int _van_inmemtree_create(VAN_TABLE *table, INMEM_TREE **treep) {
	INMEM_TREE *tree;
	INMEM_BLEAF *leaf;
	INMEM_BINTERNAL *internal;
	INMEM_CHILD *child;
	int ret;

	VAN_ASSERT(treep != NULL);
	tree = NULL;
	leaf = NULL;
	internal = NULL;
	child = NULL;
	ret = _van_calloc(NULL, 1, sizeof(INMEM_TREE), &tree);
	if (ret != 0)
		goto err;
	tree->table = table;

	ret = _van_inmemtree_config_alloc(&tree->config);
	if (ret != 0)
		goto err;

	INMEM_MUTEX_INIT(tree, mtx_nodelist, ret);
	if (ret != 0)
		goto err;
	tree->nlmtx_allocated = 1;
	TAILQ_INIT(&tree->nodelist);

	ret = _van_inmemtree_alloc_binternal(tree, &internal);
	if (ret != 0)
		goto err;

	ret = _van_inmemtree_alloc_bleaf(tree, &leaf);
	if (ret != 0)
		goto err;
	
	ret = _van_calloc(NULL, 1, sizeof(INMEM_CHILD), &child);
	if (ret != 0)
		goto err;

	/* The DATUM used for opeations */
	ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
	    &tree->defretkey);
	if (ret == 0) {
		tree->defretkey->ulen = DEF_KEY_ULEN;
		ret = _van_calloc(NULL, 1, DEF_KEY_ULEN,
		    &tree->defretkey->data);
	}
	if (ret == 0)
		ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
		    &tree->defretdata);
	if (ret == 0) {
		tree->defretdata->ulen = DEF_VAL_ULEN;
		ret = _van_calloc(NULL, 1, DEF_VAL_ULEN,
		    &tree->defretdata->data);
	}
	if (ret == 0)
		ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
		    &tree->reakey);
	if (ret == 0)
		ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM),
		    &tree->readata);

	child->gen = leaf->gen;
	child->pointer = (INMEM_BNODE *)leaf;
	
	internal->level = leaf->level + 1;
	internal->size = 1;
	internal->keys[0] = NULL;
	internal->childs[0] = child;

	tree->root = internal;
	tree->rootgen = tree->root->gen;
	tree->first = tree->last = leaf;
	tree->size = 0;
	tree->nlevel = internal->level;
	tree->maxlevel = IM_MAX_LEVEL;
	F_SET(tree, INMEM_TREE_ACTIVE);

	tree->get = _van_inmemtree_get;
	tree->put = _van_inmemtree_put;
	tree->del = _van_inmemtree_del;

	*treep = tree;

	if (0) {
err:
		if (child != NULL)
			_van_free(NULL, child);
		if (leaf != NULL)
			_van_inmemtree_free_node(tree,
			    (INMEM_BNODE *)leaf, 0, 0);
		if (internal != NULL)
			_van_inmemtree_free_node(tree,
			    (INMEM_BNODE *)internal, 0, 0);
		tree->root = NULL;
		tree->first = tree->last = NULL;
		if (tree != NULL)
			_van_inmemtree_destory(tree);
	}

	return (ret);
}

int _van_inmemtree_destory(INMEM_TREE *tree) {
	int ret, ret1;
	INMEM_BINTERNAL *root;
	INMEM_BNODE *node;
	size_t ccnt, now, tcnt, top;

	ret = 0;
	root = tree->root;
	tree->root = NULL;
	tree->first = tree->last = NULL;
	tcnt = tree->nodes_cnt;
	ccnt = 0;
	top = 50;
	now = 1;
	/* Free the nodes of the tree */
	while ((node = TAILQ_FIRST(&tree->nodelist)) != NULL) {
		_van_inmemtree_free_node(tree, node, 0, 0);
		ccnt++;	
		/*
		if (ccnt >= now * tcnt / top) {
			fprintf(stderr, "%lu/%lu nodes has been destroyed\n",
			    (unsigned long)now, (unsigned long)top);
			now++;
		}
		*/
	}

	/* Free the config */
	if (tree->config != NULL)
		ret1 = _van_inmemtree_config_destroy(tree->config);
	tree->config = NULL;
	SET_RET(ret1, ret);

	/* Free the memory */
	if (tree->readata != NULL)
		_van_datum_free(tree->readata);
	if (tree->reakey != NULL)
		_van_datum_free(tree->reakey);
	if (tree->defretdata != NULL)
		_van_datum_free(tree->defretdata);
	if (tree->defretkey != NULL)
		_van_datum_free(tree->defretkey);

	/* Free the INMEM_TREE structure itself */
	TAILQ_INIT(&tree->nodelist);
	if (tree->nlmtx_allocated) {
		tree->nlmtx_allocated = 0;
		INMEM_MUTEX_DESTROY(tree, mtx_nodelist, ret);
	}
	_van_free(NULL, tree);

	return (ret);
}

int _van_inmemtree_freeze(INMEM_TREE *tree) {
	int ret = 0;

	tree->put = _van_inmemtree_put_invalid;
	tree->del = _van_inmemtree_del_invalid;

	return (ret);
}

static int _van_inmemtree_put_invalid(INMEM_TREE *tree,
    VAN_DATUM *k, VAN_DATUM *v, flags_t flags) {

	return (VAN_READONLY);
}

static int _van_inmemtree_del_invalid(INMEM_TREE *tree,
    VAN_DATUM *k, flags_t flags) {

	return (VAN_READONLY);
}

static int _van_inmemtree_get(INMEM_TREE *tree,
    VAN_DATUM *k, VAN_DATUM *v, flags_t flags) {
	INMEM_TREECURSOR *cursor;
	int ret, ret1;

	cursor = NULL;

	ret = _van_inmemtree_cursor_init(tree, &cursor, VAN_TEMPCURSOR);
	if (ret != 0)
		goto err;
	ret = _van_inmemtree_cursor_get(cursor, k, v, VAN_SET);
err:
	if (cursor != NULL) {
		ret1 = _van_inmemtree_cursor_cleanup(cursor);
		SET_RET(ret1, ret);
	}
	return (ret);
}

static int _van_inmemtree_put(INMEM_TREE *tree,
    VAN_DATUM *k, VAN_DATUM *v, flags_t flags) {
	INMEM_TREECURSOR *cursor;
	int ret, ret1;

	cursor = NULL;

	ret = _van_inmemtree_cursor_init(tree, &cursor,
	    VAN_TEMPCURSOR | VAN_WRITECURSOR);
	if (ret != 0)
		goto err;
	ret = _van_inmemtree_cursor_put(cursor, k, v, 0);
err:
	if (cursor != NULL) {
		ret1 = _van_inmemtree_cursor_cleanup(cursor);
		SET_RET(ret1, ret);
	}
	return (ret);
}

static int _van_inmemtree_del(INMEM_TREE *tree,
    VAN_DATUM *k, flags_t flags) {
	INMEM_TREECURSOR *cursor;
	int ret, ret1;

	cursor = NULL;

	ret = _van_inmemtree_cursor_init(tree, &cursor,
	    VAN_TEMPCURSOR | VAN_WRITECURSOR);
	if (ret != 0)
		goto err;
	ret = _van_inmemtree_cursor_del(cursor, k, 0);
err:
	if (cursor != NULL) {
		ret1 = _van_inmemtree_cursor_cleanup(cursor);
		SET_RET(ret1, ret);
	}
	return (ret);
}

