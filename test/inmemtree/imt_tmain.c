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
extern int imt_t3_1a();
extern int imt_t3_1b();
extern int imt_t3_1c();
extern int imt_t3_1d();
extern int imt_t3_1e();
extern int imt_t3_1f();
extern int imt_t3_1g();
extern int imt_t3_1h();
extern int imt_t3_1i();
extern int imt_t3_1j();
extern int imt_t3_2a();
extern int imt_t3_2b();
extern int imt_t3_2c();
extern int imt_t3_2d();
extern int imt_t3_2e();
extern int imt_t3_2f();
extern int imt_t3_2g();
extern int imt_t3_2h();
extern int imt_t3_2i();
extern int imt_t3_2j();

/* imt_t1_2 takes a verbose flag; adapt it to the no-arg dispatch. */
static int imt_t1_2_wrap(void) { return (imt_t1_2(0)); }

typedef int (*test_func_t)(void);

static struct {
	const char *name;
	test_func_t func;
} test_cases[] = {
	{"imt_t1_1", (test_func_t)imt_t1_1},
	{"imt_t1_2", (test_func_t)imt_t1_2_wrap},
	{"imt_t1_3", (test_func_t)imt_t1_3},
	{"imt_t1_3b", (test_func_t)imt_t1_3b},
	{"imt_t1_3c", (test_func_t)imt_t1_3c},
	{"imt_t1_4", (test_func_t)imt_t1_4},
	{"imt_t2_1", (test_func_t)imt_t2_1},
	{"imt_t3_1a", (test_func_t)imt_t3_1a},
	{"imt_t3_1b", (test_func_t)imt_t3_1b},
	{"imt_t3_1c", (test_func_t)imt_t3_1c},
	{"imt_t3_1d", (test_func_t)imt_t3_1d},
	{"imt_t3_1e", (test_func_t)imt_t3_1e},
	{"imt_t3_1f", (test_func_t)imt_t3_1f},
	{"imt_t3_1g", (test_func_t)imt_t3_1g},
	{"imt_t3_1h", (test_func_t)imt_t3_1h},
	{"imt_t3_1i", (test_func_t)imt_t3_1i},
	{"imt_t3_1j", (test_func_t)imt_t3_1j},
	{"imt_t3_2a", (test_func_t)imt_t3_2a},
	{"imt_t3_2b", (test_func_t)imt_t3_2b},
	{"imt_t3_2c", (test_func_t)imt_t3_2c},
	{"imt_t3_2d", (test_func_t)imt_t3_2d},
	{"imt_t3_2e", (test_func_t)imt_t3_2e},
	{"imt_t3_2f", (test_func_t)imt_t3_2f},
	{"imt_t3_2g", (test_func_t)imt_t3_2g},
	{"imt_t3_2h", (test_func_t)imt_t3_2h},
	{"imt_t3_2i", (test_func_t)imt_t3_2i},
	{"imt_t3_2j", (test_func_t)imt_t3_2j},
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
