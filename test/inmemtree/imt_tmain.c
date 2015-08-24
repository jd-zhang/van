#include "test_common.h"

extern int imt_t1_1();
extern int imt_t1_2(int verbose);
extern int imt_t1_3() ;

int main(int argc, char *argv[]) {
	int ret;
	ret = 0;

	/*
	fprintf(stderr, "============ imt_t1_1() ===========\n");
	ret = imt_t1_1();
	fprintf(stderr, "\n");

	fprintf(stderr, "============ imt_t1_2(0) ===========\n");
	ret = imt_t1_2(0);
	fprintf(stderr, "\n");
	*/

	fprintf(stderr, "============ imt_t1_3() ===========\n");
	ret = imt_t1_3();
	fprintf(stderr, "\n");

	return (ret);
}
