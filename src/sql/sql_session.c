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
#include "sql_internal.h"
#include "sql_ext.h"

/*
 * Session handling: one session owns a VanDB store, its catalog and the
 * tables that were opened through it.
 */

int sql_session_open(const char *dir, flags_t flags, SQL_SESSION **sessp) {
	SQL_SESSION *sess;
	VAN_STORE *store;
	int ret;

	if (dir == NULL || sessp == NULL)
		return (SQL_ERR_ARG);
	*sessp = NULL;

	sess = sql_calloc(1, sizeof(SQL_SESSION));
	if (sess == NULL)
		return (SQL_ERR_NOMEM);
	sess->dir = sql_strdup(dir);
	if (sess->dir == NULL) {
		sql_free(sess);
		return (SQL_ERR_NOMEM);
	}
	sess->oflags = flags;

	if (FLD_ISSET(flags, SQL_OPEN_CREATE)) {
		struct stat sb;

		if (stat(dir, &sb) != 0) {
			if (mkdir(dir, DEF_DIR_MODE) != 0) {
				sql_free(sess->dir);
				sql_free(sess);
				return (SQL_ERR_STORE);
			}
		}
	}

	store = NULL;
	ret = van_store_create(&store);
	if (ret != 0) {
		sql_free(sess->dir);
		sql_free(sess);
		return (SQL_ERR_STORE);
	}
	ret = store->open(store, dir, 0);
	if (ret != 0) {
		sql_free(sess->dir);
		sql_free(sess);
		return (SQL_ERR_STORE);
	}
	sess->store = store;
	sess->opened = 1;

	ret = sql_catalog_open(sess);
	if (ret != SQL_OK) {
		(void)sql_session_close(sess);
		return (ret);
	}

	*sessp = sess;
	return (SQL_OK);
}

int sql_session_close(SQL_SESSION *sess) {
	size_t i;
	int ret;

	if (sess == NULL)
		return (SQL_ERR_ARG);
	ret = SQL_OK;

	for (i = 0; i < sess->ntables; i++) {
		if (sql_table_close(sess->tables[i]) != SQL_OK)
			ret = SQL_ERR_STORE;
		sql_table_free(sess->tables[i]);
		sess->tables[i] = NULL;
	}
	sess->ntables = 0;
	sql_free(sess->tables);
	sess->tables = NULL;
	sess->cap_tables = 0;

	if (sql_catalog_close(sess) != SQL_OK)
		ret = SQL_ERR_STORE;

	/*
	 * _van_store_close() releases the store structure itself, so the
	 * pointer must not be touched afterwards.
	 */
	if (sess->store != NULL) {
		if (sess->store->close(sess->store) != 0)
			ret = SQL_ERR_STORE;
		sess->store = NULL;
	}
	sess->opened = 0;

	sql_free(sess->dir);
	sql_free(sess);

	return (ret);
}

/*
 * Shared implementation for sql_session_exec() and the script runner.
 *
 * 'strict_one' rejects anything left over after the first statement.
 */
static int session_run(SQL_SESSION *sess, const char *sql, FILE *out,
    size_t *nrowsp, SQL_RESULTSET **rsp, int strict_one, size_t *nstmtsp) {
	SQL_TOKENSTREAM *ts;
	SQL_STMT *stmt;
	int ret;
	size_t n;

	if (sess == NULL || sql == NULL)
		return (SQL_ERR_ARG);
	if (!sess->opened)
		return (SQL_ERR_STORE);

	ts = NULL;
	stmt = NULL;
	n = 0;

	ret = sql_lex(sql, 0, &ts);
	if (ret != SQL_OK)
		return (ret);

	sess->resultp = rsp;
	for (;;) {
		if (sql_ts_peek(ts, 0)->type == TK_EOF)
			break;
		ret = sql_parse(ts, &stmt);
		if (ret != SQL_OK)
			goto err;
		if (strict_one && sql_ts_peek(ts, 0)->type != TK_EOF) {
			ret = SQL_ERR_PARSE;
			goto err;
		}
		ret = sql_stmt_exec(sess, stmt, out, nrowsp);
		if (ret != SQL_OK)
			goto err;
		sql_stmt_free(stmt);
		stmt = NULL;
		n++;
		if (strict_one)
			break;
	}

	sql_tokenstream_free(ts);
	sess->resultp = NULL;
	if (nstmtsp != NULL)
		*nstmtsp = n;

	return (SQL_OK);

err:
	if (stmt != NULL)
		sql_stmt_free(stmt);
	sql_tokenstream_free(ts);
	sess->resultp = NULL;
	if (nstmtsp != NULL)
		*nstmtsp = n;
	return (ret);
}

int sql_session_exec(SQL_SESSION *sess, const char *sql, FILE *out,
    size_t *nrowsp) {

	return (session_run(sess, sql, out, nrowsp, NULL, 1, NULL));
}

int sql_session_query(SQL_SESSION *sess, const char *sql,
    SQL_RESULTSET **rsp) {

	if (rsp == NULL)
		return (SQL_ERR_ARG);
	*rsp = NULL;

	return (session_run(sess, sql, NULL, NULL, rsp, 1, NULL));
}

int sql_session_executescript(SQL_SESSION *sess, const char *sql, FILE *out,
    size_t *nstmtsp) {

	return (session_run(sess, sql, out, NULL, NULL, 0, nstmtsp));
}
