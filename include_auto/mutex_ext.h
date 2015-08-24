/* src/mutex/van_mutex.c */

/* src/mutex/van_mutex_debug.c */
extern int _van_rwlock_stat_reset();
extern VAN_RWLOCK_STAT _van_rwlock_stat_ret();
extern int _van_rwlock_stat_print(FILE *f, const char *prefix);
extern int _van_mutex_stat_reset();
extern VAN_MUTEX_STAT _van_mutex_stat_ret();
extern int _van_mutex_stat_print(FILE *f, const char *prefix);
extern int _van_cond_stat_reset();
extern VAN_COND_STAT _van_cond_stat_ret();
extern int _van_cond_stat_print(FILE *f, const char *prefix);

