#ifndef _VAN_DEBUG_H
#define _VAN_DEBUG_H

extern ALLOC_STAT alloc_stats;
struct _alloc_stat {
	size_t nmalloc;
	size_t ncalloc;
	size_t nrealloc;
	size_t nfree;
};

#endif
