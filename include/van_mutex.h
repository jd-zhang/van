#ifndef _VAN_MUTEX_H
#define _VAN_MUTEX_H

struct _van_rwlock_stat {
	size_t ninit;
	size_t ndestroy;
	size_t nrdlocks;
	size_t nwrlocks;
	size_t nunlocks;
	size_t ntryrdlocks;
	size_t ntrywrlocks;
};

extern VAN_RWLOCK_STAT van_rwlock_stats;

#define VAN_RWLOCK_INIT(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_init(&((_node)->rwlock), NULL); \
	van_rwlock_stats.ninit++; \
} while(0)

#define VAN_RWLOCK_DESTROY(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_destroy(&((_node)->rwlock)); \
	van_rwlock_stats.ndestroy++; \
} while(0)

#define VAN_RWLOCK_RDLOCK(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_rdlock(&((_node)->rwlock)); \
	van_rwlock_stats.nrdlocks++; \
} while(0)

#define VAN_RWLOCK_TRYRDLOCK(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_tryrdlock(&((_node)->rwlock)); \
	van_rwlock_stats.ntryrdlocks++; \
} while(0)

#define VAN_RWLOCK_WRLOCK(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_wrlock(&((_node)->rwlock)); \
	van_rwlock_stats.nwrlocks++; \
} while(0)

#define VAN_RWLOCK_TRYWRLOCK(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_trywrlock(&((_node)->rwlock)); \
	van_rwlock_stats.ntrywrlocks++; \
} while(0)

#define VAN_RWLOCK_UNLOCK(_node, rwlock, _ret) do { \
	_ret = pthread_rwlock_unlock(&((_node)->rwlock)); \
	van_rwlock_stats.nunlocks++; \
} while(0)

struct _van_mutex_stat {
	size_t ninit;
	size_t ndestroy;
	size_t nlocks;
	size_t ntrylocks;
	size_t nunlocks;
};

extern VAN_MUTEX_STAT van_mutex_stats;

#define VAN_MUTEX_INIT(_node, mutex, _ret) do { \
	_ret = pthread_mutex_init(&((_node)->mutex), NULL); \
	van_mutex_stats.ninit++; \
} while(0)

#define VAN_MUTEX_DESTROY(_node, mutex, _ret) do { \
	_ret = pthread_mutex_destroy(&((_node)->mutex)); \
	van_mutex_stats.ndestroy++; \
} while(0)

#define VAN_MUTEX_LOCK(_node, mutex, _ret) do { \
	_ret = pthread_mutex_lock(&((_node)->mutex)); \
	van_mutex_stats.nlocks++; \
} while(0)

#define VAN_MUTEX_TRYLOCK(_node, mutex, _ret) do { \
	_ret = pthread_mutex_trylock(&((_node)->mutex)); \
	van_mutex_stats.ntrylocks++; \
} while(0)

#define VAN_MUTEX_UNLOCK(_node, mutex, _ret) do { \
	_ret = pthread_mutex_unlock(&((_node)->mutex)); \
	van_mutex_stats.nunlocks++; \
} while(0)

struct _van_cond_stat {
	size_t ninit;
	size_t ndestroy;
	size_t nwaits;
	size_t nsignals;
	size_t nbroadcasts;
};

extern VAN_COND_STAT van_cond_stats;

#define VAN_COND_INIT(_node, cond, _ret) do { \
	_ret = pthread_cond_init(&((_node)->cond), NULL); \
	van_cond_stats.ninit++; \
} while(0)

#define VAN_COND_DESTROY(_node, cond, _ret) do { \
	_ret = pthread_cond_destroy(&((_node)->cond)); \
	van_cond_stats.ndestroy++; \
} while(0)

#define VAN_COND_WAIT(_node, cond, mutex, _ret) do { \
	_ret = pthread_cond_wait(&((_node)->cond), (mutex)); \
	van_cond_stats.nwaits++; \
} while(0)

#define VAN_COND_SIGNAL(_node, cond, _ret) do { \
	_ret = pthread_cond_signal(&((_node)->cond)); \
	van_cond_stats.nsignals++; \
} while(0)

#define VAN_COND_BROADCAST(_node, cond, _ret) do { \
	_ret = pthread_cond_broadcast(&((_node)->cond)); \
	van_cond_stats.nbroadcasts++; \
} while(0)


#define RWLOCK_INIT 1
#define RWLOCK_DESTROY 2
#define RWLOCK_RDLOCK 3
#define RWLOCK_WRLOCK 4
#define RWLOCK_TRYRDLOCK 5
#define RWLOCK_TRYWRLOCK 6
#define RWLOCK_UNLOCK 7


#endif
