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

#include "van_mutex.h"

VAN_RWLOCK_STAT van_rwlock_stats = {
    .ninit = ATOMIC_VAR_INIT(0),
    .ndestroy = ATOMIC_VAR_INIT(0),
    .nrdlocks = ATOMIC_VAR_INIT(0),
    .nwrlocks = ATOMIC_VAR_INIT(0),
    .nunlocks = ATOMIC_VAR_INIT(0),
    .ntryrdlocks = ATOMIC_VAR_INIT(0),
    .ntrywrlocks = ATOMIC_VAR_INIT(0)
};

int _van_rwlock_stat_reset() {
    atomic_store(&van_rwlock_stats.ninit, 0);
    atomic_store(&van_rwlock_stats.ndestroy, 0);
    atomic_store(&van_rwlock_stats.nrdlocks, 0);
    atomic_store(&van_rwlock_stats.nwrlocks, 0);
    atomic_store(&van_rwlock_stats.nunlocks, 0);
    atomic_store(&van_rwlock_stats.ntryrdlocks, 0);
    atomic_store(&van_rwlock_stats.ntrywrlocks, 0);
    return (0);
}

VAN_RWLOCK_STAT _van_rwlock_stat_ret() {
    VAN_RWLOCK_STAT tmp;
    tmp.ninit = atomic_load(&van_rwlock_stats.ninit);
    tmp.ndestroy = atomic_load(&van_rwlock_stats.ndestroy);
    tmp.nrdlocks = atomic_load(&van_rwlock_stats.nrdlocks);
    tmp.nwrlocks = atomic_load(&van_rwlock_stats.nwrlocks);
    tmp.nunlocks = atomic_load(&van_rwlock_stats.nunlocks);
    tmp.ntryrdlocks = atomic_load(&van_rwlock_stats.ntryrdlocks);
    tmp.ntrywrlocks = atomic_load(&van_rwlock_stats.ntrywrlocks);
    return (tmp);
}

int _van_rwlock_stat_print(FILE *f, const char *prefix) {
    fprintf(f, "%s ninit:%lu, ndestroy:%lu, nrdlocks:%lu, nwrlocks:%lu, "
        "ntryrdlocks:%lu,ntrywrlocks:%lu,nunlocks:%lu\n",
        prefix,
        (unsigned long)atomic_load(&van_rwlock_stats.ninit),
        (unsigned long)atomic_load(&van_rwlock_stats.ndestroy),
        (unsigned long)atomic_load(&van_rwlock_stats.nrdlocks),
        (unsigned long)atomic_load(&van_rwlock_stats.nwrlocks),
        (unsigned long)atomic_load(&van_rwlock_stats.ntryrdlocks),
        (unsigned long)atomic_load(&van_rwlock_stats.ntrywrlocks),
        (unsigned long)atomic_load(&van_rwlock_stats.nunlocks));

    return(0);
}

VAN_MUTEX_STAT van_mutex_stats = {
    .ninit = ATOMIC_VAR_INIT(0),
    .ndestroy = ATOMIC_VAR_INIT(0),
    .nlocks = ATOMIC_VAR_INIT(0),
    .ntrylocks = ATOMIC_VAR_INIT(0),
    .nunlocks = ATOMIC_VAR_INIT(0)
};

int _van_mutex_stat_reset() {
    atomic_store(&van_mutex_stats.ninit, 0);
    atomic_store(&van_mutex_stats.ndestroy, 0);
    atomic_store(&van_mutex_stats.nlocks, 0);
    atomic_store(&van_mutex_stats.ntrylocks, 0);
    atomic_store(&van_mutex_stats.nunlocks, 0);
    return (0);
}

VAN_MUTEX_STAT _van_mutex_stat_ret() {
    VAN_MUTEX_STAT tmp;
    tmp.ninit = atomic_load(&van_mutex_stats.ninit);
    tmp.ndestroy = atomic_load(&van_mutex_stats.ndestroy);
    tmp.nlocks = atomic_load(&van_mutex_stats.nlocks);
    tmp.ntrylocks = atomic_load(&van_mutex_stats.ntrylocks);
    tmp.nunlocks = atomic_load(&van_mutex_stats.nunlocks);
    return (tmp);
}

int _van_mutex_stat_print(FILE *f, const char *prefix) {
    fprintf(f, "%s ninit:%lu, ndestroy:%lu, nlocks:%lu, "
        "ntrylocks:%lu, nunlocks:%lu\n",
        prefix,
        (unsigned long)atomic_load(&van_mutex_stats.ninit),
        (unsigned long)atomic_load(&van_mutex_stats.ndestroy),
        (unsigned long)atomic_load(&van_mutex_stats.nlocks),
        (unsigned long)atomic_load(&van_mutex_stats.ntrylocks),
        (unsigned long)atomic_load(&van_mutex_stats.nunlocks));

    return(0);
}

VAN_COND_STAT van_cond_stats = {
    .ninit = ATOMIC_VAR_INIT(0),
    .ndestroy = ATOMIC_VAR_INIT(0),
    .nwaits = ATOMIC_VAR_INIT(0),
    .nsignals = ATOMIC_VAR_INIT(0),
    .nbroadcasts = ATOMIC_VAR_INIT(0)
};

int _van_cond_stat_reset() {
    atomic_store(&van_cond_stats.ninit, 0);
    atomic_store(&van_cond_stats.ndestroy, 0);
    atomic_store(&van_cond_stats.nwaits, 0);
    atomic_store(&van_cond_stats.nsignals, 0);
    atomic_store(&van_cond_stats.nbroadcasts, 0);
    return (0);
}

VAN_COND_STAT _van_cond_stat_ret() {
    VAN_COND_STAT tmp;
    tmp.ninit = atomic_load(&van_cond_stats.ninit);
    tmp.ndestroy = atomic_load(&van_cond_stats.ndestroy);
    tmp.nwaits = atomic_load(&van_cond_stats.nwaits);
    tmp.nsignals = atomic_load(&van_cond_stats.nsignals);
    tmp.nbroadcasts = atomic_load(&van_cond_stats.nbroadcasts);
    return (tmp);
}

int _van_cond_stat_print(FILE *f, const char *prefix) {
    fprintf(f, "%s ninit:%lu, ndestroy:%lu, nwaits:%lu, "
        "nsignals:%lu, nbroadcasts:%lu\n",
        prefix,
        (unsigned long)atomic_load(&van_cond_stats.ninit),
        (unsigned long)atomic_load(&van_cond_stats.ndestroy),
        (unsigned long)atomic_load(&van_cond_stats.nwaits),
        (unsigned long)atomic_load(&van_cond_stats.nsignals),
        (unsigned long)atomic_load(&van_cond_stats.nbroadcasts));

    return(0);
}