#include "van_int.h"

void _van_sleep(int32 seconds, int32 useconds) {
	struct timeval t;

	t.tv_sec = seconds;
	t.tv_usec = useconds;

	(void)select(0, NULL, NULL, NULL, &t);
}
