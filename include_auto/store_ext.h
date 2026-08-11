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
#ifndef _STORE_EXT_H
#define _STORE_EXT_H

/* src/store/van_store.c */
extern int _van_store_create(VAN_STORE **storep);
extern int _van_store_open(VAN_STORE *store,
    const char *st_dir, flags_t openflags);
extern int _van_store_set_name(VAN_STORE *store,
    const char *st_name);
extern int _van_store_set_flags(VAN_STORE *store, flags_t flags);
extern int _van_store_get_flags(VAN_STORE *store, flags_t *flagsp);
extern int _van_store_set_cache_headersize(VAN_STORE *store,
    size_t headersize);
extern int _van_store_get_cache_headersize(VAN_STORE *store,
    size_t *headersizep);
extern int _van_store_set_cachesize(VAN_STORE *store, size_t cachesize);
extern int _van_store_get_cachesize(VAN_STORE *store, size_t *cachesizep);
extern int _van_store_set_ondisk_blksize(VAN_STORE *store, size_t blksize);
extern int _van_store_get_ondisk_blksize(VAN_STORE *store, size_t *blksizep);
extern int _van_store_close(VAN_STORE *store);
extern int _van_store_destory(VAN_STORE *store);
extern int _van_store_printinfo(VAN_STORE *store, FILE* outf, flags_t flags);

/* src/store/van_store_path.c */
extern int _van_store_path(VAN_STORE *store, const char *file, char **tp);
extern int _van_store_path_noalloc(VAN_STORE *store,
    char *buf, size_t buflen, const char *file);

/* src/store/van_store_id.c */
extern int _van_store_id_init(VAN_STORE *store);
extern int _van_store_id_destroy(VAN_STORE *store);
extern int _van_store_id_bind(VAN_STORE *store, VAN_TABLE *table);
extern int _van_store_id_unbind(VAN_STORE *store, VAN_TABLE *table);

/* src/store/store_rec.c */
extern int _store_tabopen_recover(VAN_STORE *, VAN_DATUM *,
    VAN_LSN *, van_recops, void *);
extern int _store_tabids_recover(VAN_STORE *, VAN_DATUM *,
    VAN_LSN *, van_recops, void *);

#endif
