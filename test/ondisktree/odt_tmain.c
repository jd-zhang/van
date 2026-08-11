#include "test_common.h"

extern int odt_t1_1();
extern int odt_t1_2();
extern int odt_t1_3();

int main(int argc, char *argv[]) {
	int ret;
	ret = 0;

	if (strcmp(argv[1], "odt_t1_1") == 0) {
		fprintf(stderr, "============ odt_t1_1() ===========\n");
		ret = odt_t1_1();
		fprintf(stderr, "\n");
	}


	if (strcmp(argv[1], "odt_t1_2") == 0) {
		fprintf(stderr, "============ odt_t1_2() ===========\n");
		ret = odt_t1_2();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "odt_t1_3") == 0) {
		fprintf(stderr, "============ odt_t1_3() ===========\n");
		ret = odt_t1_3();
		fprintf(stderr, "\n");
	}

	return (ret);
}
