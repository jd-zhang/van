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
 * Expression evaluation.
 *
 * SQL logic is three valued: TRUE, FALSE and UNKNOWN (SQL NULL).  The
 * value 1 is TRUE, 0 is FALSE and -1 is UNKNOWN.
 */
#define	TRI_FALSE	0
#define	TRI_TRUE	1
#define	TRI_UNKNOWN	(-1)

int sql_schema_findcol(SQL_SCHEMA *schema, const char *lname,
    size_t *indxp) {
	size_t i;

	if (schema == NULL || lname == NULL)
		return (SQL_ERR_ARG);
	for (i = 0; i < schema->ncols; i++) {
		if (schema->cols[i].lname != NULL &&
		    strcmp(schema->cols[i].lname, lname) == 0) {
			if (indxp != NULL)
				*indxp = i;
			return (SQL_OK);
		}
	}

	return (SQL_ERR_NO_COLUMN);
}

static int
sql_seterr_notable(SQL_SESSION *sess, const char *lname) {

	(void)snprintf(sess->errmsg, sizeof(sess->errmsg),
	    "Error: no such table '%s'", lname);
	return (SQL_ERR_NO_TABLE);
}

static int result_bool(SQL_VALUE *res, int val) {
	int ret;

	ret = sql_value_set_int(res, SQL_T_INT, (int64)val);
	if (ret != SQL_OK)
		return (ret);
	if (val == TRI_UNKNOWN)
		res->isnull = 1;

	return (SQL_OK);
}

static int is_stringish(const SQL_VALUE *v) {

	return (SQL_TYPE_IS_STRING(v->type) ||
	    SQL_TYPE_IS_BINARY(v->type));
}

/*
 * Compare two non NULL values, honouring the loose affinity rules of
 * MySQL: when a number meets a string the string is evaluated as a
 * number, only if that fails are they compared by class.
 */
static int eval_compare(const SQL_VALUE *a, const SQL_VALUE *b, int *cmp) {
	SQL_VALUE tmp;
	int ret, ok;

	if (a->isnull || b->isnull)
		return (SQL_ERR_TYPE);

	sql_value_init(&tmp);
	ret = SQL_OK;
	ok = 0;

	if (SQL_TYPE_IS_NUMERIC(a->type) && is_stringish(b)) {
		ret = sql_value_cast(b, SQL_T_DOUBLE, 0, &tmp);
		if (ret == SQL_OK)
			ok = 1;
	} else if (SQL_TYPE_IS_NUMERIC(b->type) && is_stringish(a)) {
		ret = sql_value_cast(a, SQL_T_DOUBLE, 0, &tmp);
		if (ret == SQL_OK)
			ok = 1;
	}

	if (ok) {
		double d1, d2;
		const SQL_VALUE *s1, *s2;

		s1 = (SQL_TYPE_IS_NUMERIC(a->type) ? a : b);
		s2 = &tmp;
		ret = sql_value_numeric(s1, &d1);
		if (ret == SQL_OK)
			ret = sql_value_numeric(s2, &d2);
		if (ret == SQL_OK) {
			*cmp = (d1 < d2 ? -1 : (d1 > d2 ? 1 : 0));
			sql_value_clear(&tmp);
			return (SQL_OK);
		}
		sql_value_clear(&tmp);
		return (ret);
	}
	(void)ret;
	sql_value_clear(&tmp);

	if (SQL_TYPE_IS_TEMPORAL(a->type) && is_stringish(b)) {
		if (sql_value_cast(b, a->type, 0, &tmp) == SQL_OK) {
			*cmp = sql_value_cmp(a, &tmp);
			sql_value_clear(&tmp);
			return (SQL_OK);
		}
	} else if (SQL_TYPE_IS_TEMPORAL(b->type) && is_stringish(a)) {
		if (sql_value_cast(a, b->type, 0, &tmp) == SQL_OK) {
			*cmp = sql_value_cmp(&tmp, b);
			sql_value_clear(&tmp);
			return (SQL_OK);
		}
	}
	sql_value_clear(&tmp);

	*cmp = sql_value_cmp(a, b);
	return (SQL_OK);
}

/*
 * LIKE pattern matching with % (any run of characters) and _ (exactly
 * one character).
 */
static int like_match(const unsigned char *s, size_t slen,
    const unsigned char *p, size_t plen) {
	size_t i, j;

	if (plen == 0)
		return (slen == 0 ? 1 : 0);
	if (slen == 0) {
		/* Only a run of '%' can still match the empty string. */
		for (j = 0; j < plen; j++)
			if (p[j] != '%')
				return (0);
		return (1);
	}

	if (*p == '%') {
		/* Try every possible split point of the subject. */
		for (i = 0; i <= slen; i++) {
			if (like_match(s + i, slen - i, p + 1, plen - 1) != 0)
				return (1);
		}
		return (0);
	}
	if (*p == '_' ||
	    (unsigned char)tolower(*p) == (unsigned char)tolower(*s)) {
		return (like_match(s + 1, slen - 1, p + 1, plen - 1));
	}

	return (0);
}

static int eval_like(const SQL_VALUE *a, const SQL_VALUE *b, int negate,
    int *resp) {
	char *s1, *s2;
	int ret, match;

	s1 = NULL;
	s2 = NULL;
	ret = sql_value_tostring(a, &s1);
	if (ret != SQL_OK)
		return (ret);
	ret = sql_value_tostring(b, &s2);
	if (ret != SQL_OK) {
		sql_free(s1);
		return (ret);
	}
	if (s1 == NULL || s2 == NULL) {
		sql_free(s1);
		sql_free(s2);
		*resp = TRI_UNKNOWN;
		return (SQL_OK);
	}
	match = like_match((const unsigned char *)s1, strlen(s1),
	    (const unsigned char *)s2, strlen(s2));
	sql_free(s1);
	sql_free(s2);
	*resp = (match ? TRI_TRUE : TRI_FALSE);
	if (negate && *resp != TRI_UNKNOWN)
		*resp = (*resp == TRI_TRUE ? TRI_FALSE : TRI_TRUE);

	return (SQL_OK);
}

/* Derive a tri-state truth value from any value. */
static int tri_from_value(const SQL_VALUE *v) {
	double d;

	if (v == NULL || v->isnull)
		return (TRI_UNKNOWN);
	if (SQL_TYPE_IS_NUMERIC(v->type)) {
		if (sql_value_numeric(v, &d) != SQL_OK)
			return (TRI_TRUE);
		return (d != 0 ? TRI_TRUE : TRI_FALSE);
	}
	if (is_stringish(v))
		return (v->slen != 0 ? TRI_TRUE : TRI_FALSE);

	return (v->ival != 0 ? TRI_TRUE : TRI_FALSE);
}

static int eval_arith(sql_op op, const SQL_VALUE *a, const SQL_VALUE *b,
    SQL_VALUE *res) {
	double da, db, dr;
	int64 ia, ib;
	int ret, bothint;

	if (a->isnull || b->isnull)
		return (sql_value_set_null(res, SQL_T_NULL));

	/* '+' doubles as the string concatenation operator. */
	if (op == OP_PLUS && is_stringish(a) && is_stringish(b)) {
		unsigned char *buf;
		size_t len;

		len = a->slen + b->slen;
		buf = sql_calloc(len + 1, 1);
		if (buf == NULL)
			return (SQL_ERR_NOMEM);
		if (a->slen > 0)
			memcpy(buf, a->sval, a->slen);
		if (b->slen > 0)
			memcpy(buf + a->slen, b->sval, b->slen);
		ret = sql_value_set_string(res, SQL_T_VARCHAR, buf, len);
		sql_free(buf);
		return (ret);
	}

	ret = sql_value_numeric(a, &da);
	if (ret != SQL_OK)
		return (ret);
	ret = sql_value_numeric(b, &db);
	if (ret != SQL_OK)
		return (ret);
	bothint = (SQL_TYPE_IS_INT(a->type) && SQL_TYPE_IS_INT(b->type));

	switch (op) {
		case OP_PLUS:
			dr = da + db;
			break;
		case OP_MINUS:
			dr = da - db;
			break;
		case OP_MUL:
			dr = da * db;
			break;
		case OP_DIV:
			if (db == 0)
				return (sql_value_set_null(res, SQL_T_NULL));
			dr = da / db;
			bothint = (bothint && (da - (double)(int64)da) == 0 &&
			    (db - (double)(int64)db) == 0 &&
			    ((int64)da % (int64)db) == 0);
			break;
		case OP_MOD:
			if ((int64)db == 0)
				return (sql_value_set_null(res, SQL_T_NULL));
			if (bothint)
				return (sql_value_set_int(res, SQL_T_BIGINT,
				    (int64)da % (int64)db));
			return (sql_value_set_double(res, SQL_T_DOUBLE,
			    fmod(da, db)));
		default:
			return (SQL_ERR_TYPE);
	}

	if (bothint) {
		ia = (int64)dr;
		ib = ia;
		(void)ib;
		return (sql_value_set_int(res, SQL_T_BIGINT, ia));
	}

	return (sql_value_set_double(res, SQL_T_DOUBLE, dr));
}

/*
 * Evaluate an expression in the context of 'row' (which may be NULL when
 * there is no current row, e.g. for LIMIT values).
 */
int sql_expr_eval(SQL_SESSION *sess, SQL_SCHEMA *schema,
    const SQL_VALUE *row, const SQL_EXPR *expr, SQL_VALUE *res) {
	SQL_VALUE a, b, *tmp;
	size_t indx;
	int ret, cmp;

	if (expr == NULL || res == NULL)
		return (SQL_ERR_ARG);

	sql_value_init(&a);
	sql_value_init(&b);
	cmp = 0;
	tmp = NULL;

	switch (expr->xtype) {
		case EX_LITERAL:
			return (sql_value_copy(&expr->lit, res));

		case EX_FUNC:
			if (expr->kw == KW_CURRENT_DATE) {
				int64 days;
				ret = sql_now_date(&days);
				if (ret != SQL_OK)
					return (ret);
				return (sql_value_set_int(res, SQL_T_DATE,
				    days));
			}
			if (expr->kw == KW_CURRENT_TIME) {
				int64 usec;
				ret = sql_now_time(&usec);
				if (ret != SQL_OK)
					return (ret);
				return (sql_value_set_int(res, SQL_T_TIME,
				    usec));
			}
			if (expr->kw == KW_CURRENT_TIMESTAMP) {
				int64 msec;
				ret = sql_now_timestamp(&msec);
				if (ret != SQL_OK)
					return (ret);
				return (sql_value_set_int(res,
				    SQL_T_TIMESTAMP, msec));
			}
			return (SQL_ERR_FEATURE);

		case EX_COLUMN:
			if (schema == NULL || row == NULL)
				return (SQL_ERR_NO_COLUMN);
			ret = sql_schema_findcol(schema, expr->lname, &indx);
			if (ret != SQL_OK)
				return (ret);
			return (sql_value_copy(&row[indx], res));

		case EX_UNARY:
			ret = sql_expr_eval(sess, schema, row, expr->left, &a);
			if (ret != SQL_OK)
				goto err;
			if (a.isnull)
				return (sql_value_set_null(res, a.type));
			ret = sql_value_numeric(&a, &b.dval);
			if (ret != SQL_OK)
				goto err;
			if (expr->op == OP_MINUS)
				b.dval = -b.dval;
			if (SQL_TYPE_IS_FLOAT(a.type))
				ret = sql_value_set_double(res, a.type,
				    b.dval);
			else
				ret = sql_value_set_int(res, a.type,
				    (int64)b.dval);
			sql_value_clear(&a);
			sql_value_clear(&b);
			return (ret);

		case EX_BINARY:
			if (expr->op == OP_AND || expr->op == OP_OR)
				break;	/* Handled as tri-state below. */
			ret = sql_expr_eval(sess, schema, row, expr->left, &a);
			if (ret != SQL_OK)
				goto err;
			ret = sql_expr_eval(sess, schema, row, expr->right,
			    &b);
			if (ret != SQL_OK)
				goto err;

			switch (expr->op) {
				case OP_EQ:
				case OP_NE:
				case OP_LT:
				case OP_LE:
				case OP_GT:
				case OP_GE:
					if (a.isnull || b.isnull) {
						sql_value_clear(&a);
						sql_value_clear(&b);
						return (result_bool(res,
						    TRI_UNKNOWN));
					}
					ret = eval_compare(&a, &b, &cmp);
					if (ret != SQL_OK)
						goto err;
					switch (expr->op) {
						case OP_EQ:
							cmp = (cmp == 0);
							break;
						case OP_NE:
							cmp = (cmp != 0);
							break;
						case OP_LT:
							cmp = (cmp < 0);
							break;
						case OP_LE:
							cmp = (cmp <= 0);
							break;
						case OP_GT:
							cmp = (cmp > 0);
							break;
						case OP_GE:
							cmp = (cmp >= 0);
							break;
						default:
							break;
					}
					sql_value_clear(&a);
					sql_value_clear(&b);
					return (result_bool(res,
					    cmp ? TRI_TRUE : TRI_FALSE));
				default:
					break;
			}

			/* Arithmetic */
			ret = eval_arith(expr->op, &a, &b, res);
			sql_value_clear(&a);
			sql_value_clear(&b);
			return (ret);

		case EX_NOT:
		case EX_ISNULL:
		case EX_IN:
		case EX_BETWEEN:
		case EX_LIKE:
			break;

		default:
			return (SQL_ERR_FEATURE);
	}

	/* Everything below is tri-state logic. */
	switch (expr->xtype) {
		case EX_BINARY:
		{
			int ta, tb;

			if (expr->op != OP_AND && expr->op != OP_OR)
				return (SQL_ERR_FEATURE);
			ret = sql_expr_test(sess, schema, row, expr->left,
			    &ta);
			if (ret != SQL_OK)
				return (ret);
			ret = sql_expr_test(sess, schema, row, expr->right,
			    &tb);
			if (ret != SQL_OK)
				return (ret);
			if (expr->op == OP_AND) {
				if (ta == TRI_FALSE || tb == TRI_FALSE)
					return (result_bool(res, TRI_FALSE));
				if (ta == TRI_UNKNOWN || tb == TRI_UNKNOWN)
					return (result_bool(res,
					    TRI_UNKNOWN));
				return (result_bool(res, TRI_TRUE));
			}
			if (ta == TRI_TRUE || tb == TRI_TRUE)
				return (result_bool(res, TRI_TRUE));
			if (ta == TRI_UNKNOWN || tb == TRI_UNKNOWN)
				return (result_bool(res, TRI_UNKNOWN));
			return (result_bool(res, TRI_FALSE));
		}

		case EX_NOT:
		{
			int tv;

			ret = sql_expr_test(sess, schema, row, expr->left,
			    &tv);
			if (ret != SQL_OK)
				return (ret);
			if (tv == TRI_UNKNOWN)
				return (result_bool(res, TRI_UNKNOWN));
			return (result_bool(res,
			    tv == TRI_TRUE ? TRI_FALSE : TRI_TRUE));
		}

		case EX_ISNULL:
		{
			int tv;

			ret = sql_expr_eval(sess, schema, row, expr->left,
			    &a);
			if (ret != SQL_OK)
				goto err;
			tv = (a.isnull ? TRI_TRUE : TRI_FALSE);
			if (expr->negate)
				tv = (tv == TRI_TRUE ? TRI_FALSE : TRI_TRUE);
			sql_value_clear(&a);
			return (result_bool(res, tv));
		}

		case EX_IN:
		{
			size_t i;
			int found, unknown;

			if (expr->list == NULL)
				return (SQL_ERR_ARG);
			ret = sql_expr_eval(sess, schema, row, expr->left,
			    &a);
			if (ret != SQL_OK)
				goto err;
			if (a.isnull) {
				sql_value_clear(&a);
				return (result_bool(res, TRI_UNKNOWN));
			}
			found = 0;
			unknown = 0;
			cmp = TRI_FALSE;
			for (i = 0; i < expr->list->nitems; i++) {
				ret = sql_expr_eval(sess, schema, row,
				    expr->list->items[i], &b);
				if (ret != SQL_OK) {
					sql_value_clear(&a);
					goto err;
				}
				if (b.isnull) {
					unknown = 1;
					sql_value_clear(&b);
					continue;
				}
				ret = eval_compare(&a, &b, &cmp);
				sql_value_clear(&b);
				if (ret != SQL_OK) {
					sql_value_clear(&a);
					goto err;
				}
				if (cmp == 0) {
					found = 1;
					break;
				}
			}
			sql_value_clear(&a);
			if (found)
				cmp = TRI_TRUE;
			else if (unknown)
				cmp = TRI_UNKNOWN;
			else
				cmp = TRI_FALSE;
			if (expr->negate && cmp != TRI_UNKNOWN)
				cmp = (cmp == TRI_TRUE ? TRI_FALSE :
				    TRI_TRUE);
			return (result_bool(res, cmp));
		}

		case EX_BETWEEN:
		{
			int ta, tb, tri;

			ret = sql_expr_eval(sess, schema, row, expr->left,
			    &a);
			if (ret != SQL_OK)
				goto err;
			if (a.isnull) {
				sql_value_clear(&a);
				return (result_bool(res, TRI_UNKNOWN));
			}
			sql_value_clear(&a);
			ret = sql_expr_test(sess, schema, row, expr->right,
			    &ta);
			if (ret != SQL_OK)
				return (ret);
			ret = sql_expr_test(sess, schema, row, expr->third,
			    &tb);
			if (ret != SQL_OK)
				return (ret);
			/*
			 *	bool >= low AND bool <= high, evaluated by
			 * reusing the comparison nodes below.
			 */
			tri = TRI_UNKNOWN;
			{
				SQL_EXPR ge, le;
				memset(&ge, 0, sizeof(ge));
				memset(&le, 0, sizeof(le));
				ge.xtype = EX_BINARY;
				ge.op = OP_GE;
				ge.left = expr->left;
				ge.right = expr->right;
				le.xtype = EX_BINARY;
				le.op = OP_LE;
				le.left = expr->left;
				le.right = expr->third;
				ret = sql_expr_test(sess, schema, row, &ge,
				    &ta);
				if (ret != SQL_OK)
					return (ret);
				ret = sql_expr_test(sess, schema, row, &le,
				    &tb);
				if (ret != SQL_OK)
					return (ret);
			}
			if (ta == TRI_FALSE || tb == TRI_FALSE)
				tri = TRI_FALSE;
			else if (ta == TRI_UNKNOWN || tb == TRI_UNKNOWN)
				tri = TRI_UNKNOWN;
			else
				tri = TRI_TRUE;
			if (expr->negate && tri != TRI_UNKNOWN)
				tri = (tri == TRI_TRUE ? TRI_FALSE :
				    TRI_TRUE);
			return (result_bool(res, tri));
		}

		case EX_LIKE:
		{
			int tv;

			ret = sql_expr_eval(sess, schema, row, expr->left,
			    &a);
			if (ret != SQL_OK)
				goto err;
			ret = sql_expr_eval(sess, schema, row, expr->right,
			    &b);
			if (ret != SQL_OK) {
				sql_value_clear(&a);
				goto err;
			}
			if (a.isnull || b.isnull) {
				sql_value_clear(&a);
				sql_value_clear(&b);
				return (result_bool(res, TRI_UNKNOWN));
			}
			ret = eval_like(&a, &b, expr->negate, &tv);
			sql_value_clear(&a);
			sql_value_clear(&b);
			if (ret != SQL_OK)
				return (ret);
			return (result_bool(res, tv));
		}

		default:
			break;
	}

	return (SQL_ERR_FEATURE);

err:
	sql_value_clear(&a);
	sql_value_clear(&b);
	if (tmp != NULL)
		sql_value_clear(tmp);
	return (ret);
}

/*
 * Evaluate an expression as a predicate, '*tv' is one of the TRI_*
 * values.
 */
int sql_expr_test(SQL_SESSION *sess, SQL_SCHEMA *schema,
    const SQL_VALUE *row, const SQL_EXPR *expr, int *tv) {
	SQL_VALUE v;
	int ret;

	if (expr == NULL || tv == NULL)
		return (SQL_ERR_ARG);
	*tv = TRI_FALSE;

	sql_value_init(&v);
	ret = sql_expr_eval(sess, schema, row, expr, &v);
	if (ret != SQL_OK) {
		sql_value_clear(&v);
		return (ret);
	}
	*tv = tri_from_value(&v);
	sql_value_clear(&v);

	return (SQL_OK);
}

/********************************************************************
 * Row scanning helpers
 *
 * Row ids are the keys, are ascending in insertion order and are never
 * reused, so a forward table->traverse() yields the rows in insertion
 * order.  SELECT scans the table that way; the helpers below are still
 * used by the statements that address one row at a time.
 ********************************************************************/
static int fetch_row(SQL_SESSION *sess, SQL_TABLE *tb, uint64 rowid,
    SQL_VALUE *vals, int *foundp) {
	void *data;
	size_t len;
	int ret;

	*foundp = 0;
	ret = sql_kv_get(sess, tb, rowid, &data, &len);
	if (ret != SQL_OK)
		return (ret);
	if (data == NULL)
		return (SQL_OK);

	ret = sql_row_decode(tb->schema, data, len, vals);
	sql_kv_free(data);
	if (ret != SQL_OK)
		return (ret);
	*foundp = 1;

	return (SQL_OK);
}

static int rowid_key_free(SQL_SESSION *sess, SQL_TABLE *tb, uint64 rowid) {
	void *data;
	size_t len;
	int ret;

	ret = sql_kv_get(sess, tb, rowid, &data, &len);
	if (ret != SQL_OK)
		return (0);
	if (data == NULL)
		return (1);
	sql_kv_free(data);

	return (0);
}

/*
 * Allocate a value array matching the schema, all values initialized.
 */
static SQL_VALUE *row_values_alloc(size_t ncols) {
	SQL_VALUE *vals;
	size_t i;

	vals = (SQL_VALUE *)sql_calloc(ncols, sizeof(SQL_VALUE));
	if (vals == NULL)
		return (NULL);
	for (i = 0; i < ncols; i++)
		sql_value_init(&vals[i]);

	return (vals);
}

/********************************************************************
 * CREATE TABLE
 ********************************************************************/
static int build_schema(SQL_STMT *stmt, SQL_SCHEMA **schemap) {
	SQL_SCHEMA *schema;
	size_t i;
	int ret;

	schema = sql_calloc(1, sizeof(SQL_SCHEMA));
	if (schema == NULL)
		return (SQL_ERR_NOMEM);
	schema->name = sql_strdup(stmt->tablename);
	schema->lname = sql_strlower(stmt->ltablename,
	    strlen(stmt->ltablename));
	schema->cols = (SQL_COLUMN *)sql_calloc(stmt->ncols,
	    sizeof(SQL_COLUMN));
	if (schema->name == NULL || schema->lname == NULL ||
	    schema->cols == NULL) {
		sql_schema_free(schema);
		return (SQL_ERR_NOMEM);
	}
	for (i = 0; i < stmt->ncols; i++)
		sql_column_init(&schema->cols[i]);
	schema->capacity = stmt->ncols;
	schema->ncols = stmt->ncols;
	schema->pk_indx = -1;
	schema->next_rowid = 1;

	for (i = 0; i < stmt->ncols; i++) {
		SQL_COLUMN *src, *dst;

		src = &stmt->columns[i];
		dst = &schema->cols[i];
		dst->name = sql_strdup(src->name);
		dst->lname = sql_strdup(src->lname);
		dst->type = src->type;
		dst->length = src->length;
		dst->not_null = src->not_null;
		dst->primary_key = src->primary_key;
		dst->has_default = src->has_default;
		if (dst->name == NULL || dst->lname == NULL) {
			sql_schema_free(schema);
			return (SQL_ERR_NOMEM);
		}
		ret = sql_value_copy(&src->defval, &dst->defval);
		if (ret != SQL_OK) {
			sql_schema_free(schema);
			return (ret);
		}
		if (src->primary_key) {
			if (schema->pk_indx >= 0) {
				/* Composite keys need an index. */
				sql_schema_free(schema);
				return (SQL_ERR_FEATURE);
			}
			schema->pk_indx = (int)i;
		}
	}

	/* A rowid alias is only possible for integer primary keys. */
	if (schema->pk_indx >= 0 &&
	    !SQL_TYPE_IS_INT(schema->cols[schema->pk_indx].type))
		schema->pk_indx = -1;

	*schemap = schema;
	return (SQL_OK);
}

static int exec_create_table(SQL_SESSION *sess, SQL_STMT *stmt, FILE *out) {
	SQL_SCHEMA *schema;
	SQL_TABLE *tb;
	int ret;

	(void)out;
	schema = NULL;
	tb = NULL;

	if (sql_session_find(sess, stmt->ltablename) != NULL) {
		if (stmt->if_not_exists)
			return (SQL_OK);
		return (SQL_ERR_DUP_TABLE);
	}

	ret = build_schema(stmt, &schema);
	if (ret != SQL_OK)
		return (ret);
	ret = sql_table_alloc(sess, schema->name, schema, &tb);
	if (ret != SQL_OK) {
		sql_schema_free(schema);
		return (ret);
	}
	ret = sql_session_addtable(sess, tb);
	if (ret != SQL_OK) {
		sql_table_free(tb);
		return (ret);
	}
	ret = sql_table_open(sess, tb);
	if (ret != SQL_OK) {
		sql_session_deltable(sess, tb->lname);
		return (ret);
	}
	ret = sql_catalog_save_schema(sess, schema);
	if (ret != SQL_OK)
		return (ret);

	sess->rows_affected = 0;

	return (SQL_OK);
}

/********************************************************************
 * DROP TABLE
 ********************************************************************/
static int exec_drop_table(SQL_SESSION *sess, SQL_STMT *stmt, FILE *out) {
	SQL_TABLE *tb;
	uint64 rid, limit;
	int ret;

	(void)out;
	tb = sql_session_find(sess, stmt->ltablename);
	if (tb == NULL) {
		if (stmt->if_exists)
			return (SQL_OK);
		return (sql_seterr_notable(sess, stmt->ltablename));
	}

	/*
	 * Remove the rows before the table disappears: row ids start at
	 * one again for a table created later under the same name, so
	 * leaving the rows behind would resurrect them.
	 */
	ret = sql_table_ensure_open(sess, tb);
	if (ret != SQL_OK)
		return (ret);
	limit = tb->schema->next_rowid;
	for (rid = 1; rid < limit; rid++) {
		ret = sql_kv_del(sess, tb, rid);
		if (ret != SQL_OK)
			return (ret);
	}

	sess->rows_affected = (size_t)(limit - 1);

	return (sql_catalog_drop(sess, tb->lname));
}

/********************************************************************
 * INSERT
 ********************************************************************/
/*
 * Map the i-th value of an INSERT onto a schema column: the explicit
 * target column list when there is one, otherwise positional.
 */
static int insert_target_col(SQL_STMT *stmt, SQL_SCHEMA *schema, size_t i,
    size_t *cindxp) {

	if (stmt->ninscols == 0) {
		*cindxp = i;
		return (SQL_OK);
	}
	if (i >= stmt->ninscols)
		return (SQL_ERR_ARG);

	return (sql_schema_findcol(schema, stmt->linscols[i], cindxp));
}

static int insert_one_row(SQL_SESSION *sess, SQL_STMT *stmt, SQL_TABLE *tb,
    SQL_SCHEMA *schema, SQL_EXPRLIST *row, size_t *rowidp) {
	SQL_VALUE *vals, conv;
	size_t i, ncols;
	size_t *targets;
	uint64 rowid;
	unsigned char *buf;
	size_t blen;
	int ret, dirty;

	ncols = schema->ncols;
	buf = NULL;
	const char *vs = (row->nitems == 1) ? "value" : "values";
	targets = NULL;
	blen = 0;
	dirty = 0;
	rowid = 0;

	if (stmt->ninscols != 0) {
		if (row->nitems != stmt->ninscols) {
			(void)snprintf(sess->errmsg, sizeof(sess->errmsg),
			    "Error: column count mismatch, table '%s' lists "
			    "%zu columns but %zu %s provided",
			    stmt->ltablename,
			    stmt->ninscols, row->nitems, vs);
			return (SQL_ERR_COLUMN_COUNT);
		}
	} else if (row->nitems != ncols) {
		(void)snprintf(sess->errmsg, sizeof(sess->errmsg),
		    "Error: column count mismatch, table '%s' has %zu "
		    "columns, but %zu %s provided",
		    stmt->ltablename,
		    ncols, row->nitems, vs);
		return (SQL_ERR_COLUMN_COUNT);
	}

	targets = (size_t *)sql_calloc(ncols, sizeof(size_t));
	vals = row_values_alloc(ncols);
	if (vals == NULL || targets == NULL) {
		ret = SQL_ERR_NOMEM;
		goto err;
	}
	for (i = 0; i < ncols; i++)
		targets[i] = ncols;	/* "not mentioned" marker. */

	for (i = 0; i < row->nitems; i++) {
		size_t cindx;

		ret = insert_target_col(stmt, schema, i, &cindx);
		if (ret != SQL_OK)
			goto err;
		targets[cindx] = i;
	}

	sql_value_init(&conv);
	for (i = 0; i < row->nitems; i++) {
		SQL_VALUE raw;
		size_t cindx;

		ret = insert_target_col(stmt, schema, i, &cindx);
		if (ret != SQL_OK)
			goto err;
		sql_value_init(&raw);
		ret = sql_expr_eval(sess, schema, NULL, row->items[i], &raw);
		if (ret != SQL_OK) {
			sql_value_clear(&raw);
			goto err;
		}
		ret = sql_value_cast(&raw, schema->cols[cindx].type,
		    schema->cols[cindx].length, &conv);
		sql_value_clear(&raw);
		if (ret != SQL_OK)
			goto err;
		sql_value_clear(&vals[cindx]);
		ret = sql_value_copy(&conv, &vals[cindx]);
		sql_value_clear(&conv);
		if (ret != SQL_OK)
			goto err;
	}

	/* Fill in everything the statement did not mention. */
	for (i = 0; i < ncols; i++) {
		if (targets[i] != ncols)
			continue;	/* Already has an explicit value. */
		if (schema->cols[i].has_default) {
			sql_value_clear(&vals[i]);
			ret = sql_value_copy(&schema->cols[i].defval,
			    &vals[i]);
			if (ret != SQL_OK)
				goto err;
			continue;
		}
		sql_value_clear(&vals[i]);
		ret = sql_value_set_null(&vals[i], schema->cols[i].type);
		if (ret != SQL_OK)
			goto err;
		if (schema->cols[i].not_null) {
			ret = SQL_ERR_NOT_NULL;
			goto err;
		}
	}

	/* Pick the row id, an integer primary key becomes the alias. */
	if (schema->pk_indx >= 0) {
		if (vals[schema->pk_indx].isnull) {
			rowid = schema->next_rowid;
			sql_value_clear(&vals[schema->pk_indx]);
			ret = sql_value_set_int(&vals[schema->pk_indx],
			    schema->cols[schema->pk_indx].type,
			    (int64)rowid);
			if (ret != SQL_OK)
				goto err;
		} else {
			if (!SQL_TYPE_IS_INT(vals[schema->pk_indx].type)) {
				ret = SQL_ERR_TYPE;
				goto err;
			}
			if (vals[schema->pk_indx].ival <= 0) {
				ret = SQL_ERR_TYPE;
				goto err;
			}
			rowid = (uint64)vals[schema->pk_indx].ival;
		}
		if (rowid_key_free(sess, tb, rowid) == 0) {
			ret = SQL_ERR_DUP_KEY;
			goto err;
		}
	} else
		rowid = schema->next_rowid;

	if (rowid >= schema->next_rowid) {
		schema->next_rowid = rowid + 1;
		dirty = 1;
	}

	ret = sql_row_encode(schema, vals, &buf, &blen);
	if (ret != SQL_OK)
		goto err;
	ret = sql_kv_put(sess, tb, rowid, buf, blen);
	if (ret != SQL_OK)
		goto err;

	sql_free(buf);
	sql_free(targets);
	sql_row_values_clear(vals, ncols);
	sql_free(vals);

	if (dirty) {
		ret = sql_catalog_save_schema(sess, schema);
		if (ret != SQL_OK)
			return (ret);
	}
	if (rowidp != NULL)
		*rowidp = rowid;

	return (SQL_OK);

err:
	sql_free(buf);
	sql_free(targets);
	if (vals != NULL) {
		sql_row_values_clear(vals, ncols);
		sql_free(vals);
	}
	return (ret);
}

/********************************************************************
 * SELECT
 ********************************************************************/
typedef struct _selrow {
	size_t		seq;
	SQL_VALUE	*proj;
	SQL_VALUE	*keys;
} SELROW;

/*
 * qsort_r is not portable, so the comparator reads its context from
 * these two variables.  Executing statements is not re-entrant anyway.
 */
static const SQL_ORDER_ITEM *sort_order;
static size_t sort_norder;

static int selrow_cmp(const void *pa, const void *pb) {
	const SELROW *a, *b;
	size_t i;
	int c;

	a = (const SELROW *)pa;
	b = (const SELROW *)pb;
	for (i = 0; i < sort_norder; i++) {
		c = sql_value_cmp(&a->keys[i], &b->keys[i]);
		if (c != 0)
			return (sort_order[i].desc ? -c : c);
	}
	if (a->seq < b->seq)
		return (-1);
	return (a->seq > b->seq ? 1 : 0);
}

static int eval_int_arg(SQL_SESSION *sess, const SQL_EXPR *expr,
    int64 defval, int64 *outp) {
	SQL_VALUE v, conv;
	int ret;

	if (expr == NULL) {
		*outp = defval;
		return (SQL_OK);
	}
	sql_value_init(&v);
	sql_value_init(&conv);
	ret = sql_expr_eval(sess, NULL, NULL, expr, &v);
	if (ret != SQL_OK) {
		sql_value_clear(&v);
		return (ret);
	}
	ret = sql_value_cast(&v, SQL_T_BIGINT, 0, &conv);
	sql_value_clear(&v);
	if (ret != SQL_OK) {
		sql_value_clear(&conv);
		return (ret);
	}
	if (conv.isnull || conv.ival < 0) {
		sql_value_clear(&conv);
		return (SQL_ERR_ARG);
	}
	*outp = conv.ival;
	sql_value_clear(&conv);

	return (SQL_OK);
}

/* Derive the output name of a projection item. */
static char *projection_name(const SQL_SELECT_ITEM *item,
    SQL_SCHEMA *schema) {
	size_t indx;

	if (item->alias != NULL)
		return (sql_strdup(item->alias));
	if (item->expr != NULL && item->expr->xtype == EX_COLUMN &&
	    sql_schema_findcol(schema, item->expr->lname, &indx) == SQL_OK)
		return (sql_strdup(schema->cols[indx].name));

	return (sql_strdup("expr"));
}

/*
 * What the traverse callback returns to stop the scan early.
 *
 * Every engine error is negative (see van.h), so a positive value can
 * never be mistaken for one by the caller of table->traverse().
 */
#define	SEL_STOP	1

/*
 * A rowid range that can be handed to table->traverse() as a key range.
 *
 * Only the rowid alias column can be pushed down: its stored value is
 * the row id itself, so a bound on it is a bound on the key.  A table
 * without such a column has nothing to push.
 */
typedef struct _selrange {
	int	has_start;		/* A lower bound was found	*/
	int	has_end;		/* An upper bound was found	*/
	uint64	start;
	uint64	end;
	int	start_inc;
	int	end_inc;
} SELRANGE;

/*
 * Everything the traverse callback needs, and everything it produces.
 *
 * traverse() hands out no state of its own, so the collected rows and
 * the first error seen have to live in the caller's argument.
 */
typedef struct _selctx {
	SQL_SESSION	*sess;
	SQL_STMT	*stmt;
	SQL_SCHEMA	*schema;
	SQL_VALUE	*vals;		/* Scratch row, reused per row	*/
	SELROW		*rows;
	size_t		nrows;
	size_t		cap;
	size_t		nproj;
	size_t		stop_at;	/* 0 = collect everything	*/
	int		ret;		/* First error, SQL_OK if none	*/
} SELCTX;

/*
 * Build the engine key of a synthetic row id, the same big endian 64
 * bit encoding the rest of the SQL layer uses for keys.
 */
static void sel_datum_key(uint64 rowid, VAN_DATUM *key,
    unsigned char *keybuf) {

	sql_rowkey_encode(rowid, keybuf);
	memset(key, 0, sizeof(*key));
	key->data = keybuf;
	key->size = SQL_KVSIZE;
	key->ulen = SQL_KVSIZE;
	key->flags = 0;
}

/*
 * Mirror a comparison so that the column is always on the left: the
 * constraint "100 < id" is the constraint "id > 100".
 */
static sql_op sel_mirror_op(sql_op op) {

	switch (op) {
	case OP_LT:
		return (OP_GT);
	case OP_GT:
		return (OP_LT);
	case OP_LE:
		return (OP_GE);
	case OP_GE:
		return (OP_LE);
	default:
		return (op);
	}
}

static void sel_range_lower(SELRANGE *rng, uint64 v, int inc) {

	/*
	 * The higher bound wins, and two bounds on the same value are
	 * only inclusive when both are: "id > 100 AND id >= 100" is
	 * "id > 100".
	 */
	if (!rng->has_start || v > rng->start) {
		rng->has_start = 1;
		rng->start = v;
		rng->start_inc = inc;
	} else if (v == rng->start)
		rng->start_inc = (rng->start_inc && inc);
}

static void sel_range_upper(SELRANGE *rng, uint64 v, int inc) {

	if (!rng->has_end || v < rng->end) {
		rng->has_end = 1;
		rng->end = v;
		rng->end_inc = inc;
	} else if (v == rng->end)
		rng->end_inc = (rng->end_inc && inc);
}

/*
 * Fold one comparison against the rowid alias into '*rng'.
 *
 * Anything that is not such a comparison is simply ignored here: the
 * WHERE clause is still evaluated in full by the callback, so a range
 * is always an optimization on top of a correct, complete filter and
 * can never change the result.
 */
static void sel_rowid_bound(SQL_SCHEMA *schema, const SQL_EXPR *e,
    SELRANGE *rng) {
	const SQL_EXPR *col, *lit;
	size_t indx;
	sql_op op;
	int64 v;

	if (e == NULL || e->xtype != EX_BINARY)
		return;

	op = e->op;
	col = e->left;
	lit = e->right;
	if (col != NULL && col->xtype != EX_COLUMN) {
		col = e->right;
		lit = e->left;
		op = sel_mirror_op(op);
	}
	if (col == NULL || lit == NULL)
		return;
	if (col->xtype != EX_COLUMN || lit->xtype != EX_LITERAL)
		return;
	if (schema->pk_indx < 0)
		return;
	if (sql_schema_findcol(schema, col->lname, &indx) != SQL_OK)
		return;
	if ((int)indx != schema->pk_indx)
		return;
	if (!SQL_TYPE_IS_INT(lit->lit.type) || lit->lit.isnull)
		return;
	/* Row ids start at 1, a non positive bound selects nothing. */
	if (lit->lit.ival <= 0)
		return;

	v = lit->lit.ival;
	switch (op) {
	case OP_EQ:
		sel_range_lower(rng, (uint64)v, 1);
		sel_range_upper(rng, (uint64)v, 1);
		break;
	case OP_GE:
		sel_range_lower(rng, (uint64)v, 1);
		break;
	case OP_GT:
		sel_range_lower(rng, (uint64)v, 0);
		break;
	case OP_LE:
		sel_range_upper(rng, (uint64)v, 1);
		break;
	case OP_LT:
		sel_range_upper(rng, (uint64)v, 0);
		break;
	default:
		break;
	}
}

/*
 * Walk a WHERE tree and pick up every ANDed comparison against the
 * rowid alias.
 */
static void sel_where_bounds(SQL_SCHEMA *schema, const SQL_EXPR *e,
    SELRANGE *rng) {

	if (e == NULL)
		return;
	if (e->xtype == EX_BINARY && e->op == OP_AND) {
		sel_where_bounds(schema, e->left, rng);
		sel_where_bounds(schema, e->right, rng);
		return;
	}
	sel_rowid_bound(schema, e, rng);
}

/*
 * One row of a SELECT scan, called by table->traverse().
 *
 * Returning 0 keeps the scan going.  Returning SEL_STOP ends it, and
 * ctx->ret then tells the caller whether that was an error or just
 * "enough rows collected".
 */
static int sel_traverse_cb(void *arg, const VAN_DATUM *key,
    const VAN_DATUM *value) {
	SELCTX *ctx;
	SQL_STMT *stmt;
	SQL_SCHEMA *schema;
	SELROW *newv;
	SQL_VALUE *proj, *keys, tmp;
	size_t nproj, i, j;
	int ret, tv, duplicate;

	(void)key;
	ctx = (SELCTX *)arg;
	stmt = ctx->stmt;
	schema = ctx->schema;
	nproj = ctx->nproj;

	ret = sql_row_decode(schema, value->data, value->size, ctx->vals);
	if (ret != SQL_OK) {
		ctx->ret = ret;
		return (SEL_STOP);
	}

	if (stmt->where != NULL) {
		ret = sql_expr_test(ctx->sess, schema, ctx->vals,
		    stmt->where, &tv);
		if (ret != SQL_OK) {
			ctx->ret = ret;
			return (SEL_STOP);
		}
		if (tv != TRI_TRUE)
			return (0);
	}

	if (ctx->nrows == ctx->cap) {
		newv = (SELROW *)realloc(ctx->rows,
		    (ctx->cap * 2) * sizeof(SELROW));
		if (newv == NULL) {
			ctx->ret = SQL_ERR_NOMEM;
			return (SEL_STOP);
		}
		ctx->rows = newv;
		ctx->cap *= 2;
	}

	proj = (SQL_VALUE *)sql_calloc(nproj, sizeof(SQL_VALUE));
	keys = (stmt->norder > 0 ?
	    (SQL_VALUE *)sql_calloc(stmt->norder,
	    sizeof(SQL_VALUE)) : NULL);
	if (proj == NULL || (stmt->norder > 0 && keys == NULL)) {
		sql_free(proj);
		sql_free(keys);
		ctx->ret = SQL_ERR_NOMEM;
		return (SEL_STOP);
	}

	for (i = 0; i < nproj; i++) {
		sql_value_init(&proj[i]);
		sql_value_init(&tmp);
		if (stmt->select_star) {
			ret = sql_value_copy(&ctx->vals[i], &proj[i]);
			sql_value_clear(&tmp);
			if (ret != SQL_OK)
				goto cb_err;
			continue;
		}
		ret = sql_expr_eval(ctx->sess, schema, ctx->vals,
		    stmt->select_items[i].expr, &tmp);
		if (ret != SQL_OK) {
			sql_value_clear(&tmp);
			goto cb_err;
		}
		ret = sql_value_copy(&tmp, &proj[i]);
		sql_value_clear(&tmp);
		if (ret != SQL_OK)
			goto cb_err;
	}
	for (i = 0; i < stmt->norder; i++) {
		ret = sql_expr_eval(ctx->sess, schema, ctx->vals,
		    stmt->order_by[i].expr, &keys[i]);
		if (ret != SQL_OK)
			goto cb_err;
	}

	/* DISTINCT is applied here, before the ordering. */
	duplicate = 0;
	if (stmt->distinct) {
		for (i = 0; i < ctx->nrows && !duplicate; i++) {
			for (j = 0; j < nproj; j++)
				if (sql_value_cmp(&ctx->rows[i].proj[j],
				    &proj[j]) != 0)
					break;
			duplicate = (j == nproj);
		}
	}
	if (duplicate) {
		for (i = 0; i < nproj; i++)
			sql_value_clear(&proj[i]);
		sql_free(proj);
		for (i = 0; i < stmt->norder; i++)
			sql_value_clear(&keys[i]);
		sql_free(keys);
		return (0);
	}

	ctx->rows[ctx->nrows].seq = ctx->nrows;
	ctx->rows[ctx->nrows].proj = proj;
	ctx->rows[ctx->nrows].keys = keys;
	ctx->nrows++;

	if (ctx->stop_at > 0 && ctx->nrows >= ctx->stop_at)
		return (SEL_STOP);

	return (0);

cb_err:
	for (i = 0; i < nproj; i++)
		sql_value_clear(&proj[i]);
	sql_free(proj);
	if (keys != NULL) {
		for (i = 0; i < stmt->norder; i++)
			sql_value_clear(&keys[i]);
		sql_free(keys);
	}
	ctx->ret = ret;

	return (SEL_STOP);
}

static int exec_select(SQL_SESSION *sess, SQL_STMT *stmt, FILE *out,
    size_t *nrowsp) {
	SQL_TABLE *tb;
	SQL_SCHEMA *schema;
	char **names;
	SELROW *rows;
	SQL_VALUE *vals, *proj, *keys;
	SQL_RESULTSET *rs;
	SELRANGE rng;
	SELCTX ctx;
	VAN_TRAVERSE_OPT optv;
	const VAN_TRAVERSE_OPT *opt;
	VAN_DATUM startd, endd;
	unsigned char startbuf[SQL_KVSIZE], endbuf[SQL_KVSIZE];
	size_t ncols, nproj, nrows, cap, i, j, printed, start, limit, offset;
	int64 lim, off;
	int ret, tvret, has_limit;

	tb = NULL;
	names = NULL;
	rows = NULL;
	vals = NULL;
	proj = NULL;
	keys = NULL;
	rs = NULL;
	cap = 0;
	nrows = 0;
	printed = 0;
	has_limit = 0;

	tb = sql_session_find(sess, stmt->ltablename);
	if (tb == NULL)
		return (sql_seterr_notable(sess, stmt->ltablename));
	ret = sql_table_ensure_open(sess, tb);
	if (ret != SQL_OK)
		return (ret);
	schema = tb->schema;
	ncols = schema->ncols;

	/* Build the projection plan. */
	if (stmt->select_star) {
		nproj = ncols;
		names = (char **)sql_calloc(ncols, sizeof(char *));
		if (names == NULL) {
			ret = SQL_ERR_NOMEM;
			goto err;
		}
		for (i = 0; i < ncols; i++) {
			names[i] = sql_strdup(schema->cols[i].name);
			if (names[i] == NULL) {
				ret = SQL_ERR_NOMEM;
				goto err;
			}
		}
	} else {
		nproj = stmt->nselect;
		names = (char **)sql_calloc(nproj, sizeof(char *));
		if (names == NULL) {
			ret = SQL_ERR_NOMEM;
			goto err;
		}
		for (i = 0; i < nproj; i++) {
			names[i] = projection_name(&stmt->select_items[i],
			    schema);
			if (names[i] == NULL) {
				ret = SQL_ERR_NOMEM;
				goto err;
			}
		}
	}

	/* Validate that every projected column actually exists. */
	if (!stmt->select_star) {
		for (i = 0; i < nproj; i++) {
			SQL_EXPR *e = stmt->select_items[i].expr;

			if (e != NULL && e->xtype == EX_COLUMN) {
				size_t cindx;

				if (sql_schema_findcol(schema, e->lname,
				    &cindx) != SQL_OK) {
					(void)snprintf(sess->errmsg,
					    sizeof(sess->errmsg),
					    "Error: no such column '%s' in "
					    "table '%s'",
					    e->lname, stmt->ltablename);
					ret = SQL_ERR_NO_COLUMN;
					goto err;
				}
			}
		}
	}

	ret = eval_int_arg(sess, stmt->limit, 0, &lim);
	if (ret != SQL_OK)
		goto err;
	ret = eval_int_arg(sess, stmt->offset, 0, &off);
	if (ret != SQL_OK)
		goto err;
	has_limit = (stmt->limit != NULL);
	limit = (size_t)lim;
	offset = (size_t)off;
	start = offset;
	if (has_limit)
		start = offset;

	vals = row_values_alloc(ncols);
	if (vals == NULL) {
		ret = SQL_ERR_NOMEM;
		goto err;
	}

	/* Collect every qualifying row with a single table->traverse(). */
	cap = 16;
	rows = (SELROW *)sql_calloc(cap, sizeof(SELROW));
	if (rows == NULL) {
		ret = SQL_ERR_NOMEM;
		goto err;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.sess = sess;
	ctx.stmt = stmt;
	ctx.schema = schema;
	ctx.vals = vals;
	ctx.rows = rows;
	ctx.cap = cap;
	ctx.nproj = nproj;
	ctx.ret = SQL_OK;
	/*
	 * Stopping early is only safe when the rows collected are also
	 * the rows returned, in order.  ORDER BY reorders everything
	 * afterwards and DISTINCT drops rows that cannot be counted in
	 * advance, so either one forces a full scan.
	 *
	 * The sum is checked for an overflow, a wrapped one would stop
	 * the scan too early and lose rows.
	 */
	if (has_limit && stmt->norder == 0 && !stmt->distinct &&
	    offset + limit >= limit)
		ctx.stop_at = offset + limit;

	/*
	 * A WHERE bound on the rowid alias becomes a key range.  The
	 * clause is still evaluated in the callback, so this only ever
	 * narrows the scan.
	 */
	memset(&rng, 0, sizeof(rng));
	if (schema->pk_indx >= 0 && stmt->where != NULL)
		sel_where_bounds(schema, stmt->where, &rng);

	opt = NULL;
	if (rng.has_start || rng.has_end) {
		memset(&optv, 0, sizeof(optv));
		if (rng.has_start) {
			sel_datum_key(rng.start, &startd, startbuf);
			optv.start_key = &startd;
			optv.start_inclusive = rng.start_inc;
		}
		if (rng.has_end) {
			sel_datum_key(rng.end, &endd, endbuf);
			optv.end_key = &endd;
			optv.end_inclusive = rng.end_inc;
		}
		opt = &optv;
	}

	tvret = tb->table->traverse(tb->table, VAN_TRAVERSE_FORWARD, opt,
	    sel_traverse_cb, &ctx);
	rows = ctx.rows;	/* The callback may have grown the array. */
	cap = ctx.cap;
	nrows = ctx.nrows;

	if (ctx.ret != SQL_OK) {
		ret = ctx.ret;
		goto err;
	}
	/*
	 * traverse() hands the callback's value straight back, and only
	 * SEL_STOP is ours: anything else is an engine failure.
	 */
	if (tvret != 0 && tvret != SEL_STOP) {
		ret = SQL_ERR_STORE;
		goto err;
	}

	/* ORDER BY, then LIMIT / OFFSET. */
	if (stmt->norder > 0 && nrows > 1) {
		sort_order = stmt->order_by;
		sort_norder = stmt->norder;
		qsort(rows, nrows, sizeof(SELROW), selrow_cmp);
		sort_order = NULL;
		sort_norder = 0;
	}

	ret = sql_resultset_alloc(nproj, &rs);
	if (ret != SQL_OK)
		goto err;
	ret = sql_resultset_setcolnames(rs, names);
	if (ret != SQL_OK)
		goto err;

	for (i = 0; i < nrows; i++) {
		if (i < start)
			continue;
		if (has_limit && printed >= limit)
			break;
		ret = sql_resultset_addrow(rs, rows[i].proj);
		if (ret != SQL_OK)
			goto err;
		printed++;
	}

	if (sess->resultp != NULL)
		*sess->resultp = rs;	/* Ownership moves to the caller. */
	else {
		if (out != NULL)
			sql_resultset_print(rs, out);
		sql_resultset_free(rs);
	}
	if (nrowsp != NULL)
		*nrowsp = printed;
	sess->rows_affected = printed;

	for (i = 0; i < nrows; i++) {
		if (rows[i].proj != NULL) {
			for (j = 0; j < nproj; j++)
				sql_value_clear(&rows[i].proj[j]);
			sql_free(rows[i].proj);
		}
		if (rows[i].keys != NULL) {
			for (j = 0; j < stmt->norder; j++)
				sql_value_clear(&rows[i].keys[j]);
			sql_free(rows[i].keys);
		}
	}
	sql_free(rows);
	for (i = 0; i < nproj; i++)
		sql_free(names[i]);
	sql_free(names);
	sql_row_values_clear(vals, ncols);
	sql_free(vals);
	(void)tb;

	return (SQL_OK);

err:
	if (rs != NULL)
		sql_resultset_free(rs);
	if (rows != NULL) {
		for (i = 0; i < nrows; i++) {
			if (rows[i].proj != NULL) {
				for (j = 0; j < nproj; j++)
					sql_value_clear(&rows[i].proj[j]);
				sql_free(rows[i].proj);
			}
			if (rows[i].keys != NULL) {
				for (j = 0; j < stmt->norder; j++)
					sql_value_clear(&rows[i].keys[j]);
				sql_free(rows[i].keys);
			}
		}
		sql_free(rows);
	}
	if (proj != NULL) {
		for (i = 0; i < nproj; i++)
			sql_value_clear(&proj[i]);
		sql_free(proj);
	}
	if (keys != NULL) {
		for (i = 0; i < stmt->norder; i++)
			sql_value_clear(&keys[i]);
		sql_free(keys);
	}
	if (vals != NULL) {
		sql_row_values_clear(vals, ncols);
		sql_free(vals);
	}
	if (names != NULL) {
		for (i = 0; i < nproj; i++)
			sql_free(names[i]);
		sql_free(names);
	}

	return (ret);
}

/********************************************************************
 * INSERT
 ********************************************************************/
static int exec_insert(SQL_SESSION *sess, SQL_STMT *stmt, FILE *out,
    size_t *nrowsp) {
	SQL_TABLE *tb;
	size_t i, rowid, last;
	int ret;

	(void)out;
	tb = sql_session_find(sess, stmt->ltablename);
	if (tb == NULL)
		return (sql_seterr_notable(sess, stmt->ltablename));
	ret = sql_table_ensure_open(sess, tb);
	if (ret != SQL_OK)
		return (ret);

	last = 0;
	for (i = 0; i < stmt->nrows_ins; i++) {
		rowid = 0;
		ret = insert_one_row(sess, stmt, tb, tb->schema,
		    stmt->insert_rows[i], &rowid);
		if (ret != SQL_OK) {
			/*
			 * Rows already written stay: there is no
			 * statement level rollback in this layer.
			 */
			sess->rows_affected = i;
			return (ret);
		}
		last = rowid;
	}

	sess->rows_affected = stmt->nrows_ins;
	sess->last_rowid = last;
	if (nrowsp != NULL)
		*nrowsp = stmt->nrows_ins;

	return (SQL_OK);
}

/********************************************************************
 * UPDATE
 ********************************************************************/
static int exec_update(SQL_SESSION *sess, SQL_STMT *stmt, FILE *out,
    size_t *nrowsp) {
	SQL_TABLE *tb;
	SQL_SCHEMA *schema;
	SQL_VALUE *vals, conv;
	unsigned char *buf;
	size_t i, ncols, nchanged, buf_len;
	size_t *targets;
	uint64 rid, maxrid, newrid;
	int ret, found, tv;

	(void)out;
	tb = sql_session_find(sess, stmt->ltablename);
	if (tb == NULL)
		return (sql_seterr_notable(sess, stmt->ltablename));
	ret = sql_table_ensure_open(sess, tb);
	if (ret != SQL_OK)
		return (ret);
	schema = tb->schema;
	ncols = schema->ncols;

	targets = (size_t *)sql_calloc(stmt->nset, sizeof(size_t));
	vals = row_values_alloc(ncols);
	if (targets == NULL || vals == NULL) {
		sql_free(targets);
		if (vals != NULL)
			sql_row_values_clear(vals, ncols);
		sql_free(vals);
		return (SQL_ERR_NOMEM);
	}
	sql_value_init(&conv);
	buf = NULL;
	buf_len = 0;
	nchanged = 0;

	for (i = 0; i < stmt->nset; i++) {
		ret = sql_schema_findcol(schema, stmt->set_items[i].lcolname,
		    &targets[i]);
		if (ret != SQL_OK)
			goto err;
	}

	maxrid = schema->next_rowid;
	for (rid = 1; rid < maxrid; rid++) {
		int pkchanged;

		ret = fetch_row(sess, tb, rid, vals, &found);
		if (ret != SQL_OK)
			goto err;
		if (!found)
			continue;
		if (stmt->where != NULL) {
			ret = sql_expr_test(sess, schema, vals,
			    stmt->where, &tv);
			if (ret != SQL_OK)
				goto err;
			if (tv != TRI_TRUE)
				continue;
		}

		pkchanged = 0;
		newrid = rid;
		for (i = 0; i < stmt->nset; i++) {
			size_t cindx;

			cindx = targets[i];
			ret = sql_expr_eval(sess, schema, vals,
			    stmt->set_items[i].expr, &conv);
			if (ret != SQL_OK)
				goto err;
			ret = sql_value_cast(&conv,
			    schema->cols[cindx].type,
			    schema->cols[cindx].length, &vals[cindx]);
			sql_value_clear(&conv);
			if (ret != SQL_OK)
				goto err;
			if (vals[cindx].isnull &&
			    schema->cols[cindx].not_null) {
				ret = SQL_ERR_NOT_NULL;
				goto err;
			}
			if ((int)cindx == schema->pk_indx)
				pkchanged = 1;
		}

		if (pkchanged) {
			if (vals[schema->pk_indx].isnull) {
				ret = SQL_ERR_NOT_NULL;
				goto err;
			}
			if (!SQL_TYPE_IS_INT(vals[schema->pk_indx].type) ||
			    vals[schema->pk_indx].ival <= 0) {
				ret = SQL_ERR_TYPE;
				goto err;
			}
			newrid = (uint64)vals[schema->pk_indx].ival;
			if (newrid != rid &&
			    rowid_key_free(sess, tb, newrid) == 0) {
				ret = SQL_ERR_DUP_KEY;
				goto err;
			}
		}

		ret = sql_row_encode(schema, vals, &buf, &buf_len);
		if (ret != SQL_OK)
			goto err;
		ret = sql_kv_put(sess, tb, newrid, buf, buf_len);
		if (ret != SQL_OK)
			goto err;
		sql_free(buf);
		buf = NULL;
		if (newrid != rid) {
			ret = sql_kv_del(sess, tb, rid);
			if (ret != SQL_OK)
				goto err;
			if (newrid >= schema->next_rowid) {
				schema->next_rowid = newrid + 1;
				ret = sql_catalog_save_schema(sess, schema);
				if (ret != SQL_OK)
					goto err;
			}
		}
		nchanged++;
	}

	sess->rows_affected = nchanged;
	if (nrowsp != NULL)
		*nrowsp = nchanged;

	sql_free(targets);
	sql_row_values_clear(vals, ncols);
	sql_free(vals);

	return (SQL_OK);

err:
	sql_free(buf);
	sql_free(targets);
	sql_row_values_clear(vals, ncols);
	sql_free(vals);
	return (ret);
}

/********************************************************************
 * DELETE
 ********************************************************************/
static int exec_delete(SQL_SESSION *sess, SQL_STMT *stmt, FILE *out,
    size_t *nrowsp) {
	SQL_TABLE *tb;
	SQL_SCHEMA *schema;
	SQL_VALUE *vals;
	size_t ncols, ndeleted;
	uint64 rid, maxrid;
	int ret, found, tv;

	(void)out;
	tb = sql_session_find(sess, stmt->ltablename);
	if (tb == NULL)
		return (sql_seterr_notable(sess, stmt->ltablename));
	ret = sql_table_ensure_open(sess, tb);
	if (ret != SQL_OK)
		return (ret);
	schema = tb->schema;
	ncols = schema->ncols;

	vals = row_values_alloc(ncols);
	if (vals == NULL)
		return (SQL_ERR_NOMEM);

	ndeleted = 0;
	maxrid = schema->next_rowid;
	for (rid = 1; rid < maxrid; rid++) {
		ret = fetch_row(sess, tb, rid, vals, &found);
		if (ret != SQL_OK)
			goto err;
		if (!found)
			continue;
		if (stmt->where != NULL) {
			ret = sql_expr_test(sess, schema, vals,
			    stmt->where, &tv);
			if (ret != SQL_OK)
				goto err;
			if (tv != TRI_TRUE)
				continue;
		}
		ret = sql_kv_del(sess, tb, rid);
		if (ret != SQL_OK)
			goto err;
		ndeleted++;
	}

	sess->rows_affected = ndeleted;
	if (nrowsp != NULL)
		*nrowsp = ndeleted;

	sql_row_values_clear(vals, ncols);
	sql_free(vals);

	return (SQL_OK);

err:
	sql_row_values_clear(vals, ncols);
	sql_free(vals);
	return (ret);
}

/********************************************************************
 * Dispatcher
 ********************************************************************/
int sql_stmt_exec(SQL_SESSION *sess, SQL_STMT *stmt, FILE *out,
    size_t *nrowsp) {
	int ret;

	if (sess == NULL || stmt == NULL)
		return (SQL_ERR_ARG);
	if (!sess->opened)
		return (SQL_ERR_STORE);
	if (FLD_ISSET(sess->oflags, SQL_OPEN_READONLY) &&
	    stmt->type != STMT_SELECT)
		return (SQL_ERR_READONLY);

	sess->rows_affected = 0;

	switch (stmt->type) {
		case STMT_CREATE_TABLE:
			ret = exec_create_table(sess, stmt, out);
			break;
		case STMT_DROP_TABLE:
			ret = exec_drop_table(sess, stmt, out);
			break;
		case STMT_INSERT:
			ret = exec_insert(sess, stmt, out, nrowsp);
			break;
		case STMT_SELECT:
			ret = exec_select(sess, stmt, out, nrowsp);
			break;
		case STMT_UPDATE:
			ret = exec_update(sess, stmt, out, nrowsp);
			break;
		case STMT_DELETE:
			ret = exec_delete(sess, stmt, out, nrowsp);
			break;
		default:
			ret = SQL_ERR_FEATURE;
			break;
	}

	return (ret);
}
