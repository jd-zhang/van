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

	if (strcmp(argv[1], "imt_t3_1a") == 0) {
		fprintf(stderr, "============ imt_t3_1a() ===========\n");
		ret = imt_t3_1a();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_1b") == 0) {
		fprintf(stderr, "============ imt_t3_1b() ===========\n");
		ret = imt_t3_1b();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_1c") == 0) {
		fprintf(stderr, "============ imt_t3_1c() ===========\n");
		ret = imt_t3_1c();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_1d") == 0) {
		fprintf(stderr, "============ imt_t3_1d() ===========\n");
		ret = imt_t3_1d();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_1e") == 0) {
		fprintf(stderr, "============ imt_t3_1e() ===========\n");
		ret = imt_t3_1e();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_1f") == 0) {
		fprintf(stderr, "============ imt_t3_1f() ===========\n");
		ret = imt_t3_1f();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_1g") == 0) {
		fprintf(stderr, "============ imt_t3_1g() ===========\n");
		ret = imt_t3_1g();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_1h") == 0) {
		fprintf(stderr, "============ imt_t3_1h() ===========\n");
		ret = imt_t3_1h();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_1i") == 0) {
		fprintf(stderr, "============ imt_t3_1i() ===========\n");
		ret = imt_t3_1i();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_1j") == 0) {
		fprintf(stderr, "============ imt_t3_1j() ===========\n");
		ret = imt_t3_1j();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_2a") == 0) {
		fprintf(stderr, "============ imt_t3_2a() ===========\n");
		ret = imt_t3_2a();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_2b") == 0) {
		fprintf(stderr, "============ imt_t3_2b() ===========\n");
		ret = imt_t3_2b();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_2c") == 0) {
		fprintf(stderr, "============ imt_t3_2c() ===========\n");
		ret = imt_t3_2c();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_2d") == 0) {
		fprintf(stderr, "============ imt_t3_2d() ===========\n");
		ret = imt_t3_2d();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_2e") == 0) {
		fprintf(stderr, "============ imt_t3_2e() ===========\n");
		ret = imt_t3_2e();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_2f") == 0) {
		fprintf(stderr, "============ imt_t3_2f() ===========\n");
		ret = imt_t3_2f();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_2g") == 0) {
		fprintf(stderr, "============ imt_t3_2g() ===========\n");
		ret = imt_t3_2g();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_2h") == 0) {
		fprintf(stderr, "============ imt_t3_2h() ===========\n");
		ret = imt_t3_2h();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_2i") == 0) {
		fprintf(stderr, "============ imt_t3_2i() ===========\n");
		ret = imt_t3_2i();
		fprintf(stderr, "\n");
	}

	if (strcmp(argv[1], "imt_t3_2j") == 0) {
		fprintf(stderr, "============ imt_t3_2j() ===========\n");
		ret = imt_t3_2j();
		fprintf(stderr, "\n");
	}

	return (ret);
}
