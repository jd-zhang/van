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
#include "test_common.h"

#define LOG_T1_PUTS	100
#define LOG_T1_DELS	40

/*
 * log_t1_1 --
 *
 * Do LOG_T1_PUTS puts and LOG_T1_DELS dels through the table
 * interface (each of them writes a WAL log record), then reopen the
 * store and use a log cursor to read the log from the beginning to
 * the end, counting the PUT and DEL records.
 *
 * Step 1 only verifies the record counts; key/data matching will be
 * added in later test steps.
 */
int log_t1_1() {
	VAN_TABLE *table;
	VAN_STORE *store;
	VAN_LOG_CURSOR *lgc;
	VAN_LOG *logp;
	VAN_DATUM key, value, rec;
	VAN_LSN lsn;
	uint32 type;
	int i, ret, putcnt, delcnt, othercnt, odlistcnt;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");

	table = NULL;
	store = NULL;
	lgc = NULL;
	putcnt = 0;
	delcnt = 0;
	othercnt = 0;
	odlistcnt = 0;

	fprintf(stderr, "============ 1 open table and do puts/dels =========\n");
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, GIGABYTE);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");

	for (i = 0; i < LOG_T1_PUTS; i++) {
		char kbuf[64], vbuf[64];

		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = kbuf;
		key.size = (size_t)sprintf(kbuf, "key-%d", i);
		value.data = vbuf;
		value.size = (size_t)sprintf(vbuf, "value-%d-abcdefghij", i);
		ret = table->put(table, &key, &value, 0);
		CHK_RET(ret, "table->put");
	}

	for (i = 0; i < LOG_T1_DELS; i++) {
		char kbuf[64];

		memset(&key, 0, sizeof(key));
		key.data = kbuf;
		key.size = (size_t)sprintf(kbuf, "key-%d", i);
		ret = table->del(table, &key, 0);
		CHK_RET(ret, "table->del");
	}

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	table = NULL;
	ret = store->close(store);
	CHK_RET(ret, "store->close");
	store = NULL;

	fprintf(stderr, "============ 2 reopen and read log with cursor =========\n");
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	logp = store->log_handle;

	ret = logp->cursor(logp, &lgc);
	CHK_RET(ret, "logp->cursor");

	memset(&rec, 0, sizeof(rec));
	ret = lgc->get(lgc, &lsn, &rec, VAN_FIRST);
	while (ret == 0) {
		/*
		 * The record bytes: log record header, then the common
		 * part whose first field is the record type.
		 */
		type = *(uint32 *)((char *)rec.data + VAN_LOGREC_HDRSIZE);
		switch (type) {
			case VAN_INMEMTREE_PUT:
				putcnt++;
				break;
			case VAN_INMEMTREE_DEL:
				delcnt++;
				break;
			case VAN_ODLIST_UPDATE:
				odlistcnt++;
				break;
			default:
				othercnt++;
				break;
		}
		_van_free(NULL, rec.data);
		memset(&rec, 0, sizeof(rec));
		ret = lgc->get(lgc, &lsn, &rec, VAN_NEXT);
	}
	if (ret != VAN_NOTFOUND)
		CHK_RET(ret, "lgc->get");
	if (rec.data != NULL)
		_van_free(NULL, rec.data);

	fprintf(stderr, "log_t1_1: putcnt=%d delcnt=%d othercnt=%d "
	    "odlistcnt=%d\n", putcnt, delcnt, othercnt, odlistcnt);
	TEST_ASSERT(putcnt == LOG_T1_PUTS);
	TEST_ASSERT(delcnt == LOG_T1_DELS);
	TEST_ASSERT(othercnt == 0);
	TEST_ASSERT(odlistcnt >= 1);

	ret = lgc->close(lgc);
	CHK_RET(ret, "lgc->close");
	lgc = NULL;
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	fprintf(stderr, "============ 3 log_t1_1 passed =========\n");

	return (0);
}

#define LOG_T1_2_PUTS	100
#define LOG_T1_2_DELS	40

/*
 * log_t1_2 --
 *
 * Like log_t1_1 (LOG_T1_2_PUTS puts then LOG_T1_2_DELS dels), but
 * this time parse the record contents: for a PUT record verify the
 * key/value pair was stored, for a DEL record verify the key.
 *
 * Record layout returned by the log cursor:
 *	[VAN_LOGREC_HEADER][VAN_LOGREC_COMMON][body]
 *	common:	type(tabid? no: type) txnid prev_lsn
 *	body(put):	tabid, key, data
 *	body(del):	tabid, key
 * Each datum is a uint32 size followed by the bytes.
 */
int log_t1_2() {
	VAN_TABLE *table;
	VAN_STORE *store;
	VAN_LOG_CURSOR *lgc;
	VAN_LOG *logp;
	VAN_DATUM key, value, rec;
	VAN_LSN lsn;
	char kbuf[64], vbuf[64];
	char *p, *kptr, *dptr;
	uint32 type, tabid, ksz, dsz;
	uint32 tid_expected;
	int i, k, ret;
	int putcnt, delcnt, othercnt, odlistcnt;
	int put_seen[LOG_T1_2_PUTS];
	int del_seen[LOG_T1_2_DELS];
	int recno;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");

	table = NULL;
	store = NULL;
	lgc = NULL;
	putcnt = 0;
	delcnt = 0;
	othercnt = 0;
	odlistcnt = 0;
	recno = 0;
	memset(put_seen, 0, sizeof(put_seen));
	memset(del_seen, 0, sizeof(del_seen));

	fprintf(stderr, "============ 1 open table and do puts/dels =========\n");
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, GIGABYTE);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");
	tid_expected = table->tid;

	for (i = 0; i < LOG_T1_2_PUTS; i++) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = kbuf;
		key.size = (size_t)sprintf(kbuf, "key-%d", i);
		value.data = vbuf;
		value.size = (size_t)sprintf(vbuf, "value-%d-abcdefghij", i);
		ret = table->put(table, &key, &value, 0);
		CHK_RET(ret, "table->put");
	}

	for (i = 0; i < LOG_T1_2_DELS; i++) {
		memset(&key, 0, sizeof(key));
		key.data = kbuf;
		key.size = (size_t)sprintf(kbuf, "key-%d", i);
		ret = table->del(table, &key, 0);
		CHK_RET(ret, "table->del");
	}

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	table = NULL;
	ret = store->close(store);
	CHK_RET(ret, "store->close");
	store = NULL;

	fprintf(stderr, "============ 2 reopen and verify record contents =========\n");
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	logp = store->log_handle;

	ret = logp->cursor(logp, &lgc);
	CHK_RET(ret, "logp->cursor");

	memset(&rec, 0, sizeof(rec));
	ret = lgc->get(lgc, &lsn, &rec, VAN_FIRST);
	while (ret == 0) {
		/* Common: type at [HDRSIZE], txnid, prev_lsn. */
		memcpy(&type, (char *)rec.data + VAN_LOGREC_HDRSIZE,
		    sizeof(type));
		/* Body: tabid, then the datums. */
		p = (char *)rec.data + VAN_LOGREC_HDRSIZE +
		    VAN_LOGREC_COMSIZE;
		memcpy(&tabid, p, sizeof(tabid));
		p += sizeof(tabid);
		memcpy(&ksz, p, sizeof(ksz));
		p += sizeof(ksz);
		kptr = p;
		p += ksz;

		TEST_ASSERT(tabid == tid_expected);

		switch (type) {
			case VAN_INMEMTREE_PUT: {
				memcpy(&dsz, p, sizeof(dsz));
				p += sizeof(dsz);
				dptr = p;

				/* Find the key index and verify the
				 * value of this key. */
				for (k = 0; k < LOG_T1_2_PUTS; k++) {
					sprintf(kbuf, "key-%d", k);
					if (ksz == strlen(kbuf) &&
					    memcmp(kptr, kbuf, ksz) == 0)
						break;
				}
				TEST_ASSERT(k < LOG_T1_2_PUTS);
				TEST_ASSERT(put_seen[k] == 0);
				put_seen[k] = 1;
				sprintf(vbuf, "value-%d-abcdefghij", k);
				TEST_ASSERT(dsz == strlen(vbuf));
				TEST_ASSERT(memcmp(dptr, vbuf, dsz) == 0);
				fprintf(stderr, "  rec[%d] PUT key=%.*s "
				    "value=%.*s\n", recno, (int)ksz, kptr,
				    (int)dsz, dptr);
				putcnt++;
				break;
			}
			case VAN_INMEMTREE_DEL: {
				/* DEL only has the key. */
				for (k = 0; k < LOG_T1_2_DELS; k++) {
					sprintf(kbuf, "key-%d", k);
					if (ksz == strlen(kbuf) &&
					    memcmp(kptr, kbuf, ksz) == 0)
						break;
				}
				TEST_ASSERT(k < LOG_T1_2_DELS);
				TEST_ASSERT(del_seen[k] == 0);
				del_seen[k] = 1;
				fprintf(stderr, "  rec[%d] DEL key=%.*s\n",
				    recno, (int)ksz, kptr);
				delcnt++;
				break;
			}
			case VAN_ODLIST_UPDATE:
				/*
				 * Written when a dump or a merge changes
				 * the odlist.  Not a data record, so it
				 * is accounted for on its own.
				 */
				odlistcnt++;
				break;
			default:
				fprintf(stderr, "  rec[%d] UNKNOWN type=%u\n",
				    recno, type);
				othercnt++;
				break;
			}

		recno++;
		_van_free(NULL, rec.data);
		memset(&rec, 0, sizeof(rec));
		ret = lgc->get(lgc, &lsn, &rec, VAN_NEXT);
	}
	if (ret != VAN_NOTFOUND)
		CHK_RET(ret, "lgc->get");
	if (rec.data != NULL)
		_van_free(NULL, rec.data);

	fprintf(stderr, "log_t1_2: putcnt=%d delcnt=%d othercnt=%d\n",
	    putcnt, delcnt, othercnt);
	TEST_ASSERT(putcnt == LOG_T1_2_PUTS);
	TEST_ASSERT(delcnt == LOG_T1_2_DELS);
	TEST_ASSERT(othercnt == 0);
	for (k = 0; k < LOG_T1_2_PUTS; k++)
		TEST_ASSERT(put_seen[k] == 1);
	for (k = 0; k < LOG_T1_2_DELS; k++)
		TEST_ASSERT(del_seen[k] == 1);

	ret = lgc->close(lgc);
	CHK_RET(ret, "lgc->close");
	lgc = NULL;
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	fprintf(stderr, "============ 3 log_t1_2 passed =========\n");

	return (0);
}

#define LOG_T1_3_PUTS	20
#define LOG_T1_3_NNEW	2
#define LOG_T1_3_NDEL	3

/*
 * log_t1_3 --
 *
 * Write a VAN_ODLIST_UPDATE record and read it back through the log
 * cursor, checking the tabid and both file name lists.
 *
 * In production that record is written by the merge thread, which only
 * fires once enough ondisk trees exist, so driving a real merge here
 * would make the test slow and dependent on timing.  The record is
 * written by _van_odlist_update_logrecord() - the same function merge
 * calls - so the test drives that directly against a real store and
 * table, then verifies what landed in the log.
 *
 * Record layout returned by the log cursor:
 *	[VAN_LOGREC_HEADER][VAN_LOGREC_COMMON][body]
 *	common:	type txnid prev_lsn
 *	body:	tabid, nfiles, ndels,
 *		new blob (uint32 len + len bytes),
 *		del blob (uint32 len + len bytes)
 * Every name inside a blob is a uint32 len followed by len bytes, and
 * len includes the trailing '\0', so the bytes are a C string.
 */
int log_t1_3() {
	VAN_TABLE *table;
	VAN_STORE *store;
	VAN_LOG_CURSOR *lgc;
	VAN_LOG *logp;
	VAN_STRING_ARRAY *new_files, *del_files;
	VAN_DATUM key, value, rec;
	VAN_LSN lsn;
	char kbuf[64], vbuf[64];
	char *p, *q;
	size_t i, left;
	uint32 type, tabid, nfiles, ndels, sz, nlen;
	uint32 tid_expected;
	int ret, found;
	static const char *new_names[LOG_T1_3_NNEW] = {
	    "test1.ondisk.1700000000.0",
	    "test1.ondisk.1700000001.0"
	};
	static const char *del_names[LOG_T1_3_NDEL] = {
	    "test1.ondisk.1699999999.0",
	    "test1.ondisk.1699999998.0",
	    "test1.ondisk.1699999997.0"
	};

	system("rm -fr test1*");
	system("rm -fr vanlog.*");

	table = NULL;
	store = NULL;
	lgc = NULL;
	new_files = NULL;
	del_files = NULL;
	found = 0;

	fprintf(stderr, "============ 1 open table and put %d records =========\n",
	    LOG_T1_3_PUTS);
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	ret = van_table_create(store, &table);
	CHK_RET(ret, "van_table_create");
	table->set_inmem_threshold(table, GIGABYTE);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "table->open");
	tid_expected = table->tid;

	for (i = 0; i < LOG_T1_3_PUTS; i++) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = kbuf;
		key.size = (size_t)sprintf(kbuf, "key-%lu", (unsigned long)i);
		value.data = vbuf;
		value.size = (size_t)sprintf(vbuf, "value-%lu-abcdefghij",
		    (unsigned long)i);
		ret = table->put(table, &key, &value, 0);
		CHK_RET(ret, "table->put");
	}

	fprintf(stderr, "============ 2 write the odlist update record =========\n");
	ret = _van_strarray_alloc(&new_files, 0);
	CHK_RET(ret, "_van_strarray_alloc(new_files)");
	ret = _van_strarray_alloc(&del_files, 0);
	CHK_RET(ret, "_van_strarray_alloc(del_files)");
	for (i = 0; i < LOG_T1_3_NNEW; i++) {
		ret = _van_strarray_add(new_files, new_names[i]);
		CHK_RET(ret, "_van_strarray_add(new_files)");
	}
	for (i = 0; i < LOG_T1_3_NDEL; i++) {
		ret = _van_strarray_add(del_files, del_names[i]);
		CHK_RET(ret, "_van_strarray_add(del_files)");
	}

	ret = _van_odlist_update_logrecord(store, table, new_files,
	    del_files);
	CHK_RET(ret, "_van_odlist_update_logrecord");

	(void)_van_strarray_free(new_files);
	new_files = NULL;
	(void)_van_strarray_free(del_files);
	del_files = NULL;

	ret = table->close(table, 0);
	CHK_RET(ret, "table->close");
	table = NULL;
	ret = store->close(store);
	CHK_RET(ret, "store->close");
	store = NULL;

	fprintf(stderr, "============ 3 reopen and check the record =========\n");
	ret = van_store_create(&store);
	CHK_RET(ret, "van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "store->open");
	logp = store->log_handle;

	ret = logp->cursor(logp, &lgc);
	CHK_RET(ret, "logp->cursor");

	memset(&rec, 0, sizeof(rec));
	ret = lgc->get(lgc, &lsn, &rec, VAN_FIRST);
	while (ret == 0) {
		/* Common: type at [HDRSIZE]. */
		memcpy(&type, (char *)rec.data + VAN_LOGREC_HDRSIZE,
		    sizeof(type));
		if (type != VAN_ODLIST_UPDATE) {
			_van_free(NULL, rec.data);
			memset(&rec, 0, sizeof(rec));
			ret = lgc->get(lgc, &lsn, &rec, VAN_NEXT);
			continue;
		}
		p = (char *)rec.data + VAN_LOGREC_HDRSIZE +
		    VAN_LOGREC_COMSIZE;
		memcpy(&tabid, p, sizeof(tabid));
		p += sizeof(tabid);
		memcpy(&nfiles, p, sizeof(nfiles));
		p += sizeof(nfiles);
		memcpy(&ndels, p, sizeof(ndels));
		p += sizeof(ndels);

		/*
		 * The table close at the end of this case dumps what is
		 * left in the in-memory tree, and that dump writes an
		 * ODLIST_UPDATE record of its own.  Only the record this
		 * case wrote carries LOG_T1_3_NNEW / LOG_T1_3_NDEL, so
		 * skip anything else instead of asserting on it.
		 */
		if (nfiles != LOG_T1_3_NNEW || ndels != LOG_T1_3_NDEL) {
			_van_free(NULL, rec.data);
			memset(&rec, 0, sizeof(rec));
			ret = lgc->get(lgc, &lsn, &rec, VAN_NEXT);
			continue;
		}
		found++;

		TEST_ASSERT2(tabid == tid_expected,
		    "tabid %u, expected %u", tabid, tid_expected);
		TEST_ASSERT2(nfiles == LOG_T1_3_NNEW,
		    "nfiles %u, expected %d", nfiles, LOG_T1_3_NNEW);
		TEST_ASSERT2(ndels == LOG_T1_3_NDEL,
		    "ndels %u, expected %d", ndels, LOG_T1_3_NDEL);

		/* The new file list. */
		memcpy(&sz, p, sizeof(sz));
		p += sizeof(sz);
		q = p;
		left = sz;
		for (i = 0; i < nfiles; i++) {
			TEST_ASSERT2(left >= sizeof(nlen),
			    "new list truncated at %lu", (unsigned long)i);
			memcpy(&nlen, q, sizeof(nlen));
			q += sizeof(nlen);
			left -= sizeof(nlen);
			TEST_ASSERT2(left >= nlen,
			    "new name %lu truncated", (unsigned long)i);
			TEST_ASSERT2(strcmp(q, new_names[i]) == 0,
			    "new_files[%lu] is %s, expected %s",
			    (unsigned long)i, q, new_names[i]);
			q += nlen;
			left -= nlen;
		}
		TEST_ASSERT2(left == 0, "%lu trailing bytes in the new list",
		    (unsigned long)left);
		p += sz;

		/* The deleted file list. */
		memcpy(&sz, p, sizeof(sz));
		p += sizeof(sz);
		q = p;
		left = sz;
		for (i = 0; i < ndels; i++) {
			TEST_ASSERT2(left >= sizeof(nlen),
			    "del list truncated at %lu", (unsigned long)i);
			memcpy(&nlen, q, sizeof(nlen));
			q += sizeof(nlen);
			left -= sizeof(nlen);
			TEST_ASSERT2(left >= nlen,
			    "del name %lu truncated", (unsigned long)i);
			TEST_ASSERT2(strcmp(q, del_names[i]) == 0,
			    "del_files[%lu] is %s, expected %s",
			    (unsigned long)i, q, del_names[i]);
			q += nlen;
			left -= nlen;
		}
		TEST_ASSERT2(left == 0, "%lu trailing bytes in the del list",
		    (unsigned long)left);

		fprintf(stderr, "  ODLIST_UPDATE tabid=%u nfiles=%u ndels=%u\n",
		    tabid, nfiles, ndels);

		_van_free(NULL, rec.data);
		memset(&rec, 0, sizeof(rec));
		ret = lgc->get(lgc, &lsn, &rec, VAN_NEXT);
	}
	if (ret != VAN_NOTFOUND)
		CHK_RET(ret, "lgc->get");
	if (rec.data != NULL)
		_van_free(NULL, rec.data);

	TEST_ASSERT2(found == 1, "found %d ODLIST_UPDATE records, expected 1",
	    found);

	ret = lgc->close(lgc);
	CHK_RET(ret, "lgc->close");
	lgc = NULL;
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	fprintf(stderr, "============ 4 log_t1_3 passed =========\n");

	return (0);
}
