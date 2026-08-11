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

extern int misc_t1_1();

int main(int argc, char **argv) {
	int ret;
	ret = 0;

	srand((unsigned int)time(NULL));

	if (strcmp(argv[1], "misc_t1_1") == 0) {
		fprintf(stderr, "============ misc_t1_1() ===========\n");
		ret = misc_t1_1();
		fprintf(stderr, "\n");
	}

	return (ret);

}
