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
	int i, ret, putcnt, delcnt, othercnt;

	system("rm -fr test1*");
	system("rm -fr vanlog.*");

	table = NULL;
	store = NULL;
	lgc = NULL;
	putcnt = 0;
	delcnt = 0;
	othercnt = 0;

	fprintf(stderr, "============ 1 open table and do puts/dels =========\n");
	ret = _van_store_create(&store);
	CHK_RET(ret, "_van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "_van_store_open");
	ret = _van_table_create(store, &table);
	CHK_RET(ret, "_van_table_create");
	table->set_inmem_threshold(table, GIGABYTE);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "_van_table_open");

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
	ret = _van_store_create(&store);
	CHK_RET(ret, "_van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "_van_store_open");
	logp = store->log_handle;

	ret = _van_log_cursor_alloc(logp, &lgc);
	CHK_RET(ret, "_van_log_cursor_alloc");

	memset(&rec, 0, sizeof(rec));
	ret = _van_logrec_cget(lgc, &lsn, &rec, VAN_FIRST);
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
			default:
				othercnt++;
				break;
		}
		_van_free(NULL, rec.data);
		memset(&rec, 0, sizeof(rec));
		ret = _van_logrec_cget(lgc, &lsn, &rec, VAN_NEXT);
	}
	if (ret != VAN_NOTFOUND)
		CHK_RET(ret, "_van_logrec_cget");
	if (rec.data != NULL)
		_van_free(NULL, rec.data);

	fprintf(stderr, "log_t1_1: putcnt=%d delcnt=%d othercnt=%d\n",
	    putcnt, delcnt, othercnt);
	TEST_ASSERT(putcnt == LOG_T1_PUTS);
	TEST_ASSERT(delcnt == LOG_T1_DELS);
	TEST_ASSERT(othercnt == 0);

	ret = _van_log_cursor_close(lgc);
	CHK_RET(ret, "_van_log_cursor_close");
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
	int putcnt, delcnt, othercnt;
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
	recno = 0;
	memset(put_seen, 0, sizeof(put_seen));
	memset(del_seen, 0, sizeof(del_seen));

	fprintf(stderr, "============ 1 open table and do puts/dels =========\n");
	ret = _van_store_create(&store);
	CHK_RET(ret, "_van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "_van_store_open");
	ret = _van_table_create(store, &table);
	CHK_RET(ret, "_van_table_create");
	table->set_inmem_threshold(table, GIGABYTE);
	ret = table->open(table, "test1", VAN_TABLE_SKIP_THREAD_START);
	CHK_RET(ret, "_van_table_open");
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
	ret = _van_store_create(&store);
	CHK_RET(ret, "_van_store_create");
	ret = store->open(store, ".", 0);
	CHK_RET(ret, "_van_store_open");
	logp = store->log_handle;

	ret = _van_log_cursor_alloc(logp, &lgc);
	CHK_RET(ret, "_van_log_cursor_alloc");

	memset(&rec, 0, sizeof(rec));
	ret = _van_logrec_cget(lgc, &lsn, &rec, VAN_FIRST);
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
			default:
				fprintf(stderr, "  rec[%d] UNKNOWN type=%u\n",
				    recno, type);
				othercnt++;
				break;
		}

		recno++;
		_van_free(NULL, rec.data);
		memset(&rec, 0, sizeof(rec));
		ret = _van_logrec_cget(lgc, &lsn, &rec, VAN_NEXT);
	}
	if (ret != VAN_NOTFOUND)
		CHK_RET(ret, "_van_logrec_cget");
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

	ret = _van_log_cursor_close(lgc);
	CHK_RET(ret, "_van_log_cursor_close");
	lgc = NULL;
	ret = store->close(store);
	CHK_RET(ret, "store->close");

	fprintf(stderr, "============ 3 log_t1_2 passed =========\n");

	return (0);
}
