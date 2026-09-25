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
 * Recursive descent parser for the supported subset of SQL:
 *
 *	CREATE TABLE [IF NOT EXISTS] <t> ( <coldef> [, <coldef>]* )
 *	DROP TABLE [IF EXISTS] <t>
 *	INSERT INTO <t> [ ( <col> [, <col>]* ) ] VALUES ( ... ) [, ( ... )]*
 *	SELECT [DISTINCT] <sel> [, <sel>]* FROM <t> [WHERE <e>]
 *	    [ORDER BY <e> [ASC|DESC] [, ...]] [LIMIT <e> [OFFSET <e>]]
 *	UPDATE <t> SET <col> = <e> [, <col> = <e>]* [WHERE <e>]
 *	DELETE FROM <t> [WHERE <e>]
 *
 * Every statement may be terminated by a semicolon, EOF also terminates
 * it.  Anything else shows up as SQL_ERR_PARSE.
 */

static SQL_TOKEN *peek(SQL_TOKENSTREAM *ts, size_t n) {

	return (sql_ts_peek(ts, n));
}

static int tok_is_kw(SQL_TOKEN *tk, sql_kw kw) {

	return (tk != NULL && tk->type == TK_IDENT && tk->kw == kw);
}

static int is_kw(SQL_TOKENSTREAM *ts, sql_kw kw, size_t n) {

	return (tok_is_kw(peek(ts, n), kw));
}

static int tok_is_punct(SQL_TOKEN *tk, const char *s) {
	size_t len;

	if (tk == NULL || tk->type != TK_PUNCT)
		return (0);
	len = strlen(s);
	if (len != tk->len)
		return (0);

	return (memcmp(tk->text, s, len) == 0);
}

static int tok_is_eof(SQL_TOKEN *tk) {

	return (tk != NULL && tk->type == TK_EOF);
}

/* Consume the current token when it is the given keyword. */
static int accept_kw(SQL_TOKENSTREAM *ts, sql_kw kw) {

	if (!is_kw(ts, kw, 0))
		return (0);
	sql_ts_consume(ts);

	return (1);
}

static int accept_punct(SQL_TOKENSTREAM *ts, const char *s) {

	if (!tok_is_punct(peek(ts, 0), s))
		return (0);
	sql_ts_consume(ts);

	return (1);
}

/* Same as accept_punct() but it is an error when the token is missing. */
static int expect_punct(SQL_TOKENSTREAM *ts, const char *s) {

	if (!accept_punct(ts, s))
		return (SQL_ERR_PARSE);

	return (SQL_OK);
}

static int expect_kw(SQL_TOKENSTREAM *ts, sql_kw kw) {

	if (!accept_kw(ts, kw))
		return (SQL_ERR_PARSE);

	return (SQL_OK);
}

/*
 * Grow an array of 'need' elements of 'elsize' bytes each.  Elements
 * may be pointers or records, hence the explicit element size.
 */
static int grow_vec(void **vecp, size_t *allocp, size_t need,
    size_t elsize) {
	void *newv;
	size_t newcap;

	if (need <= *allocp)
		return (SQL_OK);
	newcap = (*allocp == 0 ? 8 : *allocp);
	while (newcap < need)
		newcap *= 2;
	newv = realloc(*vecp, newcap * elsize);
	if (newv == NULL)
		return (SQL_ERR_NOMEM);
	*vecp = newv;
	*allocp = newcap;

	return (SQL_OK);
}

/********************************************************************
 * Identifiers, types and literals
 ********************************************************************/
static int parse_ident(SQL_TOKENSTREAM *ts, char **namep, char **lnamep) {
	SQL_TOKEN *tk;

	tk = peek(ts, 0);
	if (tk->type != TK_IDENT || tk->kw != KW_NONE)
		return (SQL_ERR_PARSE);
	if (tk->len == 0 || tk->len > SQL_MAX_IDENT_LEN)
		return (SQL_ERR_PARSE);
	*namep = sql_strndup(tk->text, tk->len);
	*lnamep = sql_strlower(tk->text, tk->len);
	if (*namep == NULL || *lnamep == NULL)
		return (SQL_ERR_NOMEM);
	sql_ts_consume(ts);

	return (SQL_OK);
}

/*
 * <typename> [ ( <number> [ , <number> ] ) ]
 */
static int parse_type(SQL_TOKENSTREAM *ts, sql_type_t *typep,
    size_t *lengthp) {
	SQL_TOKEN *tk;
	size_t arg;
	int ret, has_arg;

	tk = peek(ts, 0);
	if (tk->type != TK_IDENT)
		return (SQL_ERR_PARSE);

	arg = 0;
	has_arg = 0;
	sql_ts_consume(ts);

	if (accept_punct(ts, "(")) {
		SQL_TOKEN *num;
		num = peek(ts, 0);
		if (num->type != TK_INTEGER || num->ival <= 0)
			return (SQL_ERR_PARSE);
		arg = (size_t)num->ival;
		has_arg = 1;
		sql_ts_consume(ts);
		/* FLOAT(p, s) or DOUBLE(precision, scale) */
		if (accept_punct(ts, ",")) {
			num = peek(ts, 0);
			if (num->type != TK_INTEGER)
				return (SQL_ERR_PARSE);
			sql_ts_consume(ts);
		}
		ret = expect_punct(ts, ")");
		if (ret != SQL_OK)
			return (ret);
	}

	return (sql_type_parse(tk->kw, has_arg, arg, typep, lengthp));
}

/* Build a SQL_VALUE out of a literal token. */
static int token_value(SQL_TOKEN *tk, SQL_VALUE *val) {
	int ret;

	ret = SQL_OK;
	switch (tk->type) {
		case TK_INTEGER:
			if (tk->ival >= -2147483648LL &&
			    tk->ival <= 2147483647LL)
				ret = sql_value_set_int(val, SQL_T_INT,
				    tk->ival);
			else
				ret = sql_value_set_int(val, SQL_T_BIGINT,
				    tk->ival);
			break;
		case TK_FLOAT:
			ret = sql_value_set_double(val, SQL_T_DOUBLE,
			    tk->fval);
			break;
		case TK_STRING:
			ret = sql_value_from_text(val, SQL_T_VARCHAR,
			    (const char *)tk->sval, tk->slen);
			break;
		case TK_BLOB:
			ret = sql_value_set_string(val, SQL_T_BLOB,
			    tk->sval, tk->slen);
			break;
		default:
			return (SQL_ERR_PARSE);
	}

	return (ret);
}

/*
 * Column definition:
 *	<name> <type> [ NOT NULL | NULL | PRIMARY KEY | DEFAULT <lit> ]*
 */
static int parse_column_def(SQL_TOKENSTREAM *ts, SQL_COLUMN *col) {
	int ret;

	ret = parse_ident(ts, &col->name, &col->lname);
	if (ret != SQL_OK)
		return (ret);
	ret = parse_type(ts, &col->type, &col->length);
	if (ret != SQL_OK)
		return (ret);

	for (;;) {
		if (accept_kw(ts, KW_NOT)) {
			ret = expect_kw(ts, KW_NULL);
			if (ret != SQL_OK)
				return (ret);
			col->not_null = 1;
			continue;
		}
		if (accept_kw(ts, KW_NULL)) {
			col->not_null = 0;
			continue;
		}
		if (tok_is_kw(peek(ts, 0), KW_PRIMARY)) {
			ret = expect_kw(ts, KW_PRIMARY);
			if (ret != SQL_OK)
				return (ret);
			ret = expect_kw(ts, KW_KEY);
			if (ret != SQL_OK)
				return (ret);
			col->primary_key = 1;
			continue;
		}
		if (accept_kw(ts, KW_DEFAULT)) {
			SQL_TOKEN *tk;
			int neg;

			neg = 0;
			if (accept_punct(ts, "-"))
				neg = 1;
			else
				(void)accept_punct(ts, "+");
			tk = peek(ts, 0);
			if (tok_is_kw(tk, KW_NULL)) {
				sql_ts_consume(ts);
				sql_value_clear(&col->defval);
				ret = sql_value_set_null(&col->defval,
				    col->type);
				if (ret != SQL_OK)
					return (ret);
				col->has_default = 1;
				continue;
			}
			if (tk->type != TK_STRING && tk->type != TK_INTEGER &&
			    tk->type != TK_FLOAT && tk->type != TK_BLOB)
				return (SQL_ERR_PARSE);
			ret = token_value(tk, &col->defval);
			if (ret != SQL_OK)
				return (ret);
			if (neg) {
				if (!SQL_TYPE_IS_NUMERIC(col->defval.type))
					return (SQL_ERR_PARSE);
				if (col->defval.type == SQL_T_DOUBLE)
					col->defval.dval = -col->defval.dval;
				else
					col->defval.ival = -col->defval.ival;
			}
			sql_ts_consume(ts);
			col->has_default = 1;
			continue;
		}
		break;
	}

	return (SQL_OK);
}

/********************************************************************
 * Expressions
 ********************************************************************/
static int parse_expr(SQL_TOKENSTREAM *ts, SQL_EXPR **exprp);

static int parse_primary(SQL_TOKENSTREAM *ts, SQL_EXPR **exprp) {
	SQL_TOKEN *tk;
	SQL_VALUE val;
	SQL_EXPR *expr;
	int ret;

	tk = peek(ts, 0);
	if (tok_is_eof(tk))
		return (SQL_ERR_PARSE);

	if (tok_is_punct(tk, "(")) {
		sql_ts_consume(ts);
		ret = parse_expr(ts, &expr);
		if (ret != SQL_OK)
			return (ret);
		ret = expect_punct(ts, ")");
		if (ret != SQL_OK) {
			sql_expr_free(expr);
			return (ret);
		}
		*exprp = expr;
		return (SQL_OK);
	}

	if (tok_is_kw(tk, KW_NULL)) {
		sql_ts_consume(ts);
		sql_value_init(&val);
		expr = sql_expr_literal(&val);
		if (expr == NULL)
			return (SQL_ERR_NOMEM);
		*exprp = expr;
		return (SQL_OK);
	}

	if (tk->type == TK_INTEGER || tk->type == TK_FLOAT ||
	    tk->type == TK_STRING || tk->type == TK_BLOB) {
		sql_value_init(&val);
		ret = token_value(tk, &val);
		if (ret != SQL_OK) {
			sql_value_clear(&val);
			return (ret);
		}
		sql_ts_consume(ts);
		expr = sql_expr_literal(&val);
		sql_value_clear(&val);
		if (expr == NULL)
			return (SQL_ERR_NOMEM);
		*exprp = expr;
		return (SQL_OK);
	}

	if (tok_is_kw(tk, KW_CURRENT_DATE) ||
	    tok_is_kw(tk, KW_CURRENT_TIME) ||
	    tok_is_kw(tk, KW_CURRENT_TIMESTAMP)) {
		expr = sql_expr_func(tk->kw);
		if (expr == NULL)
			return (SQL_ERR_NOMEM);
		sql_ts_consume(ts);
		*exprp = expr;
		return (SQL_OK);
	}

	if (tk->type == TK_IDENT && tk->kw == KW_NONE) {
		if (tk->len == 0 || tk->len > SQL_MAX_IDENT_LEN)
			return (SQL_ERR_PARSE);
		expr = sql_expr_column(tk->text, tk->len);
		if (expr == NULL)
			return (SQL_ERR_NOMEM);
		sql_ts_consume(ts);
		*exprp = expr;
		return (SQL_OK);
	}

	/* Sub queries and everything else is out of scope. */
	return (SQL_ERR_FEATURE);
}

static int parse_unary(SQL_TOKENSTREAM *ts, SQL_EXPR **exprp) {
	SQL_EXPR *expr;
	sql_op op;
	int ret;

	if (tok_is_punct(peek(ts, 0), "-"))
		op = OP_MINUS;
	else if (tok_is_punct(peek(ts, 0), "+"))
		op = OP_PLUS;
	else
		return (parse_primary(ts, exprp));

	sql_ts_consume(ts);
	ret = parse_unary(ts, &expr);
	if (ret != SQL_OK)
		return (ret);
	*exprp = sql_expr_unary(op, expr);
	if (*exprp == NULL) {
		sql_expr_free(expr);
		return (SQL_ERR_NOMEM);
	}

	return (SQL_OK);
}

static int parse_arith_op(SQL_TOKENSTREAM *ts, sql_op *opp) {
	SQL_TOKEN *tk;

	tk = peek(ts, 0);
	if (tok_is_punct(tk, "*"))
		*opp = OP_MUL;
	else if (tok_is_punct(tk, "/"))
		*opp = OP_DIV;
	else if (tok_is_punct(tk, "%"))
		*opp = OP_MOD;
	else if (tok_is_punct(tk, "||"))
		*opp = OP_PLUS;	/* String concatenation. */
	else
		return (0);

	return (1);
}

static int parse_mul(SQL_TOKENSTREAM *ts, SQL_EXPR **exprp) {
	SQL_EXPR *left, *right;
	sql_op op;
	int ret;

	ret = parse_unary(ts, &left);
	if (ret != SQL_OK)
		return (ret);

	while (parse_arith_op(ts, &op)) {
		sql_ts_consume(ts);
		ret = parse_unary(ts, &right);
		if (ret != SQL_OK) {
			sql_expr_free(left);
			return (ret);
		}
		left = sql_expr_binary(op, left, right);
		if (left == NULL) {
			sql_expr_free(right);
			return (SQL_ERR_NOMEM);
		}
	}

	*exprp = left;
	return (SQL_OK);
}

static int parse_add(SQL_TOKENSTREAM *ts, SQL_EXPR **exprp) {
	SQL_EXPR *left, *right;
	sql_op op;
	int ret;

	ret = parse_mul(ts, &left);
	if (ret != SQL_OK)
		return (ret);

	while (tok_is_punct(peek(ts, 0), "+") ||
	    tok_is_punct(peek(ts, 0), "-")) {
		op = tok_is_punct(peek(ts, 0), "+") ? OP_PLUS : OP_MINUS;
		sql_ts_consume(ts);
		ret = parse_mul(ts, &right);
		if (ret != SQL_OK) {
			sql_expr_free(left);
			return (ret);
		}
		left = sql_expr_binary(op, left, right);
		if (left == NULL) {
			sql_expr_free(right);
			return (SQL_ERR_NOMEM);
		}
	}

	*exprp = left;
	return (SQL_OK);
}

static int parse_exprlist(SQL_TOKENSTREAM *ts, SQL_EXPRLIST **listp) {
	SQL_EXPRLIST *list;
	SQL_EXPR *expr;
	int ret;

	ret = sql_exprlist_alloc(&list);
	if (ret != SQL_OK)
		return (ret);

	ret = expect_punct(ts, "(");
	if (ret != SQL_OK)
		goto err;

	if (accept_punct(ts, ")")) {
		*listp = list;
		return (SQL_OK);
	}

	for (;;) {
		ret = parse_expr(ts, &expr);
		if (ret != SQL_OK)
			goto err;
		ret = sql_exprlist_add(list, expr);
		if (ret != SQL_OK) {
			sql_expr_free(expr);
			goto err;
		}
		if (accept_punct(ts, ","))
			continue;
		ret = expect_punct(ts, ")");
		if (ret != SQL_OK)
			goto err;
		break;
	}

	*listp = list;
	return (SQL_OK);

err:
	sql_exprlist_free(list);
	return (ret);
}

/* Recognise a comparison operator, returns 0 when there is none. */
static int parse_cmp_op(SQL_TOKENSTREAM *ts, sql_op *opp) {
	SQL_TOKEN *tk;

	tk = peek(ts, 0);
	if (tok_is_punct(tk, "="))
		*opp = OP_EQ;
	else if (tok_is_punct(tk, "!=") || tok_is_punct(tk, "<>"))
		*opp = OP_NE;
	else if (tok_is_punct(tk, "<"))
		*opp = OP_LT;
	else if (tok_is_punct(tk, "<="))
		*opp = OP_LE;
	else if (tok_is_punct(tk, ">"))
		*opp = OP_GT;
	else if (tok_is_punct(tk, ">="))
		*opp = OP_GE;
	else
		return (0);

	return (1);
}

static int parse_predicate(SQL_TOKENSTREAM *ts, SQL_EXPR **exprp) {
	SQL_EXPR *left, *right, *high;
	SQL_EXPRLIST *list;
	sql_op op;
	int negate, ret;

	list = NULL;
	high = NULL;
	right = NULL;

	ret = parse_add(ts, &left);
	if (ret != SQL_OK)
		return (ret);

	if (parse_cmp_op(ts, &op)) {
		sql_ts_consume(ts);
		ret = parse_add(ts, &right);
		if (ret != SQL_OK) {
			sql_expr_free(left);
			return (ret);
		}
		*exprp = sql_expr_binary(op, left, right);
		if (*exprp == NULL) {
			sql_expr_free(left);
			sql_expr_free(right);
			return (SQL_ERR_NOMEM);
		}
		return (SQL_OK);
	}

	/* IS [NOT] NULL */
	if (tok_is_kw(peek(ts, 0), KW_IS)) {
		sql_ts_consume(ts);
		negate = 0;
		if (accept_kw(ts, KW_NOT))
			negate = 1;
		ret = expect_kw(ts, KW_NULL);
		if (ret != SQL_OK) {
			sql_expr_free(left);
			return (ret);
		}
		*exprp = sql_expr_isnull(left, negate);
		if (*exprp == NULL) {
			sql_expr_free(left);
			return (SQL_ERR_NOMEM);
		}
		return (SQL_OK);
	}

	/* [NOT] BETWEEN <low> AND <high> */
	negate = 0;
	if (tok_is_kw(peek(ts, 0), KW_NOT)) {
		negate = 1;
		sql_ts_consume(ts);
	}
	if (tok_is_kw(peek(ts, 0), KW_BETWEEN)) {
		sql_ts_consume(ts);
		ret = parse_add(ts, &right);
		if (ret != SQL_OK) {
			sql_expr_free(left);
			return (ret);
		}
		ret = expect_kw(ts, KW_AND);
		if (ret != SQL_OK)
			goto err2;
		ret = parse_add(ts, &high);
		if (ret != SQL_OK)
			goto err2;
		*exprp = sql_expr_between(left, right, high, negate);
		if (*exprp == NULL)
			goto err2;
		return (SQL_OK);
	}

	/* [NOT] IN ( <list> ) */
	if (tok_is_kw(peek(ts, 0), KW_IN)) {
		sql_ts_consume(ts);
		ret = parse_exprlist(ts, &list);
		if (ret != SQL_OK) {
			sql_expr_free(left);
			return (ret);
		}
		*exprp = sql_expr_in(left, list, negate);
		if (*exprp == NULL) {
			sql_expr_free(left);
			sql_exprlist_free(list);
			return (SQL_ERR_NOMEM);
		}
		return (SQL_OK);
	}

	/* [NOT] LIKE <pattern> */
	if (tok_is_kw(peek(ts, 0), KW_LIKE)) {
		sql_ts_consume(ts);
		ret = parse_add(ts, &right);
		if (ret != SQL_OK) {
			sql_expr_free(left);
			return (ret);
		}
		*exprp = sql_expr_like(left, right, negate);
		if (*exprp == NULL)
			goto err2;
		return (SQL_OK);
	}

	if (negate) {
		/* A NOT that is not part of a recognised predicate. */
		sql_expr_free(left);
		return (SQL_ERR_PARSE);
	}

	*exprp = left;
	return (SQL_OK);

err2:
	sql_expr_free(left);
	sql_expr_free(right);
	sql_expr_free(high);
	return (ret == SQL_OK ? SQL_ERR_NOMEM : ret);
}

static int parse_not(SQL_TOKENSTREAM *ts, SQL_EXPR **exprp) {
	SQL_EXPR *expr;
	int ret;

	if (!tok_is_kw(peek(ts, 0), KW_NOT))
		return (parse_predicate(ts, exprp));

	sql_ts_consume(ts);
	ret = parse_not(ts, &expr);
	if (ret != SQL_OK)
		return (ret);
	*exprp = sql_expr_not(expr);
	if (*exprp == NULL) {
		sql_expr_free(expr);
		return (SQL_ERR_NOMEM);
	}

	return (SQL_OK);
}

static int parse_and(SQL_TOKENSTREAM *ts, SQL_EXPR **exprp) {
	SQL_EXPR *left, *right;
	int ret;

	ret = parse_not(ts, &left);
	if (ret != SQL_OK)
		return (ret);

	while (tok_is_kw(peek(ts, 0), KW_AND)) {
		sql_ts_consume(ts);
		ret = parse_not(ts, &right);
		if (ret != SQL_OK) {
			sql_expr_free(left);
			return (ret);
		}
		left = sql_expr_binary(OP_AND, left, right);
		if (left == NULL) {
			sql_expr_free(right);
			return (SQL_ERR_NOMEM);
		}
	}

	*exprp = left;
	return (SQL_OK);
}

static int parse_expr(SQL_TOKENSTREAM *ts, SQL_EXPR **exprp) {
	SQL_EXPR *left, *right;
	int ret;

	ret = parse_and(ts, &left);
	if (ret != SQL_OK)
		return (ret);

	while (tok_is_kw(peek(ts, 0), KW_OR)) {
		sql_ts_consume(ts);
		ret = parse_and(ts, &right);
		if (ret != SQL_OK) {
			sql_expr_free(left);
			return (ret);
		}
		left = sql_expr_binary(OP_OR, left, right);
		if (left == NULL) {
			sql_expr_free(right);
			return (SQL_ERR_NOMEM);
		}
	}

	*exprp = left;
	return (SQL_OK);
}

/********************************************************************
 * Statements
 ********************************************************************/
static int parse_create_table(SQL_TOKENSTREAM *ts, SQL_STMT *stmt) {
	int ret;
	size_t i;
	sql_type_t ttype;
	size_t tlen;

	sql_ts_consume(ts);	/* CREATE */
	ret = expect_kw(ts, KW_TABLE);
	if (ret != SQL_OK)
		return (ret);

	if (is_kw(ts, KW_IF, 0)) {
		sql_ts_consume(ts);
		ret = expect_kw(ts, KW_NOT);
		if (ret != SQL_OK)
			return (ret);
		ret = expect_kw(ts, KW_EXISTS);
		if (ret != SQL_OK)
			return (ret);
		stmt->if_not_exists = 1;
	}

	ret = parse_ident(ts, &stmt->tablename, &stmt->ltablename);
	if (ret != SQL_OK)
		return (ret);

	ret = expect_punct(ts, "(");
	if (ret != SQL_OK)
		return (ret);

	for (;;) {
		if (tok_is_kw(peek(ts, 0), KW_PRIMARY) ||
		    tok_is_kw(peek(ts, 0), KW_KEY)) {
			char *cname, *lname;
			size_t colindx;

			sql_ts_consume(ts);
			if (tok_is_kw(peek(ts, 0), KW_KEY))
				sql_ts_consume(ts);
			ret = expect_punct(ts, "(");
			if (ret != SQL_OK)
				return (ret);
			ret = parse_ident(ts, &cname, &lname);
			if (ret != SQL_OK)
				return (ret);
			ret = expect_punct(ts, ")");
			if (ret != SQL_OK) {
				sql_free(cname);
				sql_free(lname);
				return (ret);
			}
			for (colindx = 0; colindx < stmt->ncols; colindx++)
				if (strcmp(stmt->columns[colindx].lname,
				    lname) == 0)
					break;
			if (colindx == stmt->ncols) {
				sql_free(cname);
				sql_free(lname);
				return (SQL_ERR_NO_COLUMN);
			}
			stmt->columns[colindx].primary_key = 1;
			sql_free(cname);
			sql_free(lname);
		} else if (is_kw(ts, KW_ADD, 0)) {
			/* MySQL style noise, ignore it. */
			sql_ts_consume(ts);
			continue;
		} else {
			if (stmt->ncols >= SQL_MAX_COLUMNS)
				return (SQL_ERR_LIMIT);
			ret = grow_vec((void **)&stmt->columns,
			    &stmt->capcols, stmt->ncols + 1,
			    sizeof(SQL_COLUMN));
			if (ret != SQL_OK)
				return (ret);
			sql_column_init(&stmt->columns[stmt->ncols]);
			ret = parse_column_def(ts,
			    &stmt->columns[stmt->ncols]);
			if (ret != SQL_OK)
				return (ret);
			stmt->ncols++;
		}

		if (accept_punct(ts, ","))
			continue;
		break;
	}

	ret = expect_punct(ts, ")");
	if (ret != SQL_OK)
		return (ret);

	/* Table options such as ENGINE=... are out of scope. */
	if (peek(ts, 0)->type == TK_IDENT && peek(ts, 0)->kw == KW_NONE)
		return (SQL_ERR_FEATURE);

	if (stmt->ncols == 0)
		return (SQL_ERR_PARSE);

	for (i = 0; i < stmt->ncols; i++) {
		size_t j;

		for (j = i + 1; j < stmt->ncols; j++)
			if (strcmp(stmt->columns[i].lname,
			    stmt->columns[j].lname) == 0)
				return (SQL_ERR_PARSE);
		ttype = stmt->columns[i].type;
		tlen = stmt->columns[i].length;
		if (SQL_TYPE_HAS_LENGTH(ttype) && tlen == 0)
			return (SQL_ERR_PARSE);
		if (stmt->columns[i].has_default) {
			SQL_VALUE conv;

			sql_value_init(&conv);
			ret = sql_value_cast(&stmt->columns[i].defval,
			    ttype, tlen, &conv);
			if (ret != SQL_OK) {
				sql_value_clear(&conv);
				return (SQL_ERR_TYPE);
			}
			sql_value_clear(&stmt->columns[i].defval);
			ret = sql_value_copy(&conv,
			    &stmt->columns[i].defval);
			sql_value_clear(&conv);
			if (ret != SQL_OK)
				return (ret);
		}
	}

	return (SQL_OK);
}

static int parse_drop_table(SQL_TOKENSTREAM *ts, SQL_STMT *stmt) {
	int ret;

	sql_ts_consume(ts);	/* DROP */
	ret = expect_kw(ts, KW_TABLE);
	if (ret != SQL_OK)
		return (ret);
	if (is_kw(ts, KW_IF, 0)) {
		sql_ts_consume(ts);
		ret = expect_kw(ts, KW_EXISTS);
		if (ret != SQL_OK)
			return (ret);
		stmt->if_exists = 1;
	}

	return (parse_ident(ts, &stmt->tablename, &stmt->ltablename));
}

static int parse_insert(SQL_TOKENSTREAM *ts, SQL_STMT *stmt) {
	SQL_EXPRLIST *row;
	int ret;

	sql_ts_consume(ts);	/* INSERT */
	ret = expect_kw(ts, KW_INTO);
	if (ret != SQL_OK)
		return (ret);
	ret = parse_ident(ts, &stmt->tablename, &stmt->ltablename);
	if (ret != SQL_OK)
		return (ret);

	if (accept_punct(ts, "(")) {
		for (;;) {
			char *name, *lname;
			size_t cnt;

			ret = parse_ident(ts, &name, &lname);
			if (ret != SQL_OK)
				return (ret);
			cnt = stmt->ninscols;
			if (cnt >= SQL_MAX_COLUMNS) {
				sql_free(name);
				sql_free(lname);
				return (SQL_ERR_LIMIT);
			}
			ret = grow_vec((void **)&stmt->inscols,
			    &stmt->capins, cnt + 1, sizeof(char *));
			if (ret == SQL_OK)
				ret = grow_vec((void **)&stmt->linscols,
				    &stmt->caplins, cnt + 1,
				    sizeof(char *));
			if (ret != SQL_OK) {
				sql_free(name);
				sql_free(lname);
				return (ret);
			}
			stmt->inscols[cnt] = name;
			stmt->linscols[cnt] = lname;
			stmt->ninscols = cnt + 1;
			if (accept_punct(ts, ","))
				continue;
			ret = expect_punct(ts, ")");
			if (ret != SQL_OK)
				return (ret);
			break;
		}
	}

	ret = expect_kw(ts, KW_VALUES);
	if (ret != SQL_OK)
		return (ret);

	for (;;) {
		size_t cnt;

		ret = parse_exprlist(ts, &row);
		if (ret != SQL_OK)
			return (ret);
		cnt = stmt->nrows_ins;
		ret = grow_vec((void **)&stmt->insert_rows,
		    &stmt->caprows_ins, cnt + 1, sizeof(SQL_EXPRLIST *));
		if (ret != SQL_OK) {
			sql_exprlist_free(row);
			return (ret);
		}
		stmt->insert_rows[cnt] = row;
		stmt->nrows_ins = cnt + 1;
		if (accept_punct(ts, ","))
			continue;
		break;
	}

	return (SQL_OK);
}

static int parse_select(SQL_TOKENSTREAM *ts, SQL_STMT *stmt) {
	SQL_SELECT_ITEM *item;
	SQL_EXPR *expr;
	int ret;

	sql_ts_consume(ts);	/* SELECT */

	if (accept_kw(ts, KW_DISTINCT))
		stmt->distinct = 1;
	else if (is_kw(ts, KW_ALL, 0))
		sql_ts_consume(ts);

	if (tok_is_punct(peek(ts, 0), "*")) {
		sql_ts_consume(ts);
		stmt->select_star = 1;
	} else {
		for (;;) {
			char *alias, *lalias;
			size_t cnt;

			alias = NULL;
			lalias = NULL;
			ret = parse_expr(ts, &expr);
			if (ret != SQL_OK)
				return (ret);
			if (accept_kw(ts, KW_AS) || (peek(ts, 0)->type ==
			    TK_IDENT && peek(ts, 0)->kw == KW_NONE)) {
				ret = parse_ident(ts, &alias, &lalias);
				if (ret != SQL_OK) {
					sql_expr_free(expr);
					return (ret);
				}
			}
			cnt = stmt->nselect;
			ret = grow_vec((void **)&stmt->select_items,
			    &stmt->capselect, cnt + 1,
			    sizeof(SQL_SELECT_ITEM));
			if (ret != SQL_OK) {
				sql_expr_free(expr);
				sql_free(alias);
				sql_free(lalias);
				return (ret);
			}
			item = &stmt->select_items[cnt];
			item->expr = expr;
			item->alias = alias;
			item->lalias = lalias;
			stmt->nselect = cnt + 1;
			if (accept_punct(ts, ","))
				continue;
			break;
		}
	}

	ret = expect_kw(ts, KW_FROM);
	if (ret != SQL_OK)
		return (ret);
	ret = parse_ident(ts, &stmt->tablename, &stmt->ltablename);
	if (ret != SQL_OK)
		return (ret);
	/* FROM aliases would shadow the table name, reject them. */
	if (peek(ts, 0)->type == TK_IDENT && peek(ts, 0)->kw == KW_NONE)
		return (SQL_ERR_FEATURE);

	if (accept_kw(ts, KW_WHERE)) {
		ret = parse_expr(ts, &stmt->where);
		if (ret != SQL_OK)
			return (ret);
	}

	if (accept_kw(ts, KW_ORDER)) {
		ret = expect_kw(ts, KW_BY);
		if (ret != SQL_OK)
			return (ret);
		for (;;) {
			SQL_ORDER_ITEM *oitem;
			size_t cnt;

			ret = parse_expr(ts, &expr);
			if (ret != SQL_OK)
				return (ret);
			cnt = stmt->norder;
			ret = grow_vec((void **)&stmt->order_by,
			    &stmt->caporder, cnt + 1,
			    sizeof(SQL_ORDER_ITEM));
			if (ret != SQL_OK) {
				sql_expr_free(expr);
				return (ret);
			}
			oitem = &stmt->order_by[cnt];
			oitem->expr = expr;
			oitem->desc = 0;
			stmt->norder = cnt + 1;
			if (accept_kw(ts, KW_DESC))
				oitem->desc = 1;
			else if (accept_kw(ts, KW_ASC))
				oitem->desc = 0;
			if (accept_punct(ts, ","))
				continue;
			break;
		}
	}

	if (accept_kw(ts, KW_LIMIT)) {
		ret = parse_expr(ts, &stmt->limit);
		if (ret != SQL_OK)
			return (ret);
		if (accept_kw(ts, KW_OFFSET)) {
			ret = parse_expr(ts, &stmt->offset);
			if (ret != SQL_OK)
				return (ret);
		}
	} else if (accept_kw(ts, KW_OFFSET)) {
		ret = parse_expr(ts, &stmt->offset);
		if (ret != SQL_OK)
			return (ret);
		ret = expect_kw(ts, KW_LIMIT);
		if (ret != SQL_OK)
			return (ret);
	}

	return (SQL_OK);
}

static int parse_update(SQL_TOKENSTREAM *ts, SQL_STMT *stmt) {
	int ret;

	sql_ts_consume(ts);	/* UPDATE */
	ret = parse_ident(ts, &stmt->tablename, &stmt->ltablename);
	if (ret != SQL_OK)
		return (ret);
	ret = expect_kw(ts, KW_SET);
	if (ret != SQL_OK)
		return (ret);

	for (;;) {
		SQL_SET_ITEM *sitem;
		size_t cnt;

		cnt = stmt->nset;
		ret = grow_vec((void **)&stmt->set_items, &stmt->capset,
		    cnt + 1, sizeof(SQL_SET_ITEM));
		if (ret != SQL_OK)
			return (ret);
		sitem = &stmt->set_items[cnt];
		sitem->colname = NULL;
		sitem->lcolname = NULL;
		sitem->expr = NULL;
		sitem->resolved = -1;
		stmt->nset = cnt + 1;
		ret = parse_ident(ts, &sitem->colname, &sitem->lcolname);
		if (ret != SQL_OK)
			return (ret);
		ret = expect_punct(ts, "=");
		if (ret != SQL_OK)
			return (ret);
		ret = parse_expr(ts, &sitem->expr);
		if (ret != SQL_OK)
			return (ret);
		if (accept_punct(ts, ","))
			continue;
		break;
	}

	if (accept_kw(ts, KW_WHERE))
		return (parse_expr(ts, &stmt->where));

	return (SQL_OK);
}

static int parse_delete(SQL_TOKENSTREAM *ts, SQL_STMT *stmt) {
	int ret;

	sql_ts_consume(ts);	/* DELETE */
	ret = expect_kw(ts, KW_FROM);
	if (ret != SQL_OK)
		return (ret);
	ret = parse_ident(ts, &stmt->tablename, &stmt->ltablename);
	if (ret != SQL_OK)
		return (ret);
	if (accept_kw(ts, KW_WHERE))
		return (parse_expr(ts, &stmt->where));

	return (SQL_OK);
}

/*
 * Parse exactly one statement, consuming its terminating semicolon.
 */
int sql_parse(SQL_TOKENSTREAM *ts, SQL_STMT **stmtp) {
	SQL_TOKEN *tk;
	SQL_STMT *stmt;
	sql_stmt_type type;
	int ret;

	if (ts == NULL || stmtp == NULL)
		return (SQL_ERR_ARG);
	*stmtp = NULL;

	tk = peek(ts, 0);
	if (tok_is_eof(tk))
		return (SQL_ERR_PARSE);

	switch (tk->kw) {
		case KW_CREATE:
			type = STMT_CREATE_TABLE;
			break;
		case KW_DROP:
			type = STMT_DROP_TABLE;
			break;
		case KW_INSERT:
			type = STMT_INSERT;
			break;
		case KW_SELECT:
			type = STMT_SELECT;
			break;
		case KW_UPDATE:
			type = STMT_UPDATE;
			break;
		case KW_DELETE:
			type = STMT_DELETE;
			break;
		default:
			return (tk->type == TK_IDENT ?
			    SQL_ERR_FEATURE : SQL_ERR_PARSE);
	}

	ret = sql_stmt_alloc(type, &stmt);
	if (ret != SQL_OK)
		return (ret);

	switch (type) {
		case STMT_CREATE_TABLE:
			ret = parse_create_table(ts, stmt);
			break;
		case STMT_DROP_TABLE:
			ret = parse_drop_table(ts, stmt);
			break;
		case STMT_INSERT:
			ret = parse_insert(ts, stmt);
			break;
		case STMT_SELECT:
			ret = parse_select(ts, stmt);
			break;
		case STMT_UPDATE:
			ret = parse_update(ts, stmt);
			break;
		case STMT_DELETE:
			ret = parse_delete(ts, stmt);
			break;
		default:
			ret = SQL_ERR_FEATURE;
			break;
	}

	if (ret != SQL_OK) {
		sql_stmt_free(stmt);
		return (ret);
	}

	/*
	 * A semicolon terminates the statement, EOF terminates too.  Any
	 * remaining tokens belong to the next statement and are checked
	 * by sql_session_exec(), which only accepts a single statement.
	 */
	(void)accept_punct(ts, ";");

	*stmtp = stmt;
	return (SQL_OK);
}
