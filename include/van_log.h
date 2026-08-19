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
#ifndef _VAN_LOG_H
#define _VAN_LOG_H

#if defined(__cplusplus)
extern "C" {
#endif

#define VAN_LOG_VERSION	 1
#define VAN_LOG_MAGIC	0x1
#define VAN_DEF_LOGSIZE	(10 * 1048576)

#define VAN_LFPFX	"vanlog."
#define VAN_LFNAME	"vanlog.%010d"
#define	VAN_ISLOGFILE(name)	\
	(strncmp(name, VAN_LFPFX, sizeof(VAN_LFPFX) - 1) == 0)

#define INIT_LSN(lsn)	do { \
	(lsn)->file = 0; \
	(lsn)->offset = 0; \
} while(0)

#define IS_VALID_LSN(lsn)  (lsn->file != 0 && lsn->offset != 0)
#define VAN_LOGFILE_SIZE(logp)	(logp->header.logsize)
#define VAN_LOGREC_HDRSIZE (sizeof(VAN_LOGREC_HEADER))
#define VAN_LOGREC_COMSIZE (sizeof(VAN_LOGREC_COMMON))
#define VAN_LOGFILE_LEFT(logp) (logp->header.logsize - logp->cur_lsn.offset)
#define VAN_LOGREC_ISFIRST(lsn)	(lsn.offset == VAN_LOGREC_HDRSIZE)
#define VAN_LOGFILE_HDRSIZE (sizeof(VAN_LOGFILE_HEADER))

#define LOG_BIGGER(lsn1, lsn2) ((lsn1)->file > (lsn2)->file || \
    ((lsn1)->file == (lsn2)->file && (lsn1)->offset > (lsn2)->offset))
#define LOG_SMALLER(lsn1, lsn2) ((lsn1)->file < (lsn2)->file || \
    ((lsn1)->file == (lsn2)->file && (lsn1)->offset < (lsn2)->offset))
#define LOG_EQUAL(lsn1, lsn2) ((lsn1)->file == (lsn2)->file && \
    ((lsn1)->offset == (lsn2)->offset))

/*
 * If one/two empty record, means we finish current file,
 * and move to next file. If no empty record, we stay
 * in current file.
 */
#define VAN_LOGPUT_INVALID 0
#define VAN_LOGPUT_NO_EMPTY 1
#define VAN_LOGPUT_ONE_EMPTY 2
#define VAN_LOGPUT_TWO_EMPTY 3

struct _van_logfile_header {
	uint32 version;
	uint32 logsize;
	uint32 magic;
	uint32 unused;
};

struct _van_logrec_header {
	uint32 prev;
	uint32 hdrlen;
	/* TODO: Add checksum and crypto bytes ? */
	uint32 rec_len;
	uint32 orig_len;
};

struct _van_logrec_common {
	uint32	type;
	uint32	txnid;
	VAN_LSN	prev_lsn;
};

typedef enum {
	LOGREC_Done,
	LOGREC_TABLE,	/* The tableid, uint32, lu */
	LOGREC_DATUM,	/* The data, VAN_DATUM, s/p */
	LOGREC_TXN,	/* The txnid, uint32, lu */
	LOGREC_ARG,	/* The ARG, uint32, lu */
	LOGREC_LSN,	/* The lsn, VAN_LSN, lu */
	LOGREC_TIME,	/* The time, uint32, lu */
	LOGREC_OP	/* The OP, uint32, lu */
} van_logrec_type_t;

struct _van_log_recspec {
	van_logrec_type_t type;
	uint32 offset;
	const char *name;
	const char fmt[4];
};

struct _van_log {
	VAN_LSN	cur_lsn;
	VAN_LSN last_lsn;
	uint32	prev_offset;
	pthread_mutex_t	mutex;
	uint32	firstfnum;

	VAN_STORE *store;
	VAN_IOFILE *cur_logfile;

	/* Header */
	VAN_LOGFILE_HEADER header;
	/* Configurations */
	VAN_STRING_ARRAY *lognames;
};

/* Used for log cursor */
struct _van_logfile_info {
	VAN_LOG *logp;
	VAN_IOFILE *logfile;
	int completed;
	uint32 filesize;
	VAN_LSN last_lsn;
	VAN_LOGFILE_HEADER filehdr;
	VAN_LOGREC_HEADER tailhdr;
};

/*
 * Log cursor should only supports
 * VAN_NEXT, VAN_PREV, VAN_SET, VAN_FIRST,
 * VAN_LAST.
 */
struct _van_log_cursor {
	int completed;
	VAN_LOG *logp;
	VAN_IOFILE *logfile;		/* The logfile */
	VAN_LOGFILE_HEADER filehdr;	/* File header */
	VAN_LSN	lastlsn;		/* Last lsn */
	VAN_LOGREC_HEADER rechdr;	/* Last lsn's record header */
	VAN_LOGREC_HEADER tailhdr;	/* The file's last record header */
};

#ifdef __cplusplus
}
#endif

#endif
