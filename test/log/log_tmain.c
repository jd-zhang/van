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

extern int log_t1_1();
extern int log_t1_2();
extern int log_t1_3();
int main(int argc, char *argv[]) {
	int ret;
	ret = 0;

	srand((unsigned int)time(NULL));

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <case|list>\n", argv[0]);
		return (1);
	}

	if (strcmp(argv[1], "list") == 0) {
		fprintf(stderr, "log_t1_1\n");
		fprintf(stderr, "log_t1_2\n");
		fprintf(stderr, "log_t1_3\n");
		return (0);
	}

	if (strcmp(argv[1], "log_t1_1") == 0) {
		fprintf(stderr, "============ log_t1_1() ===========\n");
		ret = log_t1_1();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "log_t1_2") == 0) {
		fprintf(stderr, "============ log_t1_2() ===========\n");
		ret = log_t1_2();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "log_t1_3") == 0) {
		fprintf(stderr, "============ log_t1_3() ===========\n");
		ret = log_t1_3();
		fprintf(stderr, "\n");
	} else {
		fprintf(stderr, "no such test: %s\n", argv[1]);
		ret = -1;
	}

	return (ret);
}
