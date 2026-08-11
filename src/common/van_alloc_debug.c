#include "van_int.h"
#include "van.h"

ALLOC_STAT alloc_stats;

int _van_alloc_stat_reset() {
	memset(&alloc_stats, 0, sizeof(ALLOC_STAT));
	return (0);
}

ALLOC_STAT _van_alloc_stat_ret() {
	return (alloc_stats);
}

int _van_alloc_stat_print(FILE *f, const char *prefix) {
	fprintf(f, "%s nmalloc:%lu, ncalloc:%lu, "
	    "nrealloc:%lu, nfree:%lu\n",
	    prefix, (unsigned long)alloc_stats.nmalloc,
	    (unsigned long)alloc_stats.ncalloc,
	    (unsigned long)alloc_stats.nrealloc,
	    (unsigned long)alloc_stats.nfree);

	return(0);
}
