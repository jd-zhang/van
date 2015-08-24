#ifndef _VAN_DATUM_H
#define _VAN_DATUM_H

struct _van_datum {
	void *data;
	size_t size;
	size_t ulen;
	flags_t flags;
};
/* Comparison function definition */
typedef int (*VAN_DATUM_CMPFUNC)(const VAN_DATUM *dt1, const VAN_DATUM *dt2);

#endif
