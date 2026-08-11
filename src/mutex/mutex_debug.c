#include "van_int.h"
#include "van.h"

#include "van_mutex.h"

VAN_RWLOCK_STAT van_rwlock_stats;
int _van_rwlock_stat_reset() {
	memset(&van_rwlock_stats, 0, sizeof(VAN_RWLOCK_STAT));
	return (0);
}

VAN_RWLOCK_STAT _van_rwlock_stat_ret() {
	return (van_rwlock_stats);
}

int _van_rwlock_stat_print(FILE *f, const char *prefix) {
	fprintf(f, "%s ninit:%lu, ndestroy:%lu, nrdlocks:%lu, nwrlocks:%lu, "
	    "ntryrdlocks:%lu,ntrywrlocks:%lu,nunlocks:%lu\n",
	    prefix, (unsigned long)van_rwlock_stats.ninit,
	    (unsigned long)van_rwlock_stats.ndestroy,
	    (unsigned long)van_rwlock_stats.nrdlocks,
	    (unsigned long)van_rwlock_stats.nwrlocks,
	    (unsigned long)van_rwlock_stats.ntryrdlocks,
	    (unsigned long)van_rwlock_stats.ntrywrlocks,
	    (unsigned long)van_rwlock_stats.nunlocks);

	return(0);
}

VAN_MUTEX_STAT van_mutex_stats;

int _van_mutex_stat_reset() {
	memset(&van_mutex_stats, 0, sizeof(VAN_MUTEX_STAT));
	return (0);
}

VAN_MUTEX_STAT _van_mutex_stat_ret() {
	return (van_mutex_stats);
}

int _van_mutex_stat_print(FILE *f, const char *prefix) {
	fprintf(f, "%s ninit:%lu, ndestroy:%lu, nlocks:%lu, "
	    "ntrylocks:%lu, nunlocks:%lu\n",
	    prefix, (unsigned long)van_mutex_stats.ninit,
	    (unsigned long)van_mutex_stats.ndestroy,
	    (unsigned long)van_mutex_stats.nlocks,
	    (unsigned long)van_mutex_stats.ntrylocks,
	    (unsigned long)van_mutex_stats.nunlocks);

	return(0);
}

VAN_COND_STAT van_cond_stats;

int _van_cond_stat_reset() {
	memset(&van_cond_stats, 0, sizeof(VAN_COND_STAT));
	return (0);
}

VAN_COND_STAT _van_cond_stat_ret() {
	return (van_cond_stats);
}

int _van_cond_stat_print(FILE *f, const char *prefix) {
	fprintf(f, "%s ninit:%lu, ndestroy:%lu, nwaits:%lu, "
	    "nsignals:%lu, nbroadcasts:%lu\n",
	    prefix, (unsigned long)van_cond_stats.ninit,
	    (unsigned long)van_cond_stats.ndestroy,
	    (unsigned long)van_cond_stats.nwaits,
	    (unsigned long)van_cond_stats.nsignals,
	    (unsigned long)van_cond_stats.nbroadcasts);

	return(0);
}
