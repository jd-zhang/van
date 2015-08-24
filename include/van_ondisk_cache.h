#ifndef _VAN_ONDISK_CACHE_H
#define _VAN_ONDISK_CACHE_H

#define NCACHE_PER_HEADER 10
struct _ondisk_cache_block {
	TAILQ_ENTRY(_ondisk_cache_block)	links;
	pthread_mutex_t		mtx_load_cond;
	pthread_cond_t		load_cond;
	int			load;
	size_t		hash_indx;
	uint32		refcount;
	char		fileid[OD_FILEID_LEN];
	uint32		blkid;
	char		page[1];
};

struct _ondisk_cache_header {
	pthread_mutex_t	mtx_hash;
	TAILQ_HEAD(_odch, _ondisk_cache_block) head;
};


#endif
