#include "test_common.h"

extern int table_t1_1();

int main(int argc, char *argv[]) {
	int ret;
	ret = 0;

	srand((unsigned int)time(NULL));

	if (strcmp(argv[1], "table_t1_1") == 0) {
		fprintf(stderr, "============ table_t1_1() ===========\n");
		ret = table_t1_1();
		fprintf(stderr, "\n");
	}

	return (ret);
}
