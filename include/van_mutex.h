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
#ifndef _VAN_MUTEX_H
#define _VAN_MUTEX_H

#include <stdatomic.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct _van_rwlock_stat {
    atomic_size_t ninit;
    atomic_size_t ndestroy;
    atomic_size_t nrdlocks;
    atomic_size_t nwrlocks;
    atomic_size_t nunlocks;
    atomic_size_t ntryrdlocks;
    atomic_size_t ntrywrlocks;
};

extern VAN_RWLOCK_STAT van_rwlock_stats;

#define VAN_RWLOCK_INIT(_node, rwlock, _ret) do { \
    _ret = pthread_rwlock_init(&((_node)->rwlock), NULL); \
    atomic_fetch_add(&van_rwlock_stats.ninit, 1); \
} while(0)

#define VAN_RWLOCK_DESTROY(_node, rwlock, _ret) do { \
    _ret = pthread_rwlock_destroy(&((_node)->rwlock)); \
    atomic_fetch_add(&van_rwlock_stats.ndestroy, 1); \
} while(0)

#define VAN_RWLOCK_RDLOCK(_node, rwlock, _ret) do { \
    _ret = pthread_rwlock_rdlock(&((_node)->rwlock)); \
    atomic_fetch_add(&van_rwlock_stats.nrdlocks, 1); \
} while(0)

#define VAN_RWLOCK_TRYRDLOCK(_node, rwlock, _ret) do { \
    _ret = pthread_rwlock_tryrdlock(&((_node)->rwlock)); \
    atomic_fetch_add(&van_rwlock_stats.ntryrdlocks, 1); \
} while(0)

#define VAN_RWLOCK_WRLOCK(_node, rwlock, _ret) do { \
    _ret = pthread_rwlock_wrlock(&((_node)->rwlock)); \
    atomic_fetch_add(&van_rwlock_stats.nwrlocks, 1); \
} while(0)

#define VAN_RWLOCK_TRYWRLOCK(_node, rwlock, _ret) do { \
    _ret = pthread_rwlock_trywrlock(&((_node)->rwlock)); \
    atomic_fetch_add(&van_rwlock_stats.ntrywrlocks, 1); \
} while(0)

#define VAN_RWLOCK_UNLOCK(_node, rwlock, _ret) do { \
    _ret = pthread_rwlock_unlock(&((_node)->rwlock)); \
    atomic_fetch_add(&van_rwlock_stats.nunlocks, 1); \
} while(0)

struct _van_mutex_stat {
    atomic_size_t ninit;
    atomic_size_t ndestroy;
    atomic_size_t nlocks;
    atomic_size_t ntrylocks;
    atomic_size_t nunlocks;
};

extern VAN_MUTEX_STAT van_mutex_stats;

#define VAN_MUTEX_INIT(_node, mutex, _ret) do { \
    _ret = pthread_mutex_init(&((_node)->mutex), NULL); \
    atomic_fetch_add(&van_mutex_stats.ninit, 1); \
} while(0)

#define VAN_MUTEX_DESTROY(_node, mutex, _ret) do { \
    _ret = pthread_mutex_destroy(&((_node)->mutex)); \
    atomic_fetch_add(&van_mutex_stats.ndestroy, 1); \
} while(0)

#define VAN_MUTEX_LOCK(_node, mutex, _ret) do { \
    _ret = pthread_mutex_lock(&((_node)->mutex)); \
    atomic_fetch_add(&van_mutex_stats.nlocks, 1); \
} while(0)

#define VAN_MUTEX_TRYLOCK(_node, mutex, _ret) do { \
    _ret = pthread_mutex_trylock(&((_node)->mutex)); \
    atomic_fetch_add(&van_mutex_stats.ntrylocks, 1); \
} while(0)

#define VAN_MUTEX_UNLOCK(_node, mutex, _ret) do { \
    _ret = pthread_mutex_unlock(&((_node)->mutex)); \
    atomic_fetch_add(&van_mutex_stats.nunlocks, 1); \
} while(0)

struct _van_cond_stat {
    atomic_size_t ninit;
    atomic_size_t ndestroy;
    atomic_size_t nwaits;
    atomic_size_t nsignals;
    atomic_size_t nbroadcasts;
};

extern VAN_COND_STAT van_cond_stats;

#define VAN_COND_INIT(_node, cond, _ret) do { \
    _ret = pthread_cond_init(&((_node)->cond), NULL); \
    atomic_fetch_add(&van_cond_stats.ninit, 1); \
} while(0)

#define VAN_COND_DESTROY(_node, cond, _ret) do { \
    _ret = pthread_cond_destroy(&((_node)->cond)); \
    atomic_fetch_add(&van_cond_stats.ndestroy, 1); \
} while(0)

#define VAN_COND_WAIT(_node, cond, mutex, _ret) do { \
    _ret = pthread_cond_wait(&((_node)->cond), (mutex)); \
    atomic_fetch_add(&van_cond_stats.nwaits, 1); \
} while(0)

#define VAN_COND_SIGNAL(_node, cond, _ret) do { \
    _ret = pthread_cond_signal(&((_node)->cond)); \
    atomic_fetch_add(&van_cond_stats.nsignals, 1); \
} while(0)

#define VAN_COND_BROADCAST(_node, cond, _ret) do { \
    _ret = pthread_cond_broadcast(&((_node)->cond)); \
    atomic_fetch_add(&van_cond_stats.nbroadcasts, 1); \
} while(0)


#define RWLOCK_INIT 1
#define RWLOCK_DESTROY 2
#define RWLOCK_RDLOCK 3
#define RWLOCK_WRLOCK 4
#define RWLOCK_TRYRDLOCK 5
#define RWLOCK_TRYWRLOCK 6
#define RWLOCK_UNLOCK 7

#if defined(__cplusplus)
}
#endif

#endif