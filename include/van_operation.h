#ifndef _VAN_CURSOR_H
#define _VAN_CURSOR_H

struct _van_operation {
	VAN_TABLE *table;
	TAILQ_ENTRY(_van_operation) links;

	ticks_t create_ticks;

	VAN_DATUM retkey;
	VAN_DATUM retdata;

	int (*get)(VAN_OPERATION *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*put)(VAN_OPERATION *, VAN_DATUM *, VAN_DATUM *, flags_t);
	int (*del)(VAN_OPERATION *, VAN_DATUM *, flags_t);

#define	VAN_OP_INITED		0x01
#define VAN_OP_TRANSIENT	0x02
	flags_t flags;
};


#endif
