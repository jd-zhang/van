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
extern int table_t1_1e();
extern int table_t1_1f();
extern int table_t1_2();
extern int table_t1_3();
extern int table_t1_4();
extern int table_t1_5a();
extern int table_t1_5b();
extern int table_t1_5c();
extern int table_t1_5d();
extern int table_t2_1();
extern int table_t2_2();
extern int table_t2_3();
extern int table_t2_4();
extern int table_t2_5();
extern int table_t2_6();
extern int table_t2_7();
extern int table_t2_8();
extern int table_t2_9();
extern int table_t2_10();
extern int table_t3_1a();
extern int table_t3_1b();
extern int table_t3_1c();
extern int table_t3_1d();
extern int table_t3_1e();
extern int table_t3_1f();
extern int table_t3_1g();
extern int table_t3_1h();
extern int table_t3_1i();
extern int table_t3_1j();
extern int table_t3_2a();
extern int table_t3_2b();
extern int table_t3_2c();
extern int table_t3_2d();
extern int table_t3_2e();
extern int table_t3_2f();
extern int table_t3_2g();
extern int table_t3_2h();
extern int table_t3_2i();
extern int table_t3_2j();

typedef int (*test_func_t)(void);

static struct {
	const char *name;
	test_func_t func;
} test_cases[] = {
	{"table_t1_1", (test_func_t)table_t1_1},
	{"table_t1_1b", (test_func_t)table_t1_1b},
	{"table_t1_1c", (test_func_t)table_t1_1c},
	{"table_t1_1d", (test_func_t)table_t1_1d},
	{"table_t1_1e", (test_func_t)table_t1_1e},
	{"table_t1_1f", (test_func_t)table_t1_1f},
	{"table_t1_2", (test_func_t)table_t1_2},
	{"table_t1_3", (test_func_t)table_t1_3},
	{"table_t1_4", (test_func_t)table_t1_4},
	{"table_t1_5a", (test_func_t)table_t1_5a},
	{"table_t1_5b", (test_func_t)table_t1_5b},
	{"table_t1_5c", (test_func_t)table_t1_5c},
	{"table_t1_5d", (test_func_t)table_t1_5d},
	{"table_t2_1", (test_func_t)table_t2_1},
	{"table_t2_2", (test_func_t)table_t2_2},
	{"table_t2_3", (test_func_t)table_t2_3},
	{"table_t2_4", (test_func_t)table_t2_4},
	{"table_t2_5", (test_func_t)table_t2_5},
	{"table_t2_6", (test_func_t)table_t2_6},
	{"table_t2_7", (test_func_t)table_t2_7},
	{"table_t2_8", (test_func_t)table_t2_8},
	{"table_t2_9", (test_func_t)table_t2_9},
	{"table_t2_10", (test_func_t)table_t2_10},
	{"table_t3_1a", (test_func_t)table_t3_1a},
	{"table_t3_1b", (test_func_t)table_t3_1b},
	{"table_t3_1c", (test_func_t)table_t3_1c},
	{"table_t3_1d", (test_func_t)table_t3_1d},
	{"table_t3_1e", (test_func_t)table_t3_1e},
	{"table_t3_1f", (test_func_t)table_t3_1f},
	{"table_t3_1g", (test_func_t)table_t3_1g},
	{"table_t3_1h", (test_func_t)table_t3_1h},
	{"table_t3_1i", (test_func_t)table_t3_1i},
	{"table_t3_1j", (test_func_t)table_t3_1j},
	{"table_t3_2a", (test_func_t)table_t3_2a},
	{"table_t3_2b", (test_func_t)table_t3_2b},
	{"table_t3_2c", (test_func_t)table_t3_2c},
	{"table_t3_2d", (test_func_t)table_t3_2d},
	{"table_t3_2e", (test_func_t)table_t3_2e},
	{"table_t3_2f", (test_func_t)table_t3_2f},
	{"table_t3_2g", (test_func_t)table_t3_2g},
	{"table_t3_2h", (test_func_t)table_t3_2h},
	{"table_t3_2i", (test_func_t)table_t3_2i},
	{"table_t3_2j", (test_func_t)table_t3_2j},
	{NULL, NULL}
};

int main(int argc, char *argv[]) {
	size_t i;
	int ret;

	srand((unsigned int)time(NULL));

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <case|list>\n", argv[0]);
		return (1);
	}

	if (strcmp(argv[1], "list") == 0) {
		for (i = 0; test_cases[i].name != NULL; i++)
			printf("%s\n", test_cases[i].name);
		return (0);
	}

	ret = 0;
	for (i = 0; test_cases[i].name != NULL; i++) {
		if (strcmp(argv[1], test_cases[i].name) == 0) {
			fprintf(stderr, "============ %s() ===========\n",
			    test_cases[i].name);
			ret = test_cases[i].func();
			fprintf(stderr, "\n");
			return (ret);
		}
	}

	fprintf(stderr, "no such test: %s\n", argv[1]);
	return (-1);
}
