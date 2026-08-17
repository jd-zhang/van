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
#include "van_int.h"
#include "van.h"

#include "van_debug.h"

ALLOC_STAT alloc_stats = {
    .nmalloc = ATOMIC_VAR_INIT(0),
    .ncalloc = ATOMIC_VAR_INIT(0),
    .nrealloc = ATOMIC_VAR_INIT(0),
    .nfree = ATOMIC_VAR_INIT(0)
};

int _van_alloc_stat_reset() {
    atomic_store(&alloc_stats.nmalloc, 0);
    atomic_store(&alloc_stats.ncalloc, 0);
    atomic_store(&alloc_stats.nrealloc, 0);
    atomic_store(&alloc_stats.nfree, 0);
    return (0);
}

ALLOC_STAT _van_alloc_stat_ret() {
    ALLOC_STAT tmp;
    tmp.nmalloc = atomic_load(&alloc_stats.nmalloc);
    tmp.ncalloc = atomic_load(&alloc_stats.ncalloc);
    tmp.nrealloc = atomic_load(&alloc_stats.nrealloc);
    tmp.nfree = atomic_load(&alloc_stats.nfree);
    return (tmp);
}

int _van_alloc_stat_print(FILE *f, const char *prefix) {
    fprintf(f, "%s nmalloc:%lu, ncalloc:%lu, "
        "nrealloc:%lu, nfree:%lu\n",
        prefix,
        (unsigned long)atomic_load(&alloc_stats.nmalloc),
        (unsigned long)atomic_load(&alloc_stats.ncalloc),
        (unsigned long)atomic_load(&alloc_stats.nrealloc),
        (unsigned long)atomic_load(&alloc_stats.nfree));

    return(0);
}