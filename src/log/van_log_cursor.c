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

/*
 * Check if the LSN is valid.  (The IS_VALID_LSN macro in van_log.h
 * cannot be used with "&lsn" because of operator precedence.)
 */
#define IS_VALID_LSNP(lsnp)	((lsnp)->file != 0 && (lsnp)->offset != 0)

/*
 * _van_log_cursor_alloc --
 *	Allocate and initialize a log cursor.
 */
int _van_log_cursor_alloc(VAN_LOG *logp, VAN_LOG_CURSOR **lgcpp) {
	int ret;
	VAN_LOG_CURSOR *lgc;

	VAN_ASSERT(logp != NULL);
	VAN_ASSERT(lgcpp != NULL);
	lgc = NULL;
	ret = 0;

	ret = _van_calloc(NULL, 1, sizeof(VAN_LOG_CURSOR), &lgc);
	if (ret != 0)
		return (ret);

	lgc->logp = logp;
	lgc->get = _van_logrec_cget;
	lgc->close = _van_log_cursor_close;
	*lgcpp = lgc;

	return (0);
}

/*
 * _van_log_cursor_close --
 *	Close the log cursor and free its resources.
 */
int _van_log_cursor_close(VAN_LOG_CURSOR *lgc) {
	int ret;

	ret = 0;
	if (lgc == NULL)
		return (0);

	if (lgc->logfile != NULL) {
		ret = _van_iofile_close(lgc->logfile);
		lgc->logfile = NULL;
	}
	_van_free(NULL, lgc);

	return (ret);
}

/*
 * _van_logc_getrec --
 *	Read the record at lsn->offset from the cursor's current
 *	logfile and return the raw record bytes (log record header +
 *	common + body) in *datump.  The caller must free datump->data.
 *
 *	If the position falls at the end of the current file (an empty
 *	record or past the end), move to the next log file when the
 *	current one is complete, otherwise return VAN_NOTFOUND.
 */
static int _van_logc_getrec(VAN_LOG_CURSOR *lgc, VAN_LSN *lsn,
    VAN_DATUM *datump) {
	int ret;
	uint32 recsize;
	char hdrbuf[VAN_LOGREC_HDRSIZE];
	VAN_LOGREC_HEADER rechdr;
	VAN_LSN next_lsn;
	char *buf;

	VAN_ASSERT(lgc->logfile != NULL);
	ret = 0;

	while (1) {
		/*
		 * Past the end of the current file: move to the next
		 * file if the current one is complete, otherwise there
		 * is nothing more to read.
		 */
		if (lsn->offset + VAN_LOGREC_HDRSIZE > lgc->filesize) {
			if (!lgc->completed)
				return (VAN_NOTFOUND);
			next_lsn.file = lsn->file + 1;
			next_lsn.offset = VAN_LOGFILE_HDRSIZE;
			ret = _van_logc_openfile(lgc, &next_lsn);
			if (ret != 0)
				return (ret);
			*lsn = next_lsn;
			continue;
		}

		ret = _van_iofile_read(lgc->logfile, lsn->offset,
		    (uint8 *)hdrbuf, VAN_LOGREC_HDRSIZE);
		if (ret != 0)
			return (ret);
		memcpy(&rechdr, hdrbuf, sizeof(rechdr));

		/*
		 * An empty record (orig_len == 0) marks the end of the
		 * records in this file.  A complete file always ends
		 * with one or two empty records, so continue with the
		 * next file; an incomplete file (the one being written)
		 * has nothing more.
		 */
		if (rechdr.orig_len == 0) {
			if (!lgc->completed)
				return (VAN_NOTFOUND);
			next_lsn.file = lsn->file + 1;
			next_lsn.offset = VAN_LOGFILE_HDRSIZE;
			ret = _van_logc_openfile(lgc, &next_lsn);
			if (ret != 0)
				return (ret);
			*lsn = next_lsn;
			continue;
		}

		/*
		 * A real record: header + rec_len body bytes.
		 */
		recsize = VAN_LOGREC_HDRSIZE + rechdr.rec_len;
		ret = _van_malloc(NULL, recsize, &buf);
		if (ret != 0)
			return (ret);
		ret = _van_iofile_read(lgc->logfile, lsn->offset,
		    (uint8 *)buf, recsize);
		if (ret != 0) {
			_van_free(NULL, buf);
			return (ret);
		}

		datump->data = buf;
		datump->size = recsize;
		lgc->lastlsn = *lsn;
		lgc->rechdr = rechdr;

		return (0);
	}
}

/*
 * _van_logrec_cget --
 *	Get a record from the log using the cursor.
 *
 *	VAN_FIRST:	position at the first record of the log.
 *	VAN_NEXT:	advance to the record after the last one returned.
 *	VAN_CURRENT:	return the last record again.
 *	VAN_LAST:	position at the last record of the log.
 *	VAN_PREV:	go back to the previous record.
 *	VAN_SET:	position at the given lsn.
 *
 *	On success, *lsn is set to the record's LSN and *datum contains
 *	the raw record bytes (log record header + common + body); the
 *	caller must free datum->data.  VAN_NOTFOUND is returned when
 *	the requested position does not exist.
 */
int _van_logrec_cget(VAN_LOG_CURSOR *lgc, VAN_LSN *lsn,
    VAN_DATUM *datum, flags_t flags) {
	int ret;
	VAN_LOG *logp;
	VAN_LSN curlsn;
	uint32 nextoff;

	ret = 0;
	logp = lgc->logp;
	memset(&curlsn, 0, sizeof(curlsn));

	switch (flags) {
		case VAN_FIRST:
			curlsn.file = logp->firstfnum;
			curlsn.offset = VAN_LOGFILE_HDRSIZE;
			ret = _van_logc_openfile(lgc, &curlsn);
			CHK_ERR(ret);
			ret = _van_logc_getrec(lgc, &curlsn, datum);
			break;

		case VAN_NEXT:
			if (lgc->logfile == NULL) {
				/* Not positioned yet: start from the
				 * beginning of the log. */
				curlsn.file = logp->firstfnum;
				curlsn.offset = VAN_LOGFILE_HDRSIZE;
				ret = _van_logc_openfile(lgc, &curlsn);
				CHK_ERR(ret);
				ret = _van_logc_getrec(lgc, &curlsn, datum);
				break;
			}
			/*
			 * The next record starts right after the last
			 * one returned: lastlsn.offset + header + body.
			 */
			nextoff = lgc->lastlsn.offset + VAN_LOGREC_HDRSIZE +
			    lgc->rechdr.rec_len;
			curlsn.file = lgc->lastlsn.file;
			curlsn.offset = nextoff;
			ret = _van_logc_getrec(lgc, &curlsn, datum);
			break;

		case VAN_CURRENT:
			if (lgc->logfile == NULL) {
				ret = VAN_NOTFOUND;
				break;
			}
			curlsn = lgc->lastlsn;
			ret = _van_logc_getrec(lgc, &curlsn, datum);
			break;

		case VAN_LAST:
			curlsn = logp->last_lsn;
			if (!IS_VALID_LSNP(&curlsn)) {
				ret = VAN_NOTFOUND;
				break;
			}
			ret = _van_logc_openfile(lgc, &curlsn);
			CHK_ERR(ret);
			ret = _van_logc_getrec(lgc, &curlsn, datum);
			break;

		case VAN_PREV:
			if (lgc->logfile == NULL) {
				/* Not positioned yet: start from the
				 * last record. */
				curlsn = logp->last_lsn;
				if (!IS_VALID_LSNP(&curlsn)) {
					ret = VAN_NOTFOUND;
					break;
				}
				ret = _van_logc_openfile(lgc, &curlsn);
				CHK_ERR(ret);
				ret = _van_logc_getrec(lgc, &curlsn, datum);
				break;
			}
			/*
			 * The record header's "prev" field links to the
			 * previous record in the same file.
			 */
			if (lgc->rechdr.prev != 0) {
				curlsn.file = lgc->lastlsn.file;
				curlsn.offset = lgc->rechdr.prev;
			} else {
				/* First record of this file: move to the
				 * previous file's last record. */
				if (lgc->lastlsn.file <= logp->firstfnum) {
					ret = VAN_NOTFOUND;
					break;
				}
				curlsn.file = lgc->lastlsn.file - 1;
				curlsn.offset = VAN_LOGFILE_HDRSIZE;
				ret = _van_logc_openfile(lgc, &curlsn);
				if (ret != 0)
					break;
				if (!lgc->completed || lgc->tailhdr.prev == 0) {
					ret = VAN_NOTFOUND;
					break;
				}
				curlsn.offset = lgc->tailhdr.prev;
			}
			ret = _van_logc_getrec(lgc, &curlsn, datum);
			break;

		case VAN_SET:
			if (lsn == NULL || !IS_VALID_LSNP(lsn)) {
				ret = VAN_NOTFOUND;
				break;
			}
			curlsn = *lsn;
			ret = _van_logc_openfile(lgc, &curlsn);
			CHK_ERR(ret);
			ret = _van_logc_getrec(lgc, &curlsn, datum);
			break;

		default:
			ret = _van_error_print(
			    "_van_logrec_cget: unsupported flags");
			break;
	}

	if (ret == 0)
		*lsn = curlsn;

err:
	return (ret);
}
