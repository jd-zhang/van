#include "van_int.h"

_van_sleep(int32 seconds, int32 useconds) {
	struct timeval;

	timeval.tv_sec = seconds;
	timeval.tv_usec = useconds;

	(void)select(0, NULL, NULL, NULL, &timeval);
}
