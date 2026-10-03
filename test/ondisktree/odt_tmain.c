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

extern int odt_t1_1();
extern int odt_t1_1b();
extern int odt_t1_1c();
extern int odt_t1_2();
extern int odt_t1_3();
extern int odt_t3_1a();
extern int odt_t3_1b();
extern int odt_t3_1c();
extern int odt_t3_1d();
extern int odt_t3_1e();
extern int odt_t3_1f();
extern int odt_t3_1g();
extern int odt_t3_1h();
extern int odt_t3_1i();
extern int odt_t3_1j();
extern int odt_t3_2a();
extern int odt_t3_2b();
extern int odt_t3_2c();
extern int odt_t3_2d();
extern int odt_t3_2e();

typedef int (*test_func_t)(void);

static struct {
	const char *name;
	test_func_t func;
} test_cases[] = {
	{"odt_t1_1", (test_func_t)odt_t1_1},
	{"odt_t1_1b", (test_func_t)odt_t1_1b},
	{"odt_t1_1c", (test_func_t)odt_t1_1c},
	{"odt_t1_2", (test_func_t)odt_t1_2},
	{"odt_t1_3", (test_func_t)odt_t1_3},
	{"odt_t3_1a", (test_func_t)odt_t3_1a},
	{"odt_t3_1b", (test_func_t)odt_t3_1b},
	{"odt_t3_1c", (test_func_t)odt_t3_1c},
	{"odt_t3_1d", (test_func_t)odt_t3_1d},
	{"odt_t3_1e", (test_func_t)odt_t3_1e},
	{"odt_t3_1f", (test_func_t)odt_t3_1f},
	{"odt_t3_1g", (test_func_t)odt_t3_1g},
	{"odt_t3_1h", (test_func_t)odt_t3_1h},
	{"odt_t3_1i", (test_func_t)odt_t3_1i},
	{"odt_t3_1j", (test_func_t)odt_t3_1j},
	{"odt_t3_2a", (test_func_t)odt_t3_2a},
	{"odt_t3_2b", (test_func_t)odt_t3_2b},
	{"odt_t3_2c", (test_func_t)odt_t3_2c},
	{"odt_t3_2d", (test_func_t)odt_t3_2d},
	{"odt_t3_2e", (test_func_t)odt_t3_2e},
	{NULL, NULL}
};

int main(int argc, char *argv[]) {
	size_t i;
	int ret;

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
