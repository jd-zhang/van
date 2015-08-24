#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"

int _van_inmemtree_dump_leaf(INMEM_TREE *im_tree, 
    INMEM_BLEAF *leaf, FILE *f) {
	int ret;
	size_t i, n;
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
		    (keys[i] == NULL || keys[i]->v == NULL ||
		    keys[i]->v->data == NULL) ? "(null)" :
		    (char *)keys[i]->v->data, (values[i] == NULL ||
		    values[i]->v == NULL || values[i]->v->data == NULL) ?
		    "(null)" : (char *)values[i]->v->data);
	}

	return (ret);
}

int _van_inmemtree_dump_internal(INMEM_TREE *im_tree,
    INMEM_BINTERNAL *internal, int recursive, FILE *f) {
	int ret;
	size_t i, n;
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
		    (keys[i] == NULL || keys[i]->v == NULL ||
		    keys[i]->v->data == NULL) ? "(null)" :
		    (char *)keys[i]->v->data, (childs[i] == NULL ||
		    childs[i]->pointer == NULL) ?
		    0 : (unsigned long)childs[i]->pointer);
		if (recursive && (childs[i] != NULL) && childs[i]->pointer != NULL) {
			ret = _van_inmemtree_dump_node(im_tree, 
			    childs[i]->pointer, recursive, f);
		}
	}

	return (ret);

}

int _van_inmemtree_dump_node(INMEM_TREE *im_tree,
    INMEM_BNODE *node, int recursive, FILE *f) {
	int ret;
	INMEM_BINTERNAL *internal;
	INMEM_BLEAF *leaf;
	
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
		ret = _van_error_path("unknown node type!");
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

INMEM_RWLOCK_STAT inmem_rwlock_stats;

int _van_inmemtree_rwlock_stat_reset() {
	memset(&inmem_rwlock_stats, 0, sizeof(INMEM_RWLOCK_STAT));
	return (0);
}

INMEM_RWLOCK_STAT _van_inmemtree_rwlock_stat_ret() {
	return (inmem_rwlock_stats);
}

int _van_inmemtree_rwlock_stat_print(FILE *f, const char *prefix) {
	fprintf(f, "%s ninit:%lu, ndestroy:%lu, nrdlocks:%lu, nwrlocks:%lu, "
	    "ntryrdlocks:%lu,ntrywrlocks:%lu,nunlocks:%lu\n",
	    prefix, (unsigned long)inmem_rwlock_stats.ninit,
	    (unsigned long)inmem_rwlock_stats.ndestroy,
	    (unsigned long)inmem_rwlock_stats.nrdlocks,
	    (unsigned long)inmem_rwlock_stats.nwrlocks,
	    (unsigned long)inmem_rwlock_stats.ntryrdlocks,
	    (unsigned long)inmem_rwlock_stats.ntrywrlocks,
	    (unsigned long)inmem_rwlock_stats.nunlocks);

	return(0);
}

INMEM_MUTEX_STAT inmem_mutex_stats;

int _van_inmemtree_mutex_stat_reset() {
	memset(&inmem_mutex_stats, 0, sizeof(INMEM_MUTEX_STAT));
	return (0);
}

INMEM_MUTEX_STAT _van_inmemtree_mutex_stat_ret() {
	return (inmem_mutex_stats);
}

int _van_inmemtree_mutex_stat_print(FILE *f, const char *prefix) {
	fprintf(f, "%s ninit:%lu, ndestroy:%lu, nlocks:%lu, "
	    "ntrylocks:%lu, nunlocks:%lu\n",
	    prefix, (unsigned long)inmem_mutex_stats.ninit,
	    (unsigned long)inmem_mutex_stats.ndestroy,
	    (unsigned long)inmem_mutex_stats.nlocks,
	    (unsigned long)inmem_mutex_stats.ntrylocks,
	    (unsigned long)inmem_mutex_stats.nunlocks);

	return(0);
}
