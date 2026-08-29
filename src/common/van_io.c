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

int _van_path_isabsolute(const char *path) {
	if (path != NULL && path[0] == ABS_ROOT)
		return (VAN_TRUE);

	return (VAN_FALSE);
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

int _van_file_fclose(FILE *f) {
	int ret;

	ret = fclose(f);
	if (ret != 0)
		ret = _van_getsys_error();

	return ret;
}

int _van_file_freadline(FILE *f, char *buf, size_t buflen) {
	/* char buf[VAN_MAXPATHLEN], *rpath; */
	const char *rpath;
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

int _van_file_fwriteline(FILE *f, char *buf, size_t buflen) {
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
	off_t off;
	ssize_t rbs;

	ret = 0;
	off = lseek(fd, 0, SEEK_END);
	if (off == (off_t)-1) {
		ret = _van_getsys_error();
		goto err;
	}

	rbs = write(fd, buf, nbytes);
	if (rbs == -1)
		ret = _van_getsys_error();
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

int _van_file_rename(const char *oldp, const char *newp) {
	int ret;

	ret = rename(oldp, newp);
	if (ret != 0)
		ret = _van_getsys_error();

	return ret;
}

int _van_file_unlink(const char *path) {
	int ret;

	ret = unlink(path);
	if (ret != 0)
		ret = _van_getsys_error();

	return ret;
}

int _van_iofile_open(const char *path, int oflag, mode_t mode, VAN_IOFILE **iofp) {
	int ret, ret1;
	VAN_IOFILE *iof;

	VAN_ASSERT(path != NULL);
	VAN_ASSERT(iofp != NULL);
	iof = NULL;

	ret = _van_calloc(NULL, 1, sizeof(VAN_IOFILE), &iof);
	if (ret != 0)
		goto err;

	iof->fd = -1;
	iof->oflag = oflag;
	iof->mode = mode;
	ret = _van_strdup(NULL, path, &iof->path);
	if (ret != 0) {
		_van_free(NULL, iof);
		goto err;
	}

	ret = _van_file_open(path, oflag, mode, &iof->fd);
	if (ret == 0)
		*iofp = iof;
	else {
		ret1 = _van_iofile_close(iof);
		ret = (ret != 0 ? ret : ret1); /* No really use here. */
		iof = NULL;
	}

err:
	return (ret);
}

int _van_iofile_close(VAN_IOFILE *iofile) {
	int ret;

	ret = 0;
	if (iofile->fd >= 0) {
		ret = _van_file_close(iofile->fd);
		iofile->fd = -1;
	}
	if (iofile->path != NULL) {
		_van_free(NULL, iofile->path);
		iofile->path = NULL;
	}
	_van_free(NULL, iofile);

	return (ret);
}

int _van_iofile_read(VAN_IOFILE *iofile, off_t off, uint8 *buf, uint32 size) {
	return _van_file_read(iofile->fd, buf, (size_t)size, off);
}

int _van_iofile_write(VAN_IOFILE *iofile, off_t off, uint8 *buf, uint32 size) {
	return _van_file_write(iofile->fd, (const void *)buf, (size_t)size, off);
}

int _van_iofile_stat(VAN_IOFILE *iofile, struct stat *st) {
	return _van_file_stat(iofile->fd, st);
}

int _van_iofile_set(VAN_IOFILE *iofile, off_t off, uint32 size, char c) {
	int ret;
	char buf[VAN_IOBUF_LEN];
	uint32 bufsz, left, wsz;
	off_t woff;

	ret = 0;
	bufsz = sizeof(buf);
	memset(buf, (int)c, bufsz);
	left = size;
	woff = off;

	while (left > 0) {
		wsz = (left >= bufsz ? bufsz : left);
		ret = _van_iofile_write(iofile, woff, buf, wsz);
		if (ret != 0)
			goto err;
		woff += wsz;
		left -= wsz;
	}

err:
	return (ret);
}
