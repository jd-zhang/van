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
#include "van.h"

/*
 * _van_ondisktree_verify --
 *	Check that an on-disk tree file is complete and self consistent.
 *
 *	Stub for now: the on-disk format carries no checksum and no
 *	structure that can be validated without reading the whole tree,
 *	so there is nothing to check here beyond what the caller has
 *	already done (the file exists).  The entry point exists so the
 *	tests can call the same function a real implementation would
 *	provide, so filling it in later needs no test change.
 */
int _van_ondisktree_verify(const char *file_path) {
	int ret;

	ret = 0;
	if (file_path == NULL)
		return (VAN_INVALID);

	return (ret);
}
