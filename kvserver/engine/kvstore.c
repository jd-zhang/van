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
#include <stdlib.h>
#include <string.h>

/*
 * van_int.h is the engine's internal "everything" header: the type
 * definitions (VAN_DATUM and friends live in van_declare.h), every
 * structure header and every generated prototype header.  Including the
 * pieces one by one only works if van_declare.h comes first.
 */
#include "van_int.h"

#include "kvstore.h"

/*
 * The server is one store holding one table, so the handles stay here
 * instead of being handed out: the HTTP layer only ever wants "the"
 * database and has no use for a second one.
 */
static VAN_STORE *kv_store;
static VAN_TABLE *kv_table;

/*
 * Open the store and the table inside it.
 *
 * The order is the one sql_session_open() and sql_table_open() use: the
 * store must exist before a table can be created out of it, and the
 * background dump and merge threads must not run before the table is
 * open -- hence VAN_TABLE_SKIP_THREAD_START followed by an explicit
 * start_threads().
 */
int kv_open(const char *data_dir, const char *table_name,
    long long cache_size, long long inmem_threshold) {
	VAN_STORE *store;
	VAN_TABLE *table;
	int ret;

	if (data_dir == NULL || table_name == NULL)
		return (KV_INVALID);
	if (kv_store != NULL || kv_table != NULL)
		return (KV_BAD);

	store = NULL;
	table = NULL;

	ret = van_store_create(&store);
	if (ret != 0)
		return (KV_BAD);
	ret = store->open(store, data_dir, 0);
	if (ret != 0)
		return (KV_BAD);

	if (cache_size > 0) {
		ret = store->set_cachesize(store, (size_t)cache_size);
		if (ret != 0)
			return (KV_BAD);
	}

	ret = van_table_create(store, &table);
	if (ret != 0)
		return (KV_BAD);

	if (inmem_threshold > 0) {
		ret = table->set_inmem_threshold(table,
		    (size_t)inmem_threshold);
		if (ret != 0)
			return (KV_BAD);
	}

	ret = table->open(table, table_name, VAN_TABLE_SKIP_THREAD_START);
	if (ret != 0)
		return (KV_BAD);
	ret = table->start_threads(table);
	if (ret != 0)
		return (KV_BAD);

	kv_store = store;
	kv_table = table;

	return (KV_OK);
	/*
	 * Nothing is undone on the failure paths above: the engine has no
	 * destroy call for a half built store or table, and a caller that
	 * cannot open its data directory has nothing worth salvaging.
	 */
}

/*
 * Close the table first, then the store: a table cannot outlive the
 * store it was created from.
 */
int kv_close(void) {
	int ret, ret2;

	ret = KV_OK;

	if (kv_table != NULL) {
		ret = kv_table->close(kv_table, 0);
		kv_table = NULL;
	}
	if (kv_store != NULL) {
		ret2 = kv_store->close(kv_store);
		if (ret == KV_OK)
			ret = ret2;
		kv_store = NULL;
	}

	return (ret == 0 ? KV_OK : KV_BAD);
}

int kv_get(const char *key, long long key_len, char **value,
    long long *value_len) {
	VAN_DATUM k, v;
	int ret;

	if (kv_table == NULL || key == NULL || value == NULL ||
	    value_len == NULL)
		return (KV_INVALID);
	if (key_len < 0)
		return (KV_INVALID);

	*value = NULL;
	*value_len = 0;

	memset(&k, 0, sizeof(k));
	k.data = (void *)key;		/* get() only reads the key	*/
	k.size = (size_t)key_len;
	k.ulen = (size_t)key_len;
	k.flags = 0;

	memset(&v, 0, sizeof(v));
	v.data = NULL;
	v.size = 0;
	v.ulen = 0;
	v.flags = VAN_MALLOC;

	ret = kv_table->get(kv_table, &k, &v, 0);
	if (ret == VAN_NOTFOUND || ret == VAN_DELETED) {
		if (v.data != NULL)
			(void)_van_datum_copyfree(&v);
		return (KV_NOTFOUND);
	}
	if (ret != 0) {
		if (v.data != NULL)
			(void)_van_datum_copyfree(&v);
		return (KV_BAD);
	}

	/*
	 * A table with nothing on disk yet has no tree to search after the
	 * in-memory one, and the engine then reports success with an empty
	 * datum for a key it never saw: _van_table_op_get() overwrites its
	 * return value with the result of taking odt_mtx, which loses the
	 * VAN_NOTFOUND the in-memory search produced.
	 *
	 * The SQL layer hits the same thing and reads it as "no such row":
	 * sql_kv_get() hands back *datap == NULL and fetch_row() turns that
	 * into "not found".  Do the same, so a missing key is always
	 * KV_NOTFOUND instead of an empty value.
	 */
	if (v.data == NULL)
		return (KV_NOTFOUND);

	/*
	 * Hand back a private copy: the engine owns 'v' and is free to
	 * recycle it the moment get() returns.  Never malloc(0), a zero
	 * length value still has to come back as a real pointer.
	 */
	*value = (char *)malloc(v.size > 0 ? v.size : 1);
	if (*value == NULL) {
		(void)_van_datum_copyfree(&v);
		return (KV_BAD);
	}
	if (v.size > 0)
		memcpy(*value, v.data, v.size);
	*value_len = (long long)v.size;

	(void)_van_datum_copyfree(&v);

	return (KV_OK);
}

int kv_put(const char *key, long long key_len, const char *value,
    long long value_len) {
	VAN_DATUM k, v;
	int ret;

	if (kv_table == NULL || key == NULL)
		return (KV_INVALID);
	if (key_len < 0 || value_len < 0)
		return (KV_INVALID);

	memset(&k, 0, sizeof(k));
	k.data = (void *)key;
	k.size = (size_t)key_len;
	k.ulen = (size_t)key_len;
	k.flags = 0;

	memset(&v, 0, sizeof(v));
	v.data = (void *)value;
	v.size = (size_t)value_len;
	v.ulen = (size_t)value_len;
	v.flags = 0;

	ret = kv_table->put(kv_table, &k, &v, 0);

	return (ret == 0 ? KV_OK : KV_BAD);
}

int kv_del(const char *key, long long key_len) {
	VAN_DATUM k;
	int ret;

	if (kv_table == NULL || key == NULL)
		return (KV_INVALID);
	if (key_len < 0)
		return (KV_INVALID);

	memset(&k, 0, sizeof(k));
	k.data = (void *)key;
	k.size = (size_t)key_len;
	k.ulen = (size_t)key_len;
	k.flags = 0;

	ret = kv_table->del(kv_table, &k, 0);

	/* Deleting what is not there is not an error, DELETE is idempotent. */
	if (ret != 0 && ret != VAN_NOTFOUND && ret != VAN_DELETED)
		return (KV_BAD);

	return (KV_OK);
}

void kv_free_value(char *value) {

	if (value != NULL)
		free(value);
}
