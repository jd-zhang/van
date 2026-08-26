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

#include "van_log.h"

static int lognames_cmp(const void *d1, const void *d2) {
	long num1, num2;
	size_t start;

	start = strlen(VAN_LFPFX);
	num1 = atol(*(const char **)d1 + start);
	num2 = atol(*(const char **)d2 + start);

	return (int)(num1 - num2);
}

int _van_log_listfiles(VAN_LOG *logp, int *cntp) {
	VAN_STORE *store;
	int ret, ret1, cnt;
	const char *ldir;
	DIR *dirobj;
	struct dirent *dent;

	cnt = 0;
	store = logp->store;
	ldir = store->store_dir;

	dirobj = opendir(ldir);
	if (dirobj == NULL) {
		ret = _van_getsys_error();
		goto err;
	}

	while ((dent = readdir(dirobj)) != NULL) {
		if (!_van_log_islogfile(dent->d_name))
			continue;
		cnt++;
		ret = _van_strarray_add(logp->lognames, dent->d_name);
	}
	if (logp->lognames->size > 0) {
		ret = _van_strarray_sort(logp->lognames, lognames_cmp);
	}
err:
	if (dirobj != NULL) {
		ret1 = closedir(dirobj);
		if (ret == 0)
			ret = ret1;
	}
	if (ret == 0 && cntp != NULL)
		*cntp = cnt;

	return (ret);
}

int _van_log_islogfile(const char *lpath) {
	size_t least, plen, i;
	least = strlen(VAN_LFPFX);

	if (lpath == NULL)
		return 0;
	if (strlen(lpath) <= least)
		return 0;
	if (strncmp(lpath, VAN_LFPFX, least) != 0)
		return 0;

	plen = strlen(lpath);
	for (i = least; i < plen; i++)
		if (!isdigit(lpath[i]))
			return 0;

	return 1;
}

/* Should hold the mutex of logp->mutex */
int _van_log_newfile(VAN_LOG *logp) {
	int ret;
	VAN_LSN cur_lsn;
	char fname[VAN_MAXPATHLEN];
	char lgpath[VAN_MAXPATHLEN];
	uint32 fnum;
	int oflag;
	mode_t omode;

	/* Clean */
	if (logp->cur_logfile != NULL) {
		ret = _van_iofile_close(logp->cur_logfile);
		logp->cur_logfile = NULL;
		CHK_ERR(ret);
	}

	/* Init */
	cur_lsn = logp->cur_lsn;
	cur_lsn.file++;
	cur_lsn.offset = 0;
	fnum = cur_lsn.file;
	memset(fname, 0, sizeof(fname));
	memset(lgpath, 0, sizeof(lgpath));
	sprintf(fname, VAN_LFNAME, fnum);
	ret = _van_store_path_noalloc(logp->store, lgpath,
	    sizeof(lgpath), fname);
	CHK_ERR(ret);

	/* Open */
	oflag = O_RDWR | O_TRUNC | O_CREAT; /* O_APPEND ? */
	omode = DEF_FILE_MODE;
	ret = _van_iofile_open(lgpath, oflag, omode, &logp->cur_logfile);
	CHK_ERR(ret);

	/* Write the header, and move cur_lsn ? */
	ret = _van_iofile_write(logp->cur_logfile, 0, (uint8 *)&logp->header,
	    sizeof(VAN_LOGFILE_HEADER));
	/* TODO: error check here */
	cur_lsn.offset = sizeof(VAN_LOGFILE_HEADER);
	logp->prev_offset = 0;
	logp->cur_lsn = cur_lsn;

err:
	return (ret);
}

int _van_log_openfile(VAN_LOG *logp, uint32 fnum, VAN_LOGFILE_INFO **infopp) {
	int ret;
	VAN_IOFILE *logfile;
	VAN_LOGFILE_INFO *infop;
	char fname[VAN_MAXPATHLEN];
	char lgpath[VAN_MAXPATHLEN];
	char buf[128];
	int oflag;
	mode_t omode;
	struct stat st;

	ret = 0;
	logfile = NULL;
	infop = NULL;

	memset(fname, 0, sizeof(fname));
	memset(lgpath, 0, sizeof(lgpath));
	sprintf(fname, VAN_LFNAME, fnum);
	ret = _van_store_path_noalloc(logp->store, lgpath,
	    sizeof(lgpath), fname);
	CHK_ERR(ret);

	/* Open */
	oflag = O_RDONLY; /* O_APPEND ? */
	omode = DEF_FILE_MODE;
	ret = _van_iofile_open(lgpath, oflag, omode, &logfile);
	/* TODO: The error needs conversion for file not found ! */
	CHK_ERR(ret);

	ret = _van_calloc(NULL, 1, sizeof(VAN_LOGFILE_INFO), &infop);
	if (ret != 0) {
		_van_free(NULL, logfile);
		logfile = NULL;
		goto err;
	}
	infop->logp = logp;
	infop->logfile = logfile;

	ret = _van_iofile_read(logfile, 0, buf, VAN_LOGFILE_HDRSIZE);
	CHK_ERR(ret);
	memcpy(&infop->filehdr, buf, VAN_LOGFILE_HDRSIZE);

	ret = _van_iofile_stat(logfile, &st);
	CHK_ERR(ret);
	infop->filesize = st.st_size;

	if (st.st_size == infop->filehdr.logsize)
		infop->completed = 1;
	else
		infop->completed = 0;

	/* Only read the tail record if it is complete */
	if (infop->completed) {
		ret = _van_iofile_read(infop->logfile,
		    infop->filehdr.logsize - VAN_LOGREC_HDRSIZE,
		    buf, VAN_LOGREC_HDRSIZE);
		CHK_ERR(ret);
		memcpy(&infop->tailhdr, buf, VAN_LOGREC_HDRSIZE);
		infop->last_lsn.file = fnum;
		infop->last_lsn.offset = infop->tailhdr.prev;
	}

	*infopp = infop;
err:
	if (ret != 0) {
		if (logfile != NULL)
			(void)_van_iofile_close(logfile);
		if (infop != NULL)
			_van_free(NULL, infop);
	}
	return (ret);
}

int _van_logc_openfile(VAN_LOG_CURSOR *lgc, VAN_LSN *lsn) {
	int ret;
	VAN_LOG *logp;
	uint32 fnum;
	char fname[VAN_MAXPATHLEN];
	char lgpath[VAN_MAXPATHLEN];
	char buf[128];
	int oflag;
	mode_t omode;
	struct stat st;

	ret = 0;
	fnum = lsn->file;
	logp = lgc->logp;

	/* The file does not change */
	if (lgc->logfile != NULL && lgc->lastlsn.file == fnum)
		return (ret);

	if (lgc->logfile != NULL) {
		ret = _van_iofile_close(lgc->logfile);
		lgc->logfile = NULL;
		CHK_ERR(ret);
	}

	memset(fname, 0, sizeof(fname));
	memset(lgpath, 0, sizeof(lgpath));
	sprintf(fname, VAN_LFNAME, fnum);
	ret = _van_store_path_noalloc(logp->store, lgpath,
	    sizeof(lgpath), fname);
	CHK_ERR(ret);

	/* Open */
	oflag = O_RDONLY; /* O_APPEND ? */
	omode = DEF_FILE_MODE;
	ret = _van_iofile_open(lgpath, oflag, omode, &lgc->logfile);
	/* TODO: The error needs conversion for file not found ! */
	CHK_ERR(ret);

	ret = _van_iofile_read(lgc->logfile, 0, buf, VAN_LOGFILE_HDRSIZE);
	CHK_ERR(ret);
	memcpy(&lgc->filehdr, buf, VAN_LOGFILE_HDRSIZE);

	ret = _van_iofile_stat(lgc->logfile, &st);
	CHK_ERR(ret);

	if (st.st_size == lgc->filehdr.logsize)
		lgc->completed = 1;
	else
		lgc->completed = 0;

	/* Only read the tail record if it is complete */
	if (lgc->completed) {
		ret = _van_iofile_read(lgc->logfile,
		    lgc->filehdr.logsize - VAN_LOGREC_HDRSIZE,
		    buf, VAN_LOGREC_HDRSIZE);
		CHK_ERR(ret);
		memcpy(&lgc->tailhdr, buf, VAN_LOGREC_HDRSIZE);
	}

err:
	if (ret != 0 && lgc->logfile != NULL) {
		(void)_van_iofile_close(lgc->logfile);
		lgc->logfile = NULL;
	}

	return (ret);
}
