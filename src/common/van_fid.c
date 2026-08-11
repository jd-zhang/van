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
#include "van_ondisk_tree.h"

int _van_fid(int fd, uint8 *fidp) {
	pid_t pid;
	size_t i;
	struct stat sb;
	uint32 tmp;
	uint8 *p;
	int ret;

	memset(fidp, 0, OD_FILEID_LEN);
	ret = fstat(fd, &sb);
	if (ret != 0) {
		ret = _van_getsys_error();
		goto err;
	}
		
	tmp = (uint32)sb.st_ino;
	for (p = (uint8 *)&tmp, i = sizeof(uint32); i > 0; --i)
		*fidp++ = *p++;

	tmp = (uint32)sb.st_dev;
	for (p = (uint8 *)&tmp, i = sizeof(uint32); i > 0; --i)
		*fidp++ = *p++;	

	pid = getpid();
	tmp = (uint32)pid;
	for (p = (uint8 *)&tmp, i = sizeof(uint32); i > 0; --i)
		*fidp++ = *p++;	

	/* TODO: other 8 bytes */
err:
	return (ret);
}
