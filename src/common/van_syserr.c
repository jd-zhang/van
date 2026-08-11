#include "van_int.h"

int _van_getsys_error() {
	int ret;

	ret = errno;

	return (ret);
}

int _van_error_path(const char *str) {
	fprintf(stderr, "%s\n", str);

	return VAN_INVALID;
}

void _van_abort() {
	abort();
}
