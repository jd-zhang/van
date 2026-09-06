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

extern int table_t1_1();
extern int table_t1_1b();
extern int table_t1_1c();
extern int table_t1_1d();
extern int table_t1_2();
extern int table_t1_3();
extern int table_t1_4();
int main(int argc, char *argv[]) {
	int ret;
	ret = 0;

	srand((unsigned int)time(NULL));

	if (strcmp(argv[1], "table_t1_1") == 0) {
		fprintf(stderr, "============ table_t1_1() ===========\n");
		ret = table_t1_1();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t1_1b") == 0) {
		fprintf(stderr, "============ table_t1_1b() ===========\n");
		ret = table_t1_1b();
		fprintf(stderr, "\n");
	}  else if (strcmp(argv[1], "table_t1_1c") == 0) {
		fprintf(stderr, "============ table_t1_1c() ===========\n");
		ret = table_t1_1c();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t1_1d") == 0) {
		fprintf(stderr, "============ table_t1_1d() ===========\n");
		ret = table_t1_1d();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t1_2") == 0) {
		fprintf(stderr, "============ table_t1_2() ===========\n");
		ret = table_t1_2();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t1_3") == 0) {
		fprintf(stderr, "============ table_t1_3() ===========\n");
		ret = table_t1_3();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t1_4") == 0) {
		fprintf(stderr, "============ table_t1_4() ===========\n");
		ret = table_t1_4();
		fprintf(stderr, "\n");
	} else {
		fprintf(stderr, "Unknown test case: %s\n", argv[1]);
		ret = -1;
	}

	return (ret);
}
