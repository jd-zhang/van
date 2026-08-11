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
