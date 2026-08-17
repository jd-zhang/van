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

#include "van_inmem_tree.h"


void _inmemtree_statprint(INMEM_TREE *imtree, FILE *f, const char *prefix) {
    if (!imtree || !f) {
        return;
    }
    
    size_t nodes = atomic_load(&imtree->stat.n_nodes);
    size_t leaves = atomic_load(&imtree->stat.n_leaves);
    size_t internals = atomic_load(&imtree->stat.n_internals);
    
    if (prefix && strlen(prefix) > 0) {
        fprintf(f, "%s nodes=%zu leaves=%zu internals=%zu\n", 
                prefix, nodes, leaves, internals);
    } else {
        fprintf(f, "nodes=%zu leaves=%zu internals=%zu\n", 
                nodes, leaves, internals);
    }
}