#include "van_int.h"
#include "van.h"

#include "van_inmem_tree.h"

int _van_store_create(VAN_STORE **storep) {
	int ret;
	VAN_STORE *store;

	VAN_ASSERT(storep != NULL);
	store = NULL;
	ret = _van_calloc(NULL, 1, sizeof(VAN_STORE), &store);
	if (ret != 0)
		return (ret);

	ret = _van_store_init(store);
	if (ret == 0)
		*storep = store;

	return (ret);
}

int _van_store_init(VAN_STORE *store) {
	int ret;

	/* 
	 * NOTE: We clear all data during init.
	 */
	memset(store, 0, sizeof(VAN_STORE));
	/* TODO: error handling */
	TAILQ_INIT(&store->tables);
	/* TODO: error handling */
	VAN_MUTEX_INIT(store, tb_mtx, ret);

	store->open = _van_store_open;
	store->set_name = _van_store_set_name;
	store->close = _van_store_close;

	F_SET(store, VAN_STORE_INITED);

	return (ret);	
}

int _van_store_open(VAN_STORE *store, const char *st_dir) {
	int ret;

	VAN_ASSERT(store != NULL);
	/* No other actions then just set the dir currently */
	ret = _van_strdup(NULL, st_dir, &store->store_dir);
	if (ret == 0) {
		F_SET(store, VAN_STORE_OPENED);
	}

	return (ret);
}

int _van_store_set_name(VAN_STORE *store, const char *st_name) {
	int ret;

	VAN_ASSERT(store != NULL);
	if (store->store_name != NULL) {
		_van_free(NULL, store->store_name);
		store->store_name = NULL;
	}
	ret = _van_strdup(NULL, st_name, &store->store_name);

	return (ret);
}

int _van_store_close(VAN_STORE *store) {

	/* 
	 * TODO: What we need to do is to close all the
	 * tables in this tore.
	 */
	F_CLR(store, VAN_STORE_INITED);
	F_CLR(store, VAN_STORE_OPENED);

	/* No special action beyond destory */
	return _van_store_destory(store);
}

int _van_store_destory(VAN_STORE *store) {
	int ret;

	ret = 0;
	if (store == NULL)
		return (ret);

	if (store->store_dir != NULL) {
		_van_free(NULL, store->store_dir);
		store->store_dir = NULL;
	}
	if (store->store_name != NULL) {
		_van_free(NULL, store->store_name);
		store->store_name = NULL;
	}
	/* TODO: handling error */
	VAN_MUTEX_DESTROY(store, tb_mtx, ret);
	_van_free(NULL, store);

	return (ret);
}

