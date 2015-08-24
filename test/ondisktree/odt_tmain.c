#include "test_common.h"

extern int odt_t1_1();

int main(int argc, char *argv[]) {
	int ret;
	ret = 0;

	fprintf(stderr, "============ odt_t1_1() ===========\n");
	ret = odt_t1_1();
	fprintf(stderr, "\n");

	return (ret);
}
