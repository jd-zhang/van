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

int _van_log_open(VAN_STORE *store) {
	int ret;
	VAN_LOG	*logp;

	VAN_ASSERT(store != NULL);
	logp = NULL;
	ret = _van_calloc(NULL, 1, sizeof(VAN_LOG), &logp);
	if (ret != 0)
		return (ret);

	logp->store = store;
	ret = _van_log_init(logp);
	if (ret == 0)
		store->log_handle = logp;
	else
		(void)_van_log_cleanup(logp);

	return (ret);
}

int _van_log_close(VAN_STORE *store) {
	int ret;
	VAN_LOG *logp;

	ret =0;
	logp = store->log_handle;
	if (logp->cur_logfile != NULL) {
		ret = _van_iofile_close(logp->cur_logfile);
		logp->cur_logfile = NULL;
		CHK_ERR(ret);
	}

	ret = _van_strarray_free(logp->lognames);
	CHK_ERR(ret);

	VAN_MUTEX_DESTROY(logp, mutex, ret);
	(void)_van_log_cleanup(logp);
	store->log_handle = NULL;

err:
	return (ret);
}

int _van_log_init(VAN_LOG *logp) {
	int ret, fcnt, oflag;
	mode_t omode;
	uint32 fnum;
	VAN_IOFILE *logfile;
	char fname[VAN_MAXPATHLEN];
	char lgpath[VAN_MAXPATHLEN];
	struct stat st;

	VAN_ASSERT(logp != NULL);
	ret = 0;
	fcnt = 0;
	fnum = 0;
	logfile = NULL;

	INIT_LSN(&logp->cur_lsn);
	logp->prev_offset = 0;
	logp->cur_logfile = NULL;
	VAN_MUTEX_INIT(logp, mutex, ret);
	if (ret != 0)
		goto err;

	logp->header.version = VAN_LOG_VERSION;
	logp->header.magic = VAN_LOG_MAGIC;
	logp->header.logsize = VAN_DEF_LOGSIZE;
	logp->header.unused = 0;
	logp->firstfnum = 1;
	logp->cursor = _van_log_cursor_alloc;

	ret = _van_strarray_alloc(&logp->lognames, 0);
	CHK_ERR(ret);

	ret = _van_log_listfiles(logp, &fcnt);
	CHK_ERR(ret);

	if (fcnt == 0) {
		ret = _van_log_newfile(logp);
	} else {
		/*
		 * There are existing log files, open the latest one
		 * and continue to write after its tail. The log file
		 * names are sorted by the file number, so the latest
		 * one is the last element of the array.
		 */
		fnum = (uint32)atol(
		    logp->lognames->array[logp->lognames->size - 1] +
		    strlen(VAN_LFPFX));

		memset(fname, 0, sizeof(fname));
		memset(lgpath, 0, sizeof(lgpath));
		sprintf(fname, VAN_LFNAME, fnum);
		ret = _van_store_path_noalloc(logp->store, lgpath,
		    sizeof(lgpath), fname);
		CHK_ERR(ret);

		/* Open without truncation to keep the old records */
		oflag = O_RDWR;
		omode = DEF_FILE_MODE;
		ret = _van_iofile_open(lgpath, oflag, omode, &logfile);
		CHK_ERR(ret);

		ret = _van_iofile_stat(logfile, &st);
		if (ret != 0) {
			(void)_van_iofile_close(logfile);
			logfile = NULL;
			goto err;
		}

		logp->cur_logfile = logfile;
		logp->cur_lsn.file = fnum;
		logp->cur_lsn.offset = (uint32)st.st_size;
		logp->prev_offset = 0;
	}
err:
	return (ret);
}

int _van_log_cleanup(VAN_LOG *logp) {
	int ret;

	ret = 0;

	/* Just free the handle currently */
	_van_free(NULL, logp);

	return (ret);
}
