
/* src/store/van_store.c */
extern int _van_store_create(VAN_STORE **storep);
extern int _van_store_open(VAN_STORE *store,
    const char *st_dir);
extern int _van_store_set_name(VAN_STORE *store,
    const char *st_name);
extern int _van_store_close(VAN_STORE *store);
extern int _van_store_destory(VAN_STORE *store);
