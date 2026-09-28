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
#ifndef _SQL_EXT_H
#define _SQL_EXT_H

#if defined(__cplusplus)
extern "C" {
#endif

/* src/sql/sql_ast.c -- memory & AST construction */
extern void *sql_calloc(size_t nelem, size_t elsize);
extern void sql_free(void *p);
extern char *sql_strdup(const char *str);
extern char *sql_strndup(const char *str, size_t len);
extern char *sql_strlower(const char *str, size_t len);

extern int sql_expr_alloc(sql_expr_type xtype, SQL_EXPR **exprp);
extern void sql_expr_free(SQL_EXPR *expr);
extern SQL_EXPR *sql_expr_literal(const SQL_VALUE *val);
extern SQL_EXPR *sql_expr_column(const char *name, size_t len);
extern SQL_EXPR *sql_expr_binary(sql_op op, SQL_EXPR *left,
    SQL_EXPR *right);
extern SQL_EXPR *sql_expr_unary(sql_op op, SQL_EXPR *arg);
extern SQL_EXPR *sql_expr_not(SQL_EXPR *arg);
extern SQL_EXPR *sql_expr_isnull(SQL_EXPR *arg, int negate);
extern SQL_EXPR *sql_expr_in(SQL_EXPR *arg, SQL_EXPRLIST *list,
    int negate);
extern SQL_EXPR *sql_expr_between(SQL_EXPR *arg, SQL_EXPR *low,
    SQL_EXPR *high, int negate);
extern SQL_EXPR *sql_expr_like(SQL_EXPR *arg, SQL_EXPR *pat, int negate);
extern SQL_EXPR *sql_expr_func(sql_kw kw);
extern int sql_exprlist_alloc(SQL_EXPRLIST **listp);
extern void sql_exprlist_free(SQL_EXPRLIST *list);
extern int sql_exprlist_add(SQL_EXPRLIST *list, SQL_EXPR *expr);
extern int sql_stmt_alloc(sql_stmt_type type, SQL_STMT **stmtp);
extern void sql_stmt_free(SQL_STMT *stmt);

/* src/sql/sql_lex.c */
extern int sql_lex(const char *sql, size_t len, SQL_TOKENSTREAM **tsp);
extern void sql_tokenstream_free(SQL_TOKENSTREAM *ts);
extern SQL_TOKEN *sql_ts_peek(SQL_TOKENSTREAM *ts, size_t ahead);
extern void sql_ts_consume(SQL_TOKENSTREAM *ts);
extern int sql_kw_lookup(const char *text, size_t len);

/* src/sql/sql_parse.c */
extern int sql_parse(SQL_TOKENSTREAM *ts, SQL_SESSION *sess, SQL_STMT **stmtp);

/* src/sql/sql_type.c */
extern int sql_type_parse(sql_kw kw, int has_arg, size_t arg,
    sql_type_t *typep, size_t *lengthp);
extern int sql_value_cast(const SQL_VALUE *src, sql_type_t type,
    size_t length, SQL_VALUE *dst);
extern int sql_value_numeric(const SQL_VALUE *val, double *dp);
extern int sql_value_to_hex(const SQL_VALUE *val, char **sp);
extern int sql_value_from_hex(const char *hex, SQL_VALUE *val);
extern int sql_string_to_date(const char *str, size_t len, int64 *daysp);
extern int sql_string_to_time(const char *str, size_t len, int64 *usecp);
extern int sql_string_to_timestamp(const char *str, size_t len,
    int64 *msecp);
extern int sql_date_to_string(int64 days, char **sp);
extern int sql_time_to_string(int64 usec, char **sp);
extern int sql_timestamp_to_string(int64 msec, char **sp);
extern int sql_now_date(int64 *daysp);
extern int sql_now_time(int64 *usecp);
extern int sql_now_timestamp(int64 *msecp);

/* src/sql/sql_row.c */
extern void sql_rowkey_encode(uint64 rowid, unsigned char *buf);
extern void sql_rowkey_decode(const unsigned char *buf, uint64 *rowidp);
extern int sql_row_encode(SQL_SCHEMA *schema, const SQL_VALUE *vals,
    unsigned char **bufp, size_t *lenp);
extern int sql_row_decode(SQL_SCHEMA *schema, const void *data,
    size_t len, SQL_VALUE *vals);
extern void sql_row_values_clear(SQL_VALUE *vals, size_t ncols);

/* src/sql/sql_catalog.c */
extern int sql_catalog_open(SQL_SESSION *sess);
extern int sql_catalog_close(SQL_SESSION *sess);
extern int sql_catalog_load(SQL_SESSION *sess);
extern int sql_catalog_save_schema(SQL_SESSION *sess, SQL_SCHEMA *schema);
extern int sql_catalog_drop(SQL_SESSION *sess, const char *lname);
extern int sql_schema_serialize(SQL_SCHEMA *schema, char **textp);
extern int sql_schema_deserialize(const char *text, SQL_SCHEMA **schemap);
extern int sql_table_alloc(SQL_SESSION *sess, const char *name,
    SQL_SCHEMA *schema, SQL_TABLE **tbp);
extern void sql_table_free(SQL_TABLE *tb);
extern SQL_TABLE *sql_session_find(SQL_SESSION *sess, const char *lname);
extern int sql_session_addtable(SQL_SESSION *sess, SQL_TABLE *tb);
extern int sql_session_deltable(SQL_SESSION *sess, const char *lname);
extern int sql_table_open(SQL_SESSION *sess, SQL_TABLE *tb);
extern int sql_table_close(SQL_TABLE *tb);
extern int sql_kv_put(SQL_SESSION *sess, SQL_TABLE *tb, uint64 rowid,
    const void *data, size_t len);
extern int sql_kv_get(SQL_SESSION *sess, SQL_TABLE *tb, uint64 rowid,
    void **datap, size_t *lenp);
extern int sql_kv_del(SQL_SESSION *sess, SQL_TABLE *tb, uint64 rowid);
extern void sql_kv_free(void *p);
extern int sql_table_ensure_open(SQL_SESSION *sess, SQL_TABLE *tb);

/* src/sql/sql_exec.c */
extern int sql_schema_findcol(SQL_SCHEMA *schema, const char *lname,
    size_t *indxp);
extern int sql_expr_eval(SQL_SESSION *sess, SQL_SCHEMA *schema,
    const SQL_VALUE *row, const SQL_EXPR *expr, SQL_VALUE *res);
extern int sql_expr_test(SQL_SESSION *sess, SQL_SCHEMA *schema,
    const SQL_VALUE *row, const SQL_EXPR *expr, int *tv);
extern int sql_stmt_exec(SQL_SESSION *sess, SQL_STMT *stmt, FILE *out,
    size_t *nrowsp);

/* src/sql/sql_session.c */
extern int sql_session_executescript(SQL_SESSION *sess, const char *sql,
    FILE *out, size_t *nstmtsp);

#ifdef __cplusplus
}
#endif

#endif
