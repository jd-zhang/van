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

#ifndef _VAN_H
#define _VAN_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "van_types.h"
#include "van_datum.h"

typedef struct _van_lsn		VAN_LSN;
typedef struct _van_distab	VAN_DISTAB;

struct _van_lsn {
	uint32	file;
	uint32	offset;
};

typedef enum {
	VAN_TXN_ABORT=0,			/* Public. */
	VAN_TXN_APPLY=1,			/* Public. */
	VAN_TXN_BACKWARD_ROLL=3,		/* Public. */
	VAN_TXN_FORWARD_ROLL=4,		/* Public. */
	VAN_TXN_OPENFILES=5,		/* Internal. */
	VAN_TXN_POPENFILES=6,		/* Internal. */
	VAN_TXN_PRINT=7,			/* Public. */
	VAN_TXN_LOG_VERIFY=8		/* Internal. */
} van_recops;

struct _van_distab {
	int   (**int_dispatch)(VAN_STORE *, VAN_DATUM *,
	    VAN_LSN *, van_recops, void *);
	size_t	int_size;
	int   (**ext_dispatch)(VAN_STORE *, VAN_DATUM *,
	    VAN_LSN *, van_recops);
	size_t	ext_size;
};

#define LOG_DATUM_SIZE(dat)		\
	(sizeof(uint32)	+ ((dat) == NULL ? 0 : (dat)->size))

/* Allocation flags */
#define VAN_MALLOC		1
#define VAN_REALLOC		2
#define VAN_USERMEM		3
#define VAN_MEM_MASK		0x0F

/* Cursor allocation flags */
#define	VAN_TEMPCURSOR		0x01
#define VAN_WRITECURSOR		0x02

/* Cursor get flags */
#define VAN_SET			1
#define VAN_SET_RANGE		2
#define VAN_NEXT		3
#define VAN_PREV		4
#define VAN_FIRST		5
#define VAN_LAST		6
#define VAN_CURRENT		7
#define VAN_MASKMODE		0x0F  /* (~(~0 << 4)) seems more common here */
#define VAN_CURSOR_WRITE	0x10
#define VAN_CURSOR_READ		0x20
#define VAN_SKIP_DELETE		0x40

/* Cursor put flags */
#define VAN_NOOVERWRITE		0x1

/* flags for opening an ondisk tree. */
#define VAN_OPEN_CREAT		0x01
#define VAN_OPEN_TRUNC		0x02
#define VAN_OPEN_DIRECT		0x04
#define VAN_OPEN_APPEND		0x08
#define VAN_OPEN_NONBLOCK	0x10
#define VAN_OPEN_RDONLY		0x20
#define VAN_OPEN_EXCL		0x40

/* flags for closing an ondisk tree. */
#define VAN_CLOSE_REMOVE	0x01

/* Error return code */
#define VAN_NOTFOUND		(-30988)
#define VAN_INVALID		(-30987)
#define VAN_BUFFER_SMALL	(-30986)
#define VAN_STALE		(-30985)	/* Condition has changed */
#define VAN_BAD			(-30984)
#define VAN_DELETED		(-30983)
#define VAN_READONLY		(-30982)
#define VAN_OVERFLOW		(-30981)
#define	VAN_FROZEN		(-30980)
#define VAN_INMEM_FULL		(-30979)

/*
 * Public API aliases.
 *
 * The underscore-prefixed functions are the internal names, they are
 * exposed to the outside without the underscore via these macros so
 * that the public API reads cleanly.
 */
#define van_store_create	_van_store_create
#define van_table_create	_van_table_create
#define van_inmemtree_create	_van_inmemtree_create
#define van_ondisktree_create	_van_ondisktree_create

#define van_alloc_stat_print	_van_alloc_stat_print
#define van_alloc_stat_reset	_van_alloc_stat_reset
#define van_rwlock_stat_print	_van_rwlock_stat_print
#define van_rwlock_stat_reset	_van_rwlock_stat_reset

#define van_strarray_print	_van_strarray_print
#define van_strarray_alloc	_van_strarray_alloc
#define van_strarray_free	_van_strarray_free
#define van_strarray_add	_van_strarray_add
#define van_strarray_sort	_van_strarray_sort

#define van_error_print		_van_error_print
#define van_datum_defcmp	_van_datum_defcmp

#ifdef __cplusplus
}
#endif

#endif
