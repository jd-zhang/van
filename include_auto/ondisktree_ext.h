/* src/ondisktree/ondisktree.c */
extern int _van_ondisktree_create(VAN_TABLE *table,
    ONDISK_TREE **treep);
extern int _van_ondisktree_init(ONDISK_TREE *tree);
extern int _van_ondisktree_open(ONDISK_TREE *tree, flags_t flags);
extern int _van_ondisktree_close(ONDISK_TREE *tree);
extern int _van_ondisktree_destory(ONDISK_TREE *tree);
extern int _van_ondisktree_get(ONDISK_TREE *tree,
    VAN_DATUM *k, VAN_DATUM *v, flags_t flags);

/* src/ondisktree/ondisktree_build.c */
extern int _van_ondisktree_store(ONDISK_TREE *od_tree,
    INMEM_TREE *im_tree);
extern int _van_ondisktree_addpair(void *obj, const INMEM_KEY *key,
    const INMEM_DATA *value);
extern int _van_ondisktree_addto_internal(ONDISK_TREE *tree, 
    ONDISK_ITEM *upitem, uint32 blkid, uint32 level);
extern int _van_ondisktree_add_ipage(ONDISK_TREE *tree, ONDISK_HEADER *page,
    ONDISK_ITEM *upitem, uint32 blkid, int isfirst);
extern int _van_ondisktree_add_lpage(ONDISK_HEADER *page, 
    const ONDISK_ITEM *key, const ONDISK_ITEM *data);
extern int _van_ondisktree_init_header(ONDISK_META *meta,
    ONDISK_HEADER *header, uint32 level, uint32 type);

/* src/ondisktree/ondisktree_cache.c */
extern int _van_ondisk_cache_init(VAN_TABLE *table);
extern int _van_ondisk_cache_destroy(VAN_TABLE *table);
extern int _van_ondisk_cache_get(ONDISK_TREE *tree,
    uint32 blkid, void *cachep);
extern int _van_ondisk_cache_put(ONDISK_TREE *tree, void *cache);
extern int _van_ondisk_cache_block_destory(ONDISK_CACHE_BLOCK *block);

/* src/ondisktree/ondisktree_compare.c */
extern int _van_ondisktree_key_docmp(ONDISK_TREECURSOR *cursor,
    VAN_DATUM *key, ONDISK_ITEM *k2, int *cmpp);
extern int _van_ondisktree_compare(ONDISK_TREECURSOR *cursor,
    VAN_DATUM *key, int *cmpp);

/* src/ondisktree/ondisktree_cursor.c */
extern int _van_ondisktree_cursor_init(ONDISK_TREE *tree,
    ONDISK_TREECURSOR **cursorp, flags_t flags);
extern int _van_ondisktree_cursor_dup(ONDISK_TREECURSOR *src,
    ONDISK_TREECURSOR *dst);
extern int _van_ondisktree_cursor_allocdup(ONDISK_TREECURSOR *src,
    ONDISK_TREECURSOR **dstp);
extern int _van_ondisktree_cursor_cleanup(ONDISK_TREECURSOR *src);
extern int _van_ondisktree_cursor_cleanup_noput(ONDISK_TREECURSOR *src);
extern int _van_ondisktree_cursor_locate(ONDISK_TREECURSOR *cursor, 
    VAN_DATUM *key, flags_t flags);
extern int _van_ondisktree_cursor_next(ONDISK_TREECURSOR *cursor);
extern int _van_ondisktree_cursor_prev(ONDISK_TREECURSOR *cursor);
extern int _van_ondisktree_cursor_first(ONDISK_TREECURSOR *cursor);
extern int _van_ondisktree_cursor_last(ONDISK_TREECURSOR *cursor);
extern int _van_ondisktree_cursor_get(ONDISK_TREECURSOR *cursor,
    VAN_DATUM *key, VAN_DATUM *data, flags_t flags);

/* src/ondisktree/ondisktree_debug.c */
extern int _van_ondisktree_dump(ONDISK_TREE *tree, FILE *f);
extern int _van_ondisktree_dump_meta(ONDISK_META *meta, FILE *f);
extern int _van_ondisktree_dump_header(ONDISK_HEADER *hdr, FILE *f);
extern const char *_van_ondisktree_blk_typestr(int type);

/* src/ondisktree/ondisktree_item.c */
extern int _van_ondiskitem_from_inmemkey(ONDISK_TREE *tree, 
    const INMEM_KEY *key, ONDISK_ITEM **itemp);
extern int _van_ondiskitem_from_inmemdata(ONDISK_TREE *tree,
    const INMEM_DATA *data, ONDISK_ITEM **itemp);
extern int _van_ondiskitem_alloccopy(const ONDISK_ITEM *srcitem,
    ONDISK_ITEM **destitemp);

/* src/ondisktree/ondisktree_ret.c */
extern int _van_ondisktree_ret(ONDISK_TREECURSOR *cursor, 
    const ONDISK_ITEM *k2, VAN_DATUM *rdatum,
    int retype, flags_t flags);

/* src/ondisktree/ondisktree_search.c */
extern int _van_ondisktree_search(ONDISK_TREECURSOR *cursor,
    VAN_DATUM *key, flags_t flags);
