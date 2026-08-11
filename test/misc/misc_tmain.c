#include "test_common.h"

extern int misc_t1_1();

int main(int argc, char **argv) {
	int ret;
	ret = 0;

	srand((unsigned int)time(NULL));

	if (strcmp(argv[1], "misc_t1_1") == 0) {
		fprintf(stderr, "============ misc_t1_1() ===========\n");
		ret = misc_t1_1();
		fprintf(stderr, "\n");
	}

	return (ret);

}
