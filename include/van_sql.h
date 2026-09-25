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
#ifndef _VAN_SQL_H
#define _VAN_SQL_H

#if defined(__cplusplus)
extern "C" {
#endif

#include <stdio.h>

#include "van_types.h"

/*
 * The SQL layer lives on top of the VanDB key-value store.  Only pointers
 * to the engine structures are needed here, so forward declarations are
 * enough and this header stays usable without "van_int.h".
 */
struct _van_store;
struct _van_table;

/* Return codes of the SQL layer. */
#define	SQL_OK			0
#define	SQL_ERR_LEX		(-1)	/* Illegal token		*/
#define	SQL_ERR_PARSE		(-2)	/* Syntax error			*/
#define	SQL_ERR_NOMEM		(-3)	/* Out of memory		*/
#define	SQL_ERR_NO_TABLE	(-4)	/* Table does not exist		*/
#define	SQL_ERR_DUP_TABLE	(-5)	/* Table already exists		*/
#define	SQL_ERR_DUP_KEY		(-6)	/* Primary/unique violation	*/
#define	SQL_ERR_NO_COLUMN	(-7)	/* Column does not exist	*/
#define	SQL_ERR_AMBIGUOUS	(-8)	/* Ambiguous name		*/
#define	SQL_ERR_TYPE		(-9)	/* Type/value mismatch		*/
#define	SQL_ERR_NOT_NULL	(-10)	/* NOT NULL violation		*/
#define	SQL_ERR_ARG		(-11)	/* Bad argument / arity		*/
#define	SQL_ERR_FEATURE		(-12)	/* Feature not supported yet	*/
#define	SQL_ERR_STORE		(-13)	/* Underlying store failure	*/
#define	SQL_ERR_CATALOG		(-14)	/* Catalog corrupted		*/
#define	SQL_ERR_CONVERT		(-15)	/* Value conversion failure	*/
#define	SQL_ERR_LIMIT		(-16)	/* Internal limit reached	*/
#define	SQL_ERR_READONLY	(-17)	/* Session opened read only	*/
#define	SQL_ERR_UNKNOWN		(-99)	/* Anything else		*/

/* Safety limits of the minimal layer. */
#define	SQL_MAX_COLUMNS		256
#define	SQL_MAX_IDENT_LEN	128
#define	SQL_MAX_ROWS_DEFAULT	(1024 * 1024)

/*
 * Supported column types.
 *
 * The numeric ids are part of the persisted catalog format, do not
 * reorder or renumber them.
 */
typedef enum {
	SQL_T_NULL = 0,		/* Untyped NULL literal		*/
	SQL_T_SMALLINT = 1,	/* 16 bit signed integer	*/
	SQL_T_INT = 2,		/* 32 bit signed integer	*/
	SQL_T_BIGINT = 3,	/* 64 bit signed integer	*/
	SQL_T_FLOAT = 4,	/* 32 bit IEEE float		*/
	SQL_T_DOUBLE = 5,	/* 64 bit IEEE double		*/
	SQL_T_VARCHAR = 6,	/* Variable length string	*/
	SQL_T_CHAR = 7,		/* Blank padded string		*/
	SQL_T_TEXT = 8,		/* Unlimited string		*/
	SQL_T_BINARY = 9,	/* Zero padded byte string	*/
	SQL_T_VARBINARY = 10,	/* Variable length byte string	*/
	SQL_T_BLOB = 11,	/* Unlimited byte string	*/
	SQL_T_DATE = 12,	/* Days since 1970-01-01	*/
	SQL_T_TIME = 13,	/* Microseconds since midnight	*/
	SQL_T_TIMESTAMP = 14	/* Milliseconds since epoch	*/
} sql_type_t;

#define	SQL_TYPE_IS_INT(t)						\
	((t) == SQL_T_SMALLINT || (t) == SQL_T_INT || (t) == SQL_T_BIGINT)
#define	SQL_TYPE_IS_FLOAT(t)						\
	((t) == SQL_T_FLOAT || (t) == SQL_T_DOUBLE)
#define	SQL_TYPE_IS_NUMERIC(t)						\
	(SQL_TYPE_IS_INT(t) || SQL_TYPE_IS_FLOAT(t))
#define	SQL_TYPE_IS_STRING(t)						\
	((t) == SQL_T_CHAR || (t) == SQL_T_VARCHAR || (t) == SQL_T_TEXT)
#define	SQL_TYPE_IS_BINARY(t)						\
	((t) == SQL_T_BINARY || (t) == SQL_T_VARBINARY || (t) == SQL_T_BLOB)
#define	SQL_TYPE_IS_TEMPORAL(t)						\
	((t) == SQL_T_DATE || (t) == SQL_T_TIME ||			\
	 (t) == SQL_T_TIMESTAMP)
#define	SQL_TYPE_HAS_LENGTH(t)						\
	((t) == SQL_T_VARCHAR || (t) == SQL_T_CHAR ||			\
	 (t) == SQL_T_BINARY || (t) == SQL_T_VARBINARY)

/* Sorting classes used by sql_value_cmp(). */
#define	SQL_CMP_NULL		0
#define	SQL_CMP_NUMERIC		1
#define	SQL_CMP_TEMPORAL	2
#define	SQL_CMP_STRING		3
#define	SQL_CMP_BINARY		4

/*
 * A SQL value.
 *
 * Strings and byte strings are always owned copies stored in sval, use
 * sql_value_clear() to release them.
 */
typedef struct _sql_value {
	sql_type_t	type;		/* Declared/converted type	*/
	int		isnull;		/* SQL NULL			*/
	int64		ival;		/* Integers & temporal values	*/
	double		dval;		/* Approximate numbers		*/
	unsigned char	*sval;		/* Strings & binary data	*/
	size_t		slen;		/* Byte count of sval		*/
} SQL_VALUE;

/*
 * Column definition.
 */
typedef struct _sql_column {
	char		*name;		/* Original spelling		*/
	char		*lname;		/* Lower case name for lookup	*/
	sql_type_t	type;
	size_t		length;		/* Declared length, 0 if none	*/
	int		not_null;	/* NOT NULL constraint		*/
	int		primary_key;	/* Column level PRIMARY KEY	*/
	int		has_default;	/* DEFAULT was given		*/
	SQL_VALUE	defval;		/* The default value		*/
} SQL_COLUMN;

/*
 * A table schema, this is what gets serialized into the catalog.
 */
typedef struct _sql_schema {
	char		*name;
	char		*lname;
	size_t		ncols;
	size_t		capacity;
	SQL_COLUMN	*cols;
	int		pk_indx;	/* Rowid alias column, -1 none	*/
	uint64		next_rowid;	/* Next synthetic row id	*/
} SQL_SCHEMA;

/*
 * A SQL table: its schema plus the opened VanDB table backing it.
 */
typedef struct _sql_table {
	char		*name;		/* SQL name			*/
	char		*lname;		/* Lower case SQL name		*/
	char		*kvname;	/* VanDB table name		*/
	SQL_SCHEMA	*schema;
	struct _van_table *table;	/* NULL until opened		*/
	int		opened;
} SQL_TABLE;

/*
 * Result set returned by SELECT, also used internally by UPDATE/DELETE
 * to carry the rows that have to be rewritten.
 */
typedef struct _sql_resultset {
	size_t		ncols;
	char		**colnames;
	size_t		nrows;
	size_t		caprows;
	SQL_VALUE	**rows;		/* nrows rows of ncols values	*/
} SQL_RESULTSET;

/*
 * A session owns the underlying store and the opened tables.
 */
typedef struct _sql_session {
	struct _van_store *store;
	char		*dir;
	size_t		ntables;
	size_t		cap_tables;
	SQL_TABLE	**tables;
	struct _sql_table *catalog;	/* Schema catalog		*/
	int		opened;
	flags_t		oflags;
	/*
	 * sql_session_query() points this at the caller supplied pointer
	 * that receives the result set, otherwise it stays NULL and the
	 * result is printed instead.
	 */
	SQL_RESULTSET	**resultp;
	/* Change statistics of the last statement. */
	size_t		rows_affected;
	size_t		last_rowid;
} SQL_SESSION;

/* Open/close flags for sql_session_open(). */
#define	SQL_OPEN_CREATE		0x01	/* Create the store directory	*/
#define	SQL_OPEN_READONLY	0x02	/* SELECT statements only	*/

/* Public APIs. */
extern int sql_session_open(const char *dir, flags_t flags,
    SQL_SESSION **sessp);
extern int sql_session_close(SQL_SESSION *sess);
extern int sql_session_exec(SQL_SESSION *sess, const char *sql, FILE *out,
    size_t *nrowsp);
extern int sql_session_query(SQL_SESSION *sess, const char *sql,
    SQL_RESULTSET **rsp);

/* Errors & diagnostics. */
extern const char *sql_errstr(int err);

/* Types. */
extern const char *sql_type_name(sql_type_t type);
extern void sql_column_init(SQL_COLUMN *col);
extern void sql_column_clear(SQL_COLUMN *col);
extern void sql_schema_free(SQL_SCHEMA *schema);

/* Values. */
extern void sql_value_init(SQL_VALUE *val);
extern void sql_value_clear(SQL_VALUE *val);
extern int sql_value_copy(const SQL_VALUE *src, SQL_VALUE *dst);
extern int sql_value_set_null(SQL_VALUE *val, sql_type_t type);
extern int sql_value_set_int(SQL_VALUE *val, sql_type_t type, int64 v);
extern int sql_value_set_double(SQL_VALUE *val, sql_type_t type, double v);
extern int sql_value_set_string(SQL_VALUE *val, sql_type_t type,
    const void *data, size_t len);
extern int sql_value_from_text(SQL_VALUE *val, sql_type_t type,
    const char *text, size_t len);
extern int sql_value_tostring(const SQL_VALUE *val, char **sp);
extern int sql_value_cmp(const SQL_VALUE *a, const SQL_VALUE *b);

/* Result sets. */
extern int sql_resultset_alloc(size_t ncols, SQL_RESULTSET **rsp);
extern void sql_resultset_free(SQL_RESULTSET *rs);
extern int sql_resultset_setcolnames(SQL_RESULTSET *rs, char **names);
extern int sql_resultset_addrow(SQL_RESULTSET *rs, const SQL_VALUE *vals);
extern void sql_resultset_print(SQL_RESULTSET *rs, FILE *f);

#ifdef __cplusplus
}
#endif

#endif
