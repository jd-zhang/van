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
	} else if (strcmp(argv[1], "table_t1_1e") == 0) {
		fprintf(stderr, "============ table_t1_1e() ===========\n");
		ret = table_t1_1e();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t1_1f") == 0) {
		fprintf(stderr, "============ table_t1_1f() ===========\n");
		ret = table_t1_1f();
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
	} else if (strcmp(argv[1], "table_t2_1") == 0) {
		fprintf(stderr, "============ table_t2_1() ===========\n");
		ret = table_t2_1();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t2_2") == 0) {
		fprintf(stderr, "============ table_t2_2() ===========\n");
		ret = table_t2_2();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t2_3") == 0) {
		fprintf(stderr, "============ table_t2_3() ===========\n");
		ret = table_t2_3();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t2_4") == 0) {
		fprintf(stderr, "============ table_t2_4() ===========\n");
		ret = table_t2_4();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t2_5") == 0) {
		fprintf(stderr, "============ table_t2_5() ===========\n");
		ret = table_t2_5();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t2_6") == 0) {
		fprintf(stderr, "============ table_t2_6() ===========\n");
		ret = table_t2_6();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t2_7") == 0) {
		fprintf(stderr, "============ table_t2_7() ===========\n");
		ret = table_t2_7();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t2_8") == 0) {
		fprintf(stderr, "============ table_t2_8() ===========\n");
		ret = table_t2_8();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t2_9") == 0) {
		fprintf(stderr, "============ table_t2_9() ===========\n");
		ret = table_t2_9();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t2_10") == 0) {
		fprintf(stderr, "============ table_t2_10() ===========\n");
		ret = table_t2_10();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_1a") == 0) {
		fprintf(stderr, "============ table_t3_1a() ===========\n");
		ret = table_t3_1a();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_1b") == 0) {
		fprintf(stderr, "============ table_t3_1b() ===========\n");
		ret = table_t3_1b();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_1c") == 0) {
		fprintf(stderr, "============ table_t3_1c() ===========\n");
		ret = table_t3_1c();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_1d") == 0) {
		fprintf(stderr, "============ table_t3_1d() ===========\n");
		ret = table_t3_1d();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_1e") == 0) {
		fprintf(stderr, "============ table_t3_1e() ===========\n");
		ret = table_t3_1e();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_1f") == 0) {
		fprintf(stderr, "============ table_t3_1f() ===========\n");
		ret = table_t3_1f();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_1g") == 0) {
		fprintf(stderr, "============ table_t3_1g() ===========\n");
		ret = table_t3_1g();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_1h") == 0) {
		fprintf(stderr, "============ table_t3_1h() ===========\n");
		ret = table_t3_1h();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_1i") == 0) {
		fprintf(stderr, "============ table_t3_1i() ===========\n");
		ret = table_t3_1i();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_1j") == 0) {
		fprintf(stderr, "============ table_t3_1j() ===========\n");
		ret = table_t3_1j();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_2a") == 0) {
		fprintf(stderr, "============ table_t3_2a() ===========\n");
		ret = table_t3_2a();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_2b") == 0) {
		fprintf(stderr, "============ table_t3_2b() ===========\n");
		ret = table_t3_2b();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_2c") == 0) {
		fprintf(stderr, "============ table_t3_2c() ===========\n");
		ret = table_t3_2c();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_2d") == 0) {
		fprintf(stderr, "============ table_t3_2d() ===========\n");
		ret = table_t3_2d();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_2e") == 0) {
		fprintf(stderr, "============ table_t3_2e() ===========\n");
		ret = table_t3_2e();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_2f") == 0) {
		fprintf(stderr, "============ table_t3_2f() ===========\n");
		ret = table_t3_2f();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_2g") == 0) {
		fprintf(stderr, "============ table_t3_2g() ===========\n");
		ret = table_t3_2g();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_2h") == 0) {
		fprintf(stderr, "============ table_t3_2h() ===========\n");
		ret = table_t3_2h();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_2i") == 0) {
		fprintf(stderr, "============ table_t3_2i() ===========\n");
		ret = table_t3_2i();
		fprintf(stderr, "\n");
	} else if (strcmp(argv[1], "table_t3_2j") == 0) {
		fprintf(stderr, "============ table_t3_2j() ===========\n");
		ret = table_t3_2j();
		fprintf(stderr, "\n");
	} else {
		fprintf(stderr, "Unknown test case: %s\n", argv[1]);
		ret = -1;
	}

	return (ret);
}
