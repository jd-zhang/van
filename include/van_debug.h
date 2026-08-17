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
#ifndef _VAN_DEBUG_H
#define _VAN_DEBUG_H

#include <stdatomic.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct _alloc_stat {
    atomic_size_t nmalloc;
    atomic_size_t ncalloc;
    atomic_size_t nrealloc;
    atomic_size_t nfree;
};

extern ALLOC_STAT alloc_stats;

#define VAN_ALLOC_STAT_INC_MALLOC() \
    atomic_fetch_add(&alloc_stats.nmalloc, 1)

#define VAN_ALLOC_STAT_INC_CALLOC() \
    atomic_fetch_add(&alloc_stats.ncalloc, 1)

#define VAN_ALLOC_STAT_INC_REALLOC() \
    atomic_fetch_add(&alloc_stats.nrealloc, 1)

#define VAN_ALLOC_STAT_INC_FREE() \
    atomic_fetch_add(&alloc_stats.nfree, 1)

#define VAN_ALLOC_STAT_GET_MALLOC() \
    atomic_load(&alloc_stats.nmalloc)

#define VAN_ALLOC_STAT_GET_CALLOC() \
    atomic_load(&alloc_stats.ncalloc)

#define VAN_ALLOC_STAT_GET_REALLOC() \
    atomic_load(&alloc_stats.nrealloc)

#define VAN_ALLOC_STAT_GET_FREE() \
    atomic_load(&alloc_stats.nfree)

#ifdef __cplusplus
}
#endif

#endif