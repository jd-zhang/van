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
	CHK_ERR(ret);

	ret = _van_store_id_init(store);
	CHK_ERR(ret);

	store->open = _van_store_open;
	store->close = _van_store_close;
	store->set_name = _van_store_set_name;
	store->set_flags = _van_store_set_flags;
	store->get_flags = _van_store_get_flags;
	store->set_cache_headersize = _van_store_set_cache_headersize;
	store->get_cache_headersize = _van_store_get_cache_headersize;
	store->set_cachesize = _van_store_set_cachesize;
	store->get_cachesize = _van_store_get_cachesize;
	store->set_ondisk_blksize = _van_store_set_ondisk_blksize;
	store->get_ondisk_blksize = _van_store_get_ondisk_blksize;
	
	store->recover_dtab.int_size = 0;
	store->recover_dtab.ext_size = 0;

	/* Debug use only */
	/* ************** */
	store->printinfo = _van_store_printinfo;

	F_SET(store, VAN_STORE_INITED);

err:
	return (ret);	
}

int _van_store_open(VAN_STORE *store, const char *st_dir, flags_t openflags) {
	int ret;
	flags_t orig_oflags;

	VAN_ASSERT(store != NULL);
	orig_oflags = openflags;

	/* No other actions then just set the dir currently */
	ret = _van_strdup(NULL, st_dir, &store->store_dir);
	CHK_ERR(ret);
	
	if (store->cache_header_size == 0)
		store->cache_header_size = DEF_CACHE_HEADER_SIZE;
	if (store->ondisk_blksize == 0)
		store->ondisk_blksize = OD_DEF_BLKSIZE;
	/* This data should be used later */
	if (store->cache_size == 0)
		store->cache_size = (store->cache_header_size *
		    store->ondisk_blksize);

	ret = _van_ondisk_cache_init(store);
	CHK_ERR(ret);

	ret = _van_log_open(store);
	CHK_ERR(ret);

	if (ret == 0) {
		F_SET(store, VAN_STORE_OPENED);
		store->open_flags = orig_oflags;
	}

err:
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

int _van_store_set_flags(VAN_STORE *store, flags_t flags) {
	int ret;
	ret = 0;

	store->flags |= flags;
	return (ret);
}

int _van_store_get_flags(VAN_STORE *store, flags_t *flagsp) {
	int ret;
	ret = 0;

	*flagsp = store->flags;
	return (ret);
}

int _van_store_set_cache_headersize(VAN_STORE *store, size_t headersize) {
	int ret;
	ret = 0;

	store->cache_header_size = headersize;
	return (ret);
}

int _van_store_get_cache_headersize(VAN_STORE *store, size_t *headersizep) {
	int ret;
	ret = 0;

	*headersizep = store->cache_header_size;
	return (ret);
}

int _van_store_set_cachesize(VAN_STORE *store, size_t cachesize) {
	int ret;
	ret = 0;

	store->cache_size = cachesize;
	return (ret);
}

int _van_store_get_cachesize(VAN_STORE *store, size_t *cachesizep) {
	int ret;
	ret = 0;

	*cachesizep = store->cache_size;
	return (ret);
}

int _van_store_set_ondisk_blksize(VAN_STORE *store, size_t blksize) {
	int ret;
	ret = 0;

	store->ondisk_blksize = blksize;
	return (ret);
}

int _van_store_get_ondisk_blksize(VAN_STORE *store, size_t *blksizep) {
	int ret;
	ret = 0;

	*blksizep = store->ondisk_blksize;
	return (ret);
}

int _van_store_close(VAN_STORE *store) {
	int ret;

	/*
	 * TODO: What we need to do is to close all the
	 * tables in this tore.
	 */
	if (F_ISSET(store, VAN_STORE_OPENED)) {
		ret = _van_ondisk_cache_destroy(store);
		CHK_ERR(ret);
		ret = _van_log_close(store);
		CHK_ERR(ret);
	}
	F_CLR(store, VAN_STORE_INITED);
	F_CLR(store, VAN_STORE_OPENED);

	/* No special action beyond destory */
	ret =  _van_store_destory(store);
err:
	return(ret);
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
	CHK_ERR(ret);

	ret = _van_store_id_destroy(store);
	CHK_ERR(ret);

	_van_free(NULL, store);
err:
	return (ret);
}


/* The following are used for debug use only */
/* ***************************************** */
/* ***************************************** */
/* ***************************************** */
/* ***************************************** */
int _van_store_printinfo(VAN_STORE *store, FILE* outf, flags_t flags) {
	int ret;
	ret = 0;

	/* Store Information */
	fprintf(outf,
	    "============== Store Information: ================\n");
	fprintf(outf, "store_dir: %s\n",\
	    store->store_dir ? store->store_dir : "NULL");
	fprintf(outf, "store_name: %s\n",
	    store->store_name ? store->store_name : "NULL");
	fprintf(outf, "flags: 0x%x\n", store->flags);
	fprintf(outf, "open_flags: 0x%x\n",  store->open_flags);

	/* Table Information */
	fprintf(outf,
	    "============== Table Information: ================\n");

	/* Cache Information */
	fprintf(outf,
	    "============== Cache Information: ================\n");
	fprintf(outf, "cache_header_size: %lu\n",
	    (unsigned long)store->cache_header_size);
	fprintf(outf, "cache_size: %lu\n",
	    (unsigned long)store->cache_size);
	fprintf(outf, "ondisk_blksize: %lu\n",
	    (unsigned long)store->ondisk_blksize);
	/* TODO: cache blocks */

	return (ret);
}
