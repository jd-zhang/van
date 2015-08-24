#include "van_int.h"

int _van_path_isabsolute(const char *path) {
	if (path != NULL && path[0] == ABS_ROOT)
		return (TRUE);

	return (FALSE);
}

int _van_file_fopen(const char *path, const char *mode, FILE **fp) {
	FILE *f;
	int ret;
	
	ret = 0;
	f = fopen(path, mode);
	if (f == NULL)
		ret = _van_getsys_error();
	else
		*fp = f;

	return ret;
}

int _van_file_fclose(const FILE *f) {
	int ret;

	ret = fclose(f);
	if (ret != 0)
		ret = _van_getsys_error();

	return ret;
}

int _van_file_freadline(const FILE *f, char *buf, size_t buflen) {
	char buf[VAN_MAXPATHLEN], *rpath;
	int ret;
	size_t len;
	
	ret = 0;
	rpath = fgets(buf, VAN_MAXPATHLEN, f);
	if (rpath == NULL || strlen(rpath) == 0) {
		if (feof(f) ||  strlen(rpath) == 0)
			ret = VAN_NOTFOUND;
		else 
			ret = _van_getsys_error();
		goto err;
	}
	len = strlen(buf);
	if (buf[len - 1] == '\n')
		buf[len - 1] = '\0';
err:
	return ret;
}

int _van_file_fwriteline(const FILE *f, char *buf, size_t buflen) {
	int ret, len;

	/* buflen is not used currently */
	ret = 0;
	len = fprintf(f, "%s\n", buf);
	if (len <= 0)
		ret = _van_getsys_error();

	return ret;
}

int _van_file_open(const char *path, int oflag, mode_t omode,
    int *fdp) {
	int fd, ret;

	ret = 0;
	if (omode == 0)
		fd = open(path, oflag);
	else
		fd = open(path, oflag, omode);
	if (fd == -1)
		ret = _van_getsys_error();
	else 
		*fdp = fd;

	return (ret);
}

int _van_file_fsync(int fd) {
	int ret;

	ret = fsync(fd);
	if (ret != 0)
		ret = _van_getsys_error();

	return (ret);
}

int _van_file_close(int fd) {
	int ret;

	ret = close(fd);
	if (ret != 0)
		ret = _van_getsys_error();

	return (ret);
}

/* Using pread here */
int _van_file_openread(const char *path, off_t start, 
    size_t nbytes, void *strp) {
	int fd, ret, oflag;
	char *buf;

	fd = -1;
	buf = NULL;
	oflag = O_RDONLY;

	ret = _van_file_open(path, oflag, 0, &fd);
	CHK_RETURN(ret);
	ret = _van_malloc(NULL, nbytes, &buf);
	if (ret != 0)
		goto clean;
	ret = _van_file_read(fd, buf, nbytes, start);
	if (ret != 0)
		goto clean;
	ret = _van_file_close(fd);
	if (ret != 0)
		goto clean;

	*(char **)strp = buf;

	if (0) {
clean:	
		if (ret != 0)
			ret = _van_getsys_error();
		if (fd != -1) {
			(void)_van_file_close(fd);
			fd = -1;
		}
		if (buf != NULL) {
			_van_free(NULL, buf);
			buf = NULL;
		}
	}
	return (ret);
}

/* Using pread here */
int _van_file_openreadall(const char *path, void *strp, size_t *np) {
	int fd, ret, oflag;
	char *buf;
	struct stat st;
	size_t nbytes;

	fd = -1;
	buf = NULL;
	oflag = O_RDONLY;

	ret = _van_file_open(path, oflag, 0, &fd);
	CHK_RETURN(ret);
	ret = _van_file_stat(fd, &st);
	if (ret != 0)
		goto clean;
	nbytes = st.st_size;
	ret = _van_malloc(NULL, nbytes, &buf);
	if (ret != 0)
		goto clean;
	ret = _van_file_read(fd, buf, nbytes, 0);
	if (ret != 0)
		goto clean;
	ret = _van_file_close(fd);
	if (ret != 0)
		goto clean;

	*np = nbytes;
	*(char **)strp = buf;

	if (0) {
clean:
		if (ret != 0)
			ret = _van_getsys_error();
		if (fd != -1) {
			(void)_van_file_close(fd);
			fd = -1;
		}
		if (buf != NULL) {
			_van_free(NULL, buf);
			buf = NULL;
		}		
	}
	return (ret);
}

int _van_file_readall(int fd, size_t nbytes, void *strp) {
	int ret;
	char *buf;

	buf = NULL;

	ret = _van_malloc(NULL, nbytes, &buf);
	CHK_RETURN(ret);
	ret = _van_file_read(fd, buf, nbytes, 0);
	if (ret != 0)
		goto clean;

	*(char **)strp = buf;

	if (0) {
clean:
		if (buf != NULL) {
			_van_free(NULL, buf);
			buf = NULL;
		}
	}
	return (ret);
}

int _van_file_readblk(int fd, uint32 blksize, uint32 blk,
    off_t start, void *blkaddr) {
	off_t pos;
	int ret;

	ret = 0;

	pos = start + (off_t)blk * (off_t)blksize;
	ret = _van_file_read(fd, blkaddr, blksize, pos);

	return (ret);
}

int _van_file_readblk_alloc(int fd, uint32 blksize, uint32 blk,
    off_t start, void *retp) {
	off_t pos;
	int ret;
	char *buf;

	buf = NULL;
	ret = 0;

	ret = _van_malloc(NULL, blksize, &buf);
	CHK_RETURN(ret);
	pos = start + (off_t)blk * (off_t)blksize;
	ret = _van_file_read(fd, buf, blksize, pos);
	if (ret != 0)
		_van_free(NULL, buf);
	else
		*(char **)retp = buf;

	return (ret);
}

int _van_file_writeblk(int fd, uint32 blksize, uint32 blk,
    off_t start, const void *blkaddr) {
	off_t pos;
	int ret;

	ret = 0;
	pos = start + (off_t)blk * (off_t)blksize;
	ret = _van_file_write(fd, blkaddr, (size_t)blksize, pos);

	return (ret);
}

int _van_file_lseek(int fd, off_t pos) {
	off_t off;
	int ret;
	
	ret = 0;
	off = lseek(fd, pos, SEEK_SET);
	if (off == (off_t)-1)
		ret = _van_getsys_error();

	return (ret);
}

/* TODO: Should have retry if reading bytes not enough. */
int _van_file_read(int fd, void *buf, size_t nbytes, off_t pos) {
	int ret;
	ssize_t rbs;

	ret = 0;
	rbs = pread(fd, buf, nbytes, pos);
	if (rbs == -1)
		ret = _van_getsys_error();

	return (ret);
}

/* TODO: Should have retry if writing bytes not enough. */
int _van_file_write(int fd, const void *buf, size_t nbytes, off_t pos) {
	int ret;
	ssize_t rbs;

	ret = 0;
	rbs = pwrite(fd, buf, nbytes, pos);
	if (rbs == -1)
		ret = _van_getsys_error();

	return (ret);
}

int _van_file_append(int fd, const void *buf, size_t nbytes) {
	int ret;
	ssize_t rbs;

	ret = lseek(fd, 0, SEEK_END);
	if (ret == -1) {
		ret = _van_getsys_error();
		goto err;
	} 

	rbs = write(fd, buf, nbytes);
	if (rbs == -1)
		ret = _van_getsys_error();
	else
		ret = 0;

err:
	return (ret);
}

int _van_file_stat(int fd, struct stat *st) {
	int ret;

	ret = fstat(fd, st);
	if (ret != 0)
		ret = _van_getsys_error();

	return (ret);
}

int _van_file_getfid(int fd, char *fidp) {
	int ret;
	time_t t;
	pid_t pid;
	uint32 id, temp;
	struct stat st;
	unsigned v;

	ret = 0;
	memset(fidp, 0, OD_FILEID_LEN);

	id = (uint32)getpid() ^ (uint32)time(NULL);
	memcpy(fidp, &id, sizeof(id));
	fidp += sizeof(id);

	ret = _van_file_stat(fd, &st);
	if (ret != 0)
		return (ret);

	temp = (uint32)st.st_ino;
	memcpy(fidp, &temp, sizeof(temp));
	fidp += sizeof(temp);

	temp = (uint32)st.st_dev;
	memcpy(fidp, &temp, sizeof(temp));
	fidp += sizeof(temp);

	/* What others ? */
	v = (unsigned)time(NULL);
	temp = rand_r(&v);
	temp = (uint32)st.st_dev;
	memcpy(fidp, &temp, sizeof(temp));
	fidp += sizeof(temp);

	ret = 0;

	return (ret);
}

int _van_file_rename(const char *oldp, const char *newp) {
	int ret;

	ret = rename(old, new);
	if (ret != 0)
		ret = _van_getsys_error();

	return ret;
}
