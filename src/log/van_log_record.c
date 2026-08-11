#include "van_int.h"
#include "van.h"

#include "van_log.h"

int _van_put_logrecord(VAN_STORE *store, VAN_TABLE *table, VAN_TXN *txnp,
    VAN_LSN *ret_lsnp, uint32 flags, uint32 rectype, uint32 size,
    VAN_LOG_RECSPEC *spec, ...) {
	va_list argp;
	int ret;
	char *buf, *wp;
	uint32 bufsize;
	int type, locked;
	VAN_LSN rlsn, prelsn;
	uint32 txnid;
	VAN_LOG_RECSPEC *sp;
	VAN_DATUM *datav;
	uint32 ui32v;
	VAN_LSN lsnv;
	VAN_LOG *logp;

	buf = NULL;
	ret = 0;
	bufsize = 0;
	type = VAN_LOGPUT_INVALID;
	memset(&rlsn, 0, sizeof(rlsn));
	memset(&prelsn, 0, sizeof(prelsn));

	datav = NULL;
	ui32v = 0;
	locked = 0;
	INIT_LSN(&lsnv);
	logp = store->log_handle;

	va_start(argp, spec);
	VAN_MUTEX_LOCK(logp, mutex, ret);
	if (ret != 0)
		goto err;
	locked = 1;
	ret = _van_logrec_init(store, size, &buf, &bufsize, &type, &rlsn);
	CHK_ERR(ret);

	wp = buf + VAN_LOGREC_HDRSIZE;
	
	/* 
	 * Write the common things: rectype, txnid, txn->prev_lsn.
	 */
	memcpy(wp, &rectype, sizeof(uint32));
	wp += sizeof(uint32);
	txnid = (txnp == NULL ? 0 : txnp->txnid);
	memcpy(wp, &txnid, sizeof(uint32));
	wp += sizeof(uint32);
	if (txnp != NULL)
		prelsn = txnp->last_lsn;
	memcpy(wp, &prelsn, sizeof(VAN_LSN));
	wp += sizeof(VAN_LSN);

	for (sp = spec; sp->type != LOGREC_Done; sp++) {
		switch (sp->type) {
			case LOGREC_TABLE:
				ui32v = 0;
				if (table != NULL)
					ui32v = table->tid;
				memcpy(wp, &ui32v, sizeof(ui32v));
				wp += sizeof(ui32v);
				break;
			case LOGREC_DATUM:
				datav = va_arg(argp, VAN_DATUM *);
				ui32v = datav->size;
				memcpy(wp, &ui32v, sizeof(ui32v));
				wp += sizeof(ui32v);
				if (ui32v > 0) {
					memcpy(wp, datav->data, ui32v);
					wp += ui32v;
				}
				break;
			case LOGREC_TXN:
			case LOGREC_ARG:
			case LOGREC_TIME:
			case LOGREC_OP:
				ui32v = va_arg(argp, uint32);
				memcpy(wp, &ui32v, sizeof(ui32v));
				wp += sizeof(ui32v);
				break;
			case LOGREC_LSN:
				lsnv = va_arg(argp, VAN_LSN);
				memcpy(wp, &lsnv, sizeof(lsnv));
				wp += sizeof(lsnv);
				break;
			default:
				ret = _van_error_path("Invalid sp->type!");

		}
	}

	ret = _van_logrec_write(store, buf, bufsize, type);
	CHK_ERR(ret);

	if (txnp != NULL)
		txnp->last_lsn = rlsn;
	*ret_lsnp = rlsn;
err:
	if (locked) {
		VAN_MUTEX_UNLOCK(logp, mutex, ret);
		locked = 0;
	}
	va_end(argp);
	if (buf != NULL)
		_van_free(NULL, buf);
	return (ret);
}


int _van_read_logrecord(VAN_STORE *store, VAN_TABLE **tablepp, void *td,
    void* data, VAN_LOG_RECSPEC *spec, uint32 size, void **argp) {
	int ret;

err:
	return (ret);
}

int _van_print_logrecord(VAN_STORE *store, VAN_DATUM *datap, VAN_LSN *lsnp, 
    const char *str, VAN_LOG_RECSPEC *spec, void *info) {
	int ret;


	return (ret);
}

int _van_logrec_init(VAN_STORE *store, uint32 orig_len,
    char **bufp, uint32 *bufsizep, int *typep, VAN_LSN *lsnp) {
	uint32 rec_len, tlen, bufsize, left, hdrsz;
	int ret, type;
	char *buf;
	VAN_LOG *logp;
	VAN_LSN lsn;
	VAN_LOGREC_HEADER hdr;
	uint32 woff;
	
	logp = store->log_handle;
	type = VAN_LOGPUT_INVALID;
	bufsize = 0;
	buf = NULL;
	ret = 0;
	memset(&hdr, 0, sizeof(hdr));

	rec_len = VAN_ALIGN(orig_len, 8);
	tlen = VAN_LOGREC_HDRSIZE + rec_len; 
	left = VAN_LOGFILE_LEFT(logp);
	hdrsz = VAN_LOGREC_HDRSIZE;
	memset(&lsn, 0, sizeof(lsn));

	/* Whether we support pure empty record from upper functions ? */
	VAN_ASSERT(orig_len > 0);
	VAN_ASSERT(left > (3 * hdrsz));
	hdr.hdrlen = hdrsz;

	if (tlen + hdrsz > left) {
		/* 
		 * write two empty header, and some empty chars.
		 * This means no space to put the 
		 * record(and an empty record).
		 */
		ret = _van_malloc(NULL, left, &buf);
		CHK_ERR(ret);
		memset(buf, 0, left);

		hdr.prev = logp->prev_offset;
		hdr.rec_len = left - 2 * hdrsz;
		hdr.orig_len = 0;
		memcpy(buf, &hdr, sizeof(hdr));

		woff = left - hdrsz;
		/* We link two empty records to the 
		 * last non-empty records instead of
		 * link the last to an empty record.
		 * hdr.prev = logp->cur_lsn.offset;
		 */
		hdr.rec_len = 0;
		memcpy(buf + woff, &hdr, sizeof(hdr));
		type = VAN_LOGPUT_TWO_EMPTY;

		/* Write the empty headers now */
		ret = _van_logrec_write(store, buf, left, type);
		_van_free(NULL, buf); /* Must do */
		CHK_ERR(ret);

		return _van_logrec_init(store, orig_len,
		    bufp, bufsizep, typep, lsnp);

	} else if (tlen + 3 * hdrsz >= left) {
		/* 
		 * This means after we putting the record,
		 * there is no space to put other records in 
		 * this file.
		 */
		type = VAN_LOGPUT_ONE_EMPTY;
		bufsize = left;
		ret = _van_malloc(NULL, bufsize, &buf);
		CHK_ERR(ret);
		memset(buf, 0, bufsize);

		/* The record header */
		lsn = logp->cur_lsn;
		hdr.orig_len = orig_len;
		hdr.rec_len = left - 2 * hdrsz;
		hdr.prev = logp->prev_offset;
		memcpy(buf, &hdr, sizeof(hdr));

		/* The empty record */
		hdr.orig_len = 0;
		hdr.rec_len = 0;
		hdr.prev = logp->cur_lsn.offset;
		memcpy(buf + (bufsize - hdrsz), &hdr, hdrsz);

		*bufp = buf;
		*bufsizep = bufsize;
		*lsnp = lsn;
		*typep = type;

	} else {
		/* 
		 * This means there are enough space to put
		 * the record, and after putting this record,
		 * the left space may be put for other
		 * records in this file.
		 */
		type = VAN_LOGPUT_NO_EMPTY;
		bufsize = tlen;
		ret = _van_malloc(NULL, bufsize, &buf);
		CHK_ERR(ret);
		memset(buf, 0, bufsize);

		/* The record header */
		lsn = logp->cur_lsn;
		hdr.orig_len = orig_len;
		hdr.rec_len = rec_len;
		hdr.prev = logp->prev_offset;
		memcpy(buf, &hdr, sizeof(hdr));

		*bufp = buf;
		*bufsizep = bufsize;
		*lsnp = lsn;
		*typep = type;
	}

err:
	return (ret);
}

/* 
 * The caller should free the 'buf'.
 */
int _van_logrec_write(VAN_STORE *store, const char *buf,
    uint32 bufsize, int type) {
	int ret;
	VAN_LSN lsn;
	VAN_LOG *logp;

	logp = store->log_handle;
	ret = 0;
	lsn = logp->cur_lsn;

	VAN_ASSERT(type == VAN_LOGPUT_NO_EMPTY ||
	    type == VAN_LOGPUT_ONE_EMPTY ||
	    type == VAN_LOGPUT_TWO_EMPTY);

	switch (type) {
		case VAN_LOGPUT_NO_EMPTY: 
			ret = _van_iofile_write(logp->cur_logfile, lsn.offset, (uint8 *)buf, bufsize);
			if (ret != 0)
				break;
			logp->prev_offset = lsn.offset;
			logp->cur_lsn.offset += bufsize;
			break;
		case VAN_LOGPUT_ONE_EMPTY:
		case VAN_LOGPUT_TWO_EMPTY:
			ret = _van_iofile_write(logp->cur_logfile, lsn.offset, (uint8 *)buf, bufsize); 
			if (ret != 0)
				break;
			ret = _van_log_newfile(logp);
			break;
		default: 
			ret = _van_error_path(
			    "_van_logrec_write: Invalid type !");
	}

	return (ret);
}
