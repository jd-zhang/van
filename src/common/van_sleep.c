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
#include "van_int.h"

void _van_sleep(int32 seconds, int32 useconds) {
	struct timeval t;

	t.tv_sec = seconds;
	t.tv_usec = useconds;

	(void)select(0, NULL, NULL, NULL, &t);
}

void _van_yield(void) {
	struct timeval t;

	t.tv_sec = 0;
	t.tv_usec = 0;

	(void)select(0, NULL, NULL, NULL, &t);
}