/*
 * Copyright (c) 2026 VanDB Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "test_common.h"

extern int imt_t1_1();
extern int imt_t1_2(int verbose);
extern int imt_t1_3();
extern int imt_t1_3b();
extern int imt_t1_3c();
extern int imt_t1_4();
extern int imt_t2_1();

int main(int argc, char *argv[]) {
	int ret;
	ret = 0;

	if (strcmp(argv[1], "imt_t1_1") == 0) {
		fprintf(stderr, "============ imt_t1_1() ===========\n");
		ret = imt_t1_1();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t1_2") == 0) {
		fprintf(stderr, "============ imt_t1_2(0) ===========\n");
		ret = imt_t1_2(0);
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t1_3") == 0) {
		fprintf(stderr, "============ imt_t1_3() ===========\n");
		ret = imt_t1_3();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t1_3b") == 0) {
		fprintf(stderr, "============ imt_t1_3b() ===========\n");
		ret = imt_t1_3b();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t1_3c") == 0) {
		fprintf(stderr, "============ imt_t1_3c() ===========\n");
		ret = imt_t1_3c();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t1_4") == 0) {
		fprintf(stderr, "============ imt_t1_4() ===========\n");
		ret = imt_t1_4();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t2_1") == 0) {
		fprintf(stderr, "============ imt_t2_1() ===========\n");
		ret = imt_t2_1();
		fprintf(stderr, "\n");
	}

	return (ret);
}
