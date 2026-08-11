#ifndef _VAN_LOCK_H
#define _VAN_LOCK_H

/* Lock Mode */
#define VAN_LOCK_S 1
#define VAN_LOCK_X 2
#define VAN_LOCK_IS 3
#define VAN_LOCK_IX 4
#define VAN_LOCK_SIX 5
/* As there is no intention lock currently, we only have S/X lock currently */

/* Lock Type */
#define VAN_LOCK_STORE 1
#define VAN_LOCK_TABLE 2
#define VAN_LOCK_ROW 3
#define VAN_LOCK_COL 4
/* Currently, all locks are row lock(key-value) */


struct _van_lockobj {
	uint32 tid;
	uint32 size;
	uint32 fullsize;
	uint64 hashvalue;
	char *data;
};

struct _van_locker {
	uint32	lckid;
};

struct _van_lock {
	uint32	lckid;
};

#endif
