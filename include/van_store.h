#ifndef _VAN_STORE_H
#define _VAN_STORE_H

struct _van_store {
	/* size_t cache_t_size; */
	char *store_dir;
	char *store_name; /* No real usage currently. */

	pthread_mutex_t tb_mtx;
	TAILQ_HEAD(_tables, _van_table) tables;

#define VAN_STORE_INITED	0x01
#define	VAN_STORE_OPENED	0x02
	flags_t		flags;

	/* Functions handles */
	int (*open)(VAN_STORE *store, const char *dir);
	int (*set_name)(VAN_STORE *store, const char *name);
	int (*close)(VAN_STORE *store);
};

#endif
