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
#ifndef _SQL_INTERNAL_H
#define _SQL_INTERNAL_H

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Internal declarations shared by the SQL layer modules.
 *
 * Unlike "van_sql.h" this header pulls in the engine internals, it is
 * never installed for consumers of the public SQL API.
 */
#include "van_int.h"
#include "van.h"
#include "van_sql.h"

/*
 * There is no table level cursor in the engine yet, so a full scan walks
 * the synthetic row ids, which are encoded as big endian 64 bit values.
 */
#define	SQL_KVSIZE		sizeof(uint64)
#define	SQL_MAXTABLENAME	(SQL_MAX_IDENT_LEN + 16)

/* Catalog bookkeeping lives in a dedicated table of its own. */
#define	SQL_CATALOG_TABLE	"__van_sql_catalog"
#define	SQL_SCHEMA_VERSION	"vansql1"

/********************************************************************
 * Lexer
 ********************************************************************/
typedef enum {
	TK_EOF = 0,
	TK_IDENT,
	TK_INTEGER,
	TK_FLOAT,
	TK_STRING,
	TK_BLOB,		/* X'..' and B'..' literals		*/
	TK_PUNCT
} sql_tk_type;

typedef enum {
	KW_NONE = 0,
	KW_ADD,			/* Accepted, never required.		*/
	KW_ALL,
	KW_AND,
	KW_AS,
	KW_ASC,
	KW_BETWEEN,
	KW_BLOB,
	KW_BY,
	KW_CHAR,
	KW_CREATE,
	KW_CURRENT_DATE,
	KW_CURRENT_TIME,
	KW_CURRENT_TIMESTAMP,
	KW_DATE,
	KW_DEFAULT,
	KW_DELETE,
	KW_DESC,
	KW_DISTINCT,
	KW_DOUBLE,
	KW_DROP,
	KW_EXISTS,
	KW_FLOAT,
	KW_FROM,
	KW_IF,
	KW_IN,
	KW_INSERT,
	KW_INT,
	KW_INTO,
	KW_IS,
	KW_KEY,
	KW_LIKE,
	KW_LIMIT,
	KW_NOT,
	KW_NULL,
	KW_OFFSET,
	KW_OR,
	KW_ORDER,
	KW_PRIMARY,
	KW_SELECT,
	KW_SET,
	KW_SMALLINT,
	KW_TABLE,
	KW_TEXT,
	KW_TIME,
	KW_TIMESTAMP,
	KW_UPDATE,
	KW_VALUES,
	KW_VARCHAR,
	KW_WHERE,
	KW_BINARY,
	KW_VARBINARY,
	KW_BIGINT
} sql_kw;

typedef struct _sql_token {
	sql_tk_type	type;
	sql_kw		kw;		/* Set for TK_IDENT keywords	*/
	const char	*text;		/* Points into the lexer input	*/
	size_t		len;
	size_t		off;		/* Byte offset into the input	*/
	int64		ival;		/* TK_INTEGER			*/
	double		fval;		/* TK_FLOAT			*/
	unsigned char	*sval;		/* Unescaped string/blob	*/
	size_t		slen;
} SQL_TOKEN;

typedef struct _sql_tokenstream {
	SQL_TOKEN	*tokens;
	size_t		ntokens;
	size_t		capacity;
	size_t		pos;		/* Parser cursor			*/
	char		*input;		/* Owned copy of the statement	*/
} SQL_TOKENSTREAM;

/********************************************************************
 * Abstract syntax tree
 ********************************************************************/
typedef enum {
	STMT_CREATE_TABLE = 1,
	STMT_DROP_TABLE,
	STMT_INSERT,
	STMT_SELECT,
	STMT_UPDATE,
	STMT_DELETE
} sql_stmt_type;

typedef enum {
	OP_PLUS = 1,
	OP_MINUS,
	OP_MUL,
	OP_DIV,
	OP_MOD,
	OP_EQ,
	OP_NE,
	OP_LT,
	OP_LE,
	OP_GT,
	OP_GE,
	OP_AND,
	OP_OR
} sql_op;

typedef enum {
	EX_LITERAL = 1,
	EX_COLUMN,
	EX_UNARY,
	EX_BINARY,
	EX_NOT,
	EX_ISNULL,
	EX_IN,
	EX_BETWEEN,
	EX_LIKE,
	EX_FUNC
} sql_expr_type;

typedef struct _sql_expr SQL_EXPR;
typedef struct _sql_exprlist SQL_EXPRLIST;

struct _sql_exprlist {
	size_t		nitems;
	size_t		capacity;
	SQL_EXPR	**items;
};

struct _sql_expr {
	sql_expr_type	xtype;
	sql_op		op;		/* EX_UNARY / EX_BINARY		*/
	sql_kw		kw;		/* EX_FUNC			*/
	int		negate;		/* NOT LIKE / NOT IN / ...	*/
	SQL_VALUE	lit;		/* EX_LITERAL			*/
	char		*name;		/* EX_COLUMN / EX_FUNC		*/
	char		*lname;
	SQL_EXPR	*left;
	SQL_EXPR	*right;
	SQL_EXPR	*third;		/* BETWEEN upper bound		*/
	SQL_EXPRLIST	*list;		/* IN value list		*/
};

typedef struct _sql_select_item {
	SQL_EXPR	*expr;
	char		*alias;
	char		*lalias;
} SQL_SELECT_ITEM;

typedef struct _sql_order_item {
	SQL_EXPR	*expr;
	int		desc;
} SQL_ORDER_ITEM;

typedef struct _sql_set_item {
	char		*colname;
	char		*lcolname;
	int		resolved;	/* Resolved column index	*/
	SQL_EXPR	*expr;
} SQL_SET_ITEM;

typedef struct _sql_stmt {
	sql_stmt_type	type;

	/* CREATE TABLE / DROP TABLE / INSERT / UPDATE / DELETE target */
	char		*tablename;
	char		*ltablename;
	int		if_exists;
	int		if_not_exists;

	/* CREATE TABLE */
	SQL_COLUMN	*columns;
	size_t		ncols;
	size_t		capcols;

	/* INSERT */
	char		**inscols;
	char		**linscols;
	size_t		ninscols;
	size_t		capins;
	size_t		caplins;
	SQL_EXPRLIST	**insert_rows;	/* Each row is an expression list */
	size_t		nrows_ins;
	size_t		caprows_ins;

	/* SELECT */
	SQL_SELECT_ITEM	*select_items;
	size_t		nselect;
	size_t		capselect;
	int		select_star;
	int		distinct;
	SQL_EXPR	*where;
	SQL_ORDER_ITEM	*order_by;
	size_t		norder;
	size_t		caporder;
	SQL_EXPR	*limit;
	SQL_EXPR	*offset;

	/* UPDATE */
	SQL_SET_ITEM	*set_items;
	size_t		nset;
	size_t		capset;
} SQL_STMT;

/********************************************************************
 * Row helpers
 ********************************************************************/
typedef struct _sql_row {
	uint64		rowid;
	size_t		ncols;
	SQL_VALUE	*vals;
} SQL_ROW;

#if defined(__cplusplus)
}
#endif

#endif
