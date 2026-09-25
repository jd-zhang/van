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
 * The engine has no table level cursor, so a scan walks the synthetic
 * row ids from 1 up to the next free id.  Row ids are used as keys, are
 * ascending in insertion order and are never reused, so this yields the
 * rows in insertion order.
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
		return (SQL_ERR_NO_TABLE);
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
	targets = NULL;
	blen = 0;
	dirty = 0;
	rowid = 0;

	if (stmt->ninscols != 0 && stmt->ninscols != row->nitems) {
		/* Arity is checked against the schema further down. */
		if (row->nitems != stmt->ninscols)
			return (SQL_ERR_ARG);
	}
	if (row->nitems > ncols)
		return (SQL_ERR_ARG);

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

static int exec_select(SQL_SESSION *sess, SQL_STMT *stmt, FILE *out,
    size_t *nrowsp) {
	SQL_TABLE *tb;
	SQL_SCHEMA *schema;
	char **names;
	SELROW *rows;
	SQL_VALUE *vals, *proj, *keys;
	SQL_RESULTSET *rs;
	size_t ncols, nproj, nrows, cap, i, j, printed, start, limit, offset;
	uint64 rid, maxrid;
	int64 lim, off;
	int ret, found, tv, has_limit, duplicate;

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
		return (SQL_ERR_NO_TABLE);
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

	/* Collect every qualifying row. */
	cap = 16;
	rows = (SELROW *)sql_calloc(cap, sizeof(SELROW));
	if (rows == NULL) {
		ret = SQL_ERR_NOMEM;
		goto err;
	}
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
		if (nrows == cap) {
			SELROW *newv;
			newv = (SELROW *)realloc(rows,
			    (cap * 2) * sizeof(SELROW));
			if (newv == NULL) {
				ret = SQL_ERR_NOMEM;
				goto err;
			}
			rows = newv;
			cap *= 2;
		}
		proj = (SQL_VALUE *)sql_calloc(nproj, sizeof(SQL_VALUE));
		keys = (stmt->norder > 0 ?
		    (SQL_VALUE *)sql_calloc(stmt->norder,
		    sizeof(SQL_VALUE)) : NULL);
		if (proj == NULL || (stmt->norder > 0 && keys == NULL)) {
			sql_free(proj);
			sql_free(keys);
			ret = SQL_ERR_NOMEM;
			goto err;
		}
		for (i = 0; i < nproj; i++) {
			SQL_VALUE tmp;

			sql_value_init(&proj[i]);
			sql_value_init(&tmp);
			if (stmt->select_star) {
				ret = sql_value_copy(&vals[i], &proj[i]);
				sql_value_clear(&tmp);
				if (ret != SQL_OK)
					goto err;
				continue;
			}
			ret = sql_expr_eval(sess, schema, vals,
			    stmt->select_items[i].expr, &tmp);
			if (ret != SQL_OK) {
				sql_value_clear(&tmp);
				goto err;
			}
			ret = sql_value_copy(&tmp, &proj[i]);
			sql_value_clear(&tmp);
			if (ret != SQL_OK)
				goto err;
		}
		for (i = 0; i < stmt->norder; i++) {
			ret = sql_expr_eval(sess, schema, vals,
			    stmt->order_by[i].expr, &keys[i]);
			if (ret != SQL_OK)
				goto err;
		}

		/* DISTINCT is applied here, before the ordering. */
		duplicate = 0;
		if (stmt->distinct) {
			for (i = 0; i < nrows && !duplicate; i++) {
				for (j = 0; j < nproj; j++)
					if (sql_value_cmp(&rows[i].proj[j],
					    &proj[j]) != 0)
						break;
				duplicate = (j == nproj);
			}
		}
		if (duplicate) {
			for (i = 0; i < nproj; i++)
				sql_value_clear(&proj[i]);
			sql_free(proj);
			proj = NULL;
			for (i = 0; i < stmt->norder; i++)
				sql_value_clear(&keys[i]);
			sql_free(keys);
			keys = NULL;
			continue;
		}

		rows[nrows].seq = nrows;
		rows[nrows].proj = proj;
		rows[nrows].keys = keys;
		proj = NULL;
		keys = NULL;
		nrows++;
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
		return (SQL_ERR_NO_TABLE);
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
		return (SQL_ERR_NO_TABLE);
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
		return (SQL_ERR_NO_TABLE);
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
