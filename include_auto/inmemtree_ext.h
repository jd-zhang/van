/* src/inmemtree/inmemtree.c */
extern int _van_inmemtree_create(VAN_TABLE *table,
    INMEM_TREE **treep);
extern int _van_inmemtree_destory(INMEM_TREE *tree);
extern int _van_inmemtree_freeze(INMEM_TREE *tree);

/* src/inmemtree/inmemtree_config.c */
extern int _van_inmemtree_config_alloc(INMEM_CONFIG **dstp);
extern int _van_inmemtree_config_destroy(INMEM_CONFIG *dst);
extern int _van_inmemtree_config_copy(INMEM_CONFIG *src,
    INMEM_CONFIG *dst);
extern int _van_inmemtree_config_alloccopy(INMEM_CONFIG *src,
    INMEM_CONFIG **dstp);
extern int _van_inmemtree_set_max_entries(INMEM_TREE *tree, size_t max);
extern int _van_inmemtree_set_split_point(INMEM_TREE *tree, size_t point);
extern int _van_inmemtree_set_key_compare(INMEM_TREE *tree,
    VAN_DATUM_CMPFUNC func);

/* src/inmemtree/inmemtree_cursor.c */
extern int _van_inmemtree_cursor_init(INMEM_TREE *im_tree,
    INMEM_TREECURSOR **cursorp, flags_t flags);
extern int _van_inmemtree_cursor_write_valid(
    INMEM_TREECURSOR *cursor);
extern int _van_inmemtree_cursor_dup(INMEM_TREECURSOR *src,
    INMEM_TREECURSOR *dst);
extern int _van_inmemtree_cursor_allocdup(INMEM_TREECURSOR *src,
    INMEM_TREECURSOR **dstp);
extern int _van_inmemtree_cursor_cleanup(INMEM_TREECURSOR *src);
extern int _van_inmemtree_cursor_cleanup_nounlock(INMEM_TREECURSOR *src);
extern int _van_inmemtree_cursor_locate(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, flags_t flags);
extern int _van_inmemtree_cursor_next(INMEM_TREECURSOR *cursor);
extern int _van_inmemtree_cursor_prev(INMEM_TREECURSOR *cursor);
extern int _van_inmemtree_cursor_first(INMEM_TREECURSOR *cursor);
extern int _van_inmemtree_cursor_last(INMEM_TREECURSOR *cursor);
extern int _van_inmemtree_cursor_insert(INMEM_TREECURSOR *cursor, 
    VAN_DATUM *key, VAN_DATUM *value);
extern int _van_inmemtree_cursor_update(INMEM_TREECURSOR *cursor,
    VAN_DATUM *value, flags_t flags);
extern int _van_inmemtree_cursor_putfordel(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, flags_t flags);
extern int _van_inmemtree_cursor_justdel(INMEM_TREECURSOR *cursor,\
    flags_t flags);
extern int _van_inmemtree_cursor_get(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, VAN_DATUM *data, flags_t flags);
extern int _van_inmemtree_cursor_put(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, VAN_DATUM *data, flags_t flags);
extern int _van_inmemtree_cursor_del(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, flags_t flags);
extern int _van_inmemtree_cursor_insert_internal(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, INMEM_BNODE *child);

/* src/inmemtree/inmemtree_compare.c */
extern int _van_inmemtree_key_docmp(INMEM_TREECURSOR *cursor,
    VAN_DATUM *key, INMEM_KEY *k2);
extern int _van_inmemtree_compare(INMEM_TREECURSOR *cursor, 
    VAN_DATUM *key);

/* src/inmemtree/inmemtree_debug.c */
extern int _van_inmemtree_dump_leaf(INMEM_TREE *im_tree, 
    INMEM_BLEAF *leaf, FILE *f);
extern int _van_inmemtree_dump_internal(INMEM_TREE *im_tree,
    INMEM_BINTERNAL *internal, int recursive, FILE *f);
extern int _van_inmemtree_dump_all_leaves(INMEM_TREE *im_tree, FILE *f);
extern int _van_inmemtree_dump_tree(INMEM_TREE *im_tree, FILE *f);
extern int _van_inmemtree_dump_node(INMEM_TREE *im_tree,
    INMEM_BNODE *node, int recursive, FILE *f);
extern int _van_inmemtree_rwlock_stat_reset();
extern INMEM_RWLOCK_STAT _van_inmemtree_rwlock_stat_ret();
extern int _van_inmemtree_rwlock_stat_print(FILE *f, const char *prefix);
extern int _van_inmemtree_mutex_stat_reset();
extern INMEM_MUTEX_STAT _van_inmemtree_mutex_stat_ret();
extern int _van_inmemtree_mutex_stat_print(FILE *f,
    const char *prefix);

/* src/inmemtree/inmemtree_init.c */
extern int _van_inmemtree_alloc_bnode(INMEM_TREE *im_tree, 
    INMEM_BNODE **nodep);
extern int _van_inmemtree_alloc_binternal(INMEM_TREE *im_tree, 
    INMEM_BINTERNAL **nodep);
int _van_inmemtree_alloc_bleaf(INMEM_TREE *im_tree, 
    INMEM_BLEAF **nodep);

/* src/inmemtree/inmemtree_node.c */
extern int _van_inmemtree_gen_key(VAN_DATUM *v, INMEM_KEY **keyp);
extern int _van_inmemtree_gen_key_nocopydatum(VAN_DATUM *v,
    INMEM_KEY **keyp);
extern int _van_inmemtree_free_key(INMEM_KEY *key);
extern int _van_inmemtree_gen_data(VAN_DATUM *v, INMEM_DATA **datap);
extern int _van_inmemtree_free_data(INMEM_DATA *data);
extern int _van_inmemtree_free_leaf(INMEM_TREE *tree, INMEM_BLEAF *leaf);
extern int _van_inmemtree_free_child(INMEM_TREE *tree, 
    INMEM_CHILD *child, int recursive, int lock);
extern int _van_inmemtree_free_internal(INMEM_TREE *tree,
    INMEM_BINTERNAL *internal, int recursive, int lock);
extern int _van_inmemtree_free_node(INMEM_TREE *tree, 
    INMEM_BNODE *node, int recursive, int lock);
extern int _van_inmemtree_copy_node(INMEM_TREE *tree, 
    INMEM_BNODE *src, INMEM_BNODE *dst);
extern int _van_inmem_initialize_newroot(INMEM_BINTERNAL *newroot, 
    INMEM_BNODE *left, INMEM_BNODE *right, VAN_DATUM *sep);
extern int _van_inmemtree_node_rwlock_action(INMEM_BNODE *node,
    int action);

/* src/inmemtree/inmemtree_ret.c */
extern int _van_inmemtree_ret(INMEM_TREECURSOR *cursor,
    const VAN_DATUM *datum, VAN_DATUM *rdatum,
    int retype, flags_t flags);

/* src/inmemtree/inmemtree_search.c */
extern int _van_inmemtree_search(INMEM_TREECURSOR *imc,
    VAN_DATUM *key, int dest_level, flags_t flags);
extern int _van_inmemtree_search_node(INMEM_TREECURSOR *imc,
    VAN_DATUM *key, int dest_level, flags_t flags);

/* src/inmemtree/inmemtree_split.c */
extern int _van_inmemtree_split_bleaf(INMEM_TREE *im_tree, INMEM_BLEAF *leaf, 
    VAN_DATUM **splitp, INMEM_BLEAF **thenewp);
extern int _van_inmemtree_split_binternal(INMEM_TREE *im_tree,
    INMEM_BINTERNAL *internal, VAN_DATUM **splitp,
    INMEM_BINTERNAL **thenewp);
extern int _van_inmemtree_split_rootinternal(INMEM_TREE *im_tree);
extern int _van_inmemtree_split_node(INMEM_TREE *im_tree,
    INMEM_BNODE *node);

/* src/inmemtree/inmemtree_stat.c */

/* src/inmemtree/inmemtree_traverse.c */
extern int _van_inmemtree_process_leaf_forward(INMEM_BLEAF *leaf,
    INMEM_TRAVERSE_FUNC func, void *firstarg);
extern int _van_inmemtree_process_leaf_backward(INMEM_BLEAF *leaf,
    INMEM_TRAVERSE_FUNC func, void *firstarg);
extern int _van_inmemtree_traverse_forard(INMEM_TREE *tree, 
    INMEM_TRAVERSE_FUNC func, void *firstarg);
extern int _van_inmemtree_traverse_backward(INMEM_TREE *tree,
    INMEM_TRAVERSE_FUNC func, void *firstarg);
