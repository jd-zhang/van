#ifndef _WAN_H
#define _WAN_H

#include "van_types.h"
#include "van_datum.h"

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

/* Cursor put flags */
#define VAN_NOOVERWRITE		0x1

/* flags for opening a ondisk tree. */
#define VAN_OPEN_CREAT		0x01
#define VAN_OPEN_TRUNC		0x02
#define VAN_OPEN_DIRECT		0x04
#define VAN_OPEN_APPEND		0x08
#define VAN_OPEN_NONBLOCK	0x10
#define VAN_OPEN_RDONLY		0x20
#define VAN_OPEN_EXCL		0x40

/* Error return code */
#define VAN_NOTFOUND		(-30988)
#define VAN_INVALID		(-30987)
#define VAN_BUFFER_SMALL	(-30986)
#define VAN_STALE		(-30985)	/* Condition has changed */
#define VAN_BAD			(-30984)
#define VAN_DELETED		(-30983)
#define VAN_READONLY		(-30982)
#define VAN_OVERFLOW		(-30981)
#endif
