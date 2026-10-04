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
#ifndef _KVSTORE_H
#define _KVSTORE_H

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Return codes.
 *
 * These are the engine's own numbers (see include/van.h), repeated here
 * as plain constants so that the SWIG interface file can %include this
 * header without dragging the whole engine into the generated wrapper.
 */
#define	KV_OK		0
#define	KV_NOTFOUND	(-30988)	/* VAN_NOTFOUND		*/
#define	KV_INVALID	(-30987)	/* VAN_INVALID		*/
#define	KV_BAD		(-30984)	/* VAN_BAD		*/

/*
 * One store, one table: there is no handle to pass around, the pair is
 * opened by kv_open() and kept inside the wrapper until kv_close().
 *
 * Keys and values are (pointer, length) pairs so that arbitrary binary
 * content, NUL bytes included, round trips unchanged.
 *
 * kv_get() returns a malloc'd copy of the value in '*value' with its
 * size in '*value_len'; release it with kv_free_value().  A missing key
 * is KV_NOTFOUND and leaves '*value' NULL.
 *
 * An empty value cannot be told apart from a missing one: the engine
 * reports an empty value as a NULL datum, which is exactly what it
 * returns for a key it has never seen.  kv_put() accepts a zero length
 * value, but the following kv_get() reports KV_NOTFOUND.  The HTTP layer
 * therefore answers 404 for both.
 */
int kv_open(const char *data_dir, const char *table_name,
    long long cache_size, long long inmem_threshold);
int kv_close(void);
int kv_get(const char *key, long long key_len,
    char **value, long long *value_len);
int kv_put(const char *key, long long key_len,
    const char *value, long long value_len);
int kv_del(const char *key, long long key_len);
void kv_free_value(char *value);

#if defined(__cplusplus)
}
#endif

#endif
