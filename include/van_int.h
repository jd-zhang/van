#ifndef _VAN_INT_H
#define _VAN_INT_H

#include <unistd.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

/* IPC */
#include <mqueue.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>

/* network headers */
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>

/* Google headers */
/*
#include <gperftools/tcmalloc.h>
*/

/* Set, clear and test flags. */
#define LF_CLR(f)		(flags) &= ~(f)
#define LF_ISSET(f)		((flags) & (f))
#define LF_SET(f)		(flags) |= (f)
#define F_CLR(p, f)		(p)->flags &= ~(f)
#define F_ISSET(p, f)		((p)->flags & (f))
#define F_SET(p, f)		(p)->flags |= (f)
#define FLD_CLR(fld, f)		(fld) &= ~(f)
#define FLD_ISSET(fld, f)	((fld) & (f))
#define FLD_SET(fld, f)		(fld) |= (f)

#define SET_RET(tret, rret) do { \
	int _tret; \
	_tret = (tret); \
	if ((_tret) != 0 && (rret) == 0) \
		(rret) = (_tret); \
} while(0)

#define VAN_ASSERT(expr) do { \
	if(!(expr)) { \
		fprintf(stderr, "Assert Failure(%s:%d): "#expr,\
		    __FILE__, __LINE__); \
		abort(); \
	} \
}while(0)

#define VAN_VASSERT_VARGS(expr, ...) do { \
	if(!(expr)) { \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "Assert Failure(%s:%d): "#expr,\
		    __FILE__, __LINE__); \
		abort(); \
	} \
}while(0)

/* cursor index macros */
#define INVALID_CURSOR_INDX	(-1)
#define IS_CURSOR_INDX_VALID(cursor)	(cursor->indx != -1)

/* flags for opening a file */
#define VAN_OPEN_CREAT		0x01
#define VAN_OPEN_TRUNC		0x02
#define VAN_OPEN_DIRECT		0x04
#define VAN_OPEN_APPEND		0x08
#define VAN_OPEN_NONBLOCK	0x10
#define VAN_OPEN_RDONLY		0x20

#define DEF_DIR_MODE	(S_IRWXU | S_IRWXG )
#define DEF_FILE_MODE	(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)

#define CHK_RETURN(val) do {		\
	int ___ret;			\
	___ret = (val);			\
	if (___ret)			\
		return (___ret);	\
} while(0)

#define CHK_ERR(ret) do {		\
	if (ret != 0)			\
		goto err;		\
} while(0)

#define CHK_SYS_RETURN(val) do {		\
	int ___ret;				\
	___ret = (val);				\
	if (___ret)				\
		return _van_getsys_error();	\
} while(0)

#define SET_SYS_RETURN(val, ret) do {		\
	int ___ret;				\
	___ret = (val);				\
	if (___ret)				\
		ret = _van_getsys_error();	\
} while(0)


/* headers */
#include "queue.h"
#include "van_types.h"
#include "van_declare.h"
#include "van_mutex.h"
#include "van_datum.h"
#include "van_debug.h"
#include "van_inmem_tree.h"
#include "van_io.h"
#include "van_ondisk_tree.h"
#include "van_ondisk_cache.h"
#include "van_operation.h"
#include "van_store.h"
#include "van_table.h"

/* typedef TAILQ_HEAD(__hash_head) VAN_HASHTAB; */

#undef	FALSE
#define	FALSE		0
#undef	TRUE
#define	TRUE		(!FALSE)

#define KILOBYTE	1024
#define	MEGABYTE	1048576
#define	GIGABYTE	1073741824

#define	NS_PER_MS	1000000		/* Nanoseconds in a millisecond */
#define	NS_PER_US	1000		/* Nanoseconds in a microsecond */
#define	NS_PER_SEC	1000000000	/* Nanoseconds in a second */
#define	US_PER_MS	1000		/* Microseconds in a millisecond */
#define	US_PER_SEC	1000000		/* Microseconds in a second */
#define	MS_PER_SEC	1000		/* Milliseconds in a second */

#define	NOP_STATEMENT	do { } while (0)
/* Align an integer to a specific boundary. */
#undef	VAN_ALIGN
#define	VAN_ALIGN(v, bound)						\
	(((v) + (bound) - 1) & ~(((uintmax_t)(bound)) - 1))
/* Increment a pointer to a specific boundary. */
#undef	ALIGNP_INC
#define	ALIGNP_INC(p, bound)						\
	(void *)(((uintptr_t)(p) + (bound) - 1) & ~(((uintptr_t)(bound)) - 1))
/*
 * Print an address as a u_long (a u_long is the largest type we can print
 * portably).  Most 64-bit systems have made longs 64-bits, so this should
 * work.
 */
#define	P_TO_ULONG(p)	((unsigned long)(uintptr_t)(p))
/*
 * Convert a pointer to an integral value.
 *
 * The (uint16)(uintptr_t) cast avoids warnings: the (uintptr_t) cast
 * converts the value to an integral type, and the (uint16) cast converts
 * it to a small integral type so we don't get complaints when we assign the
 * final result to an integral type smaller than uintptr_t.
 */
#define	P_TO_UINT32(p)	((uint32)(uintptr_t)(p))
#define	P_TO_UINT16(p)	((uint16)(uintptr_t)(p))

#undef	SSZ
#define	SSZ(name, field)  P_TO_UINT16(&(((name *)0)->field))
#undef	SSZA
#define	SSZA(name, field) P_TO_UINT16(&(((name *)0)->field[0]))

#define	VAN_MAXPATHLEN	1024
#define	PATH_DOT	"."	/* Current working directory. */
				/* Path separator character(s). */
#define	PATH_SEPARATOR	"\\/:"

/* Final header */
#include "van.h"

/* Headers for function prototypes */
#include "common_ext.h"
#include "datum_ext.h"
#include "inmemtree_ext.h"
#include "ondisktree_ext.h"
#include "store_ext.h"
#include "table_ext.h"

#endif
