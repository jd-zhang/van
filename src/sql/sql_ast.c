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
 * Memory helpers.
 *
 * The SQL layer uses the plain C allocator: the values it produces are
 * handed out through the public SQL API and must stay valid after the
 * session is closed.
 */
void *sql_calloc(size_t nelem, size_t elsize) {
	void *p;

	if (nelem == 0 || elsize == 0)
		return (NULL);
	p = calloc(nelem, elsize);
	return (p);
}

void sql_free(void *p) {

	if (p != NULL)
		free(p);
}

char *sql_strdup(const char *str) {
	size_t len;

	if (str == NULL)
		return (NULL);
	len = strlen(str);
	return (sql_strndup(str, len));
}

char *sql_strndup(const char *str, size_t len) {
	char *p;

	if (str == NULL)
		return (NULL);
	p = sql_calloc(len + 1, 1);
	if (p == NULL)
		return (NULL);
	memcpy(p, str, len);
	return (p);
}

char *sql_strlower(const char *str, size_t len) {
	char *p;
	size_t i;

	if (str == NULL)
		return (NULL);
	p = sql_strndup(str, len);
	if (p == NULL)
		return (NULL);
	for (i = 0; i < len; i++)
		p[i] = (char)tolower((unsigned char)p[i]);
	return (p);
}

/*
 * Grow a vector, 'cnt' is the current element count, 'alloc' is written
 * back with the new capacity.  Returns SQL_OK or SQL_ERR_NOMEM.
 */
static int sql_ptrarray_grow(void ***vecp, size_t cnt, size_t *allocp,
    size_t need) {
	void **vec;
	size_t alloc, newsz;

	alloc = *allocp;
	if (need <= alloc)
		return (SQL_OK);

	if (alloc == 0)
		alloc = 8;
	while (alloc < need)
		alloc *= 2;
	/*
	 * cnt is unused except for documentation purposes, keep the
	 * signature stable for all callers.
	 */
	(void)cnt;
	newsz = alloc * sizeof(void *);
	vec = realloc(*vecp, newsz);
	if (vec == NULL)
		return (SQL_ERR_NOMEM);
	*vecp = vec;
	*allocp = alloc;

	return (SQL_OK);
}

int sql_expr_alloc(sql_expr_type xtype, SQL_EXPR **exprp) {
	SQL_EXPR *expr;

	expr = sql_calloc(1, sizeof(SQL_EXPR));
	if (expr == NULL)
		return (SQL_ERR_NOMEM);
	expr->xtype = xtype;
	sql_value_init(&expr->lit);
	*exprp = expr;

	return (SQL_OK);
}

void sql_expr_free(SQL_EXPR *expr) {

	if (expr == NULL)
		return;

	sql_value_clear(&expr->lit);
	sql_free(expr->name);
	sql_free(expr->lname);
	sql_expr_free(expr->left);
	sql_expr_free(expr->right);
	sql_expr_free(expr->third);
	sql_exprlist_free(expr->list);
	sql_free(expr);
}

SQL_EXPR *sql_expr_literal(const SQL_VALUE *val) {
	SQL_EXPR *expr;
	int ret;

	expr = NULL;
	ret = sql_expr_alloc(EX_LITERAL, &expr);
	if (ret != SQL_OK)
		return (NULL);
	ret = sql_value_copy(val, &expr->lit);
	if (ret != SQL_OK) {
		sql_expr_free(expr);
		return (NULL);
	}

	return (expr);
}

SQL_EXPR *sql_expr_column(const char *name, size_t len) {
	SQL_EXPR *expr;

	if (sql_expr_alloc(EX_COLUMN, &expr) != SQL_OK)
		return (NULL);
	expr->name = sql_strndup(name, len);
	expr->lname = sql_strlower(name, len);
	if (expr->name == NULL || expr->lname == NULL) {
		sql_expr_free(expr);
		return (NULL);
	}

	return (expr);
}

SQL_EXPR *sql_expr_binary(sql_op op, SQL_EXPR *left, SQL_EXPR *right) {
	SQL_EXPR *expr;

	if (sql_expr_alloc(EX_BINARY, &expr) != SQL_OK)
		return (NULL);
	expr->op = op;
	expr->left = left;
	expr->right = right;

	return (expr);
}

SQL_EXPR *sql_expr_unary(sql_op op, SQL_EXPR *arg) {
	SQL_EXPR *expr;

	if (sql_expr_alloc(EX_UNARY, &expr) != SQL_OK)
		return (NULL);
	expr->op = op;
	expr->left = arg;

	return (expr);
}

SQL_EXPR *sql_expr_not(SQL_EXPR *arg) {
	SQL_EXPR *expr;

	if (sql_expr_alloc(EX_NOT, &expr) != SQL_OK)
		return (NULL);
	expr->left = arg;

	return (expr);
}

SQL_EXPR *sql_expr_isnull(SQL_EXPR *arg, int negate) {
	SQL_EXPR *expr;

	if (sql_expr_alloc(EX_ISNULL, &expr) != SQL_OK)
		return (NULL);
	expr->left = arg;
	expr->negate = negate;

	return (expr);
}

SQL_EXPR *sql_expr_in(SQL_EXPR *arg, SQL_EXPRLIST *list, int negate) {
	SQL_EXPR *expr;

	if (sql_expr_alloc(EX_IN, &expr) != SQL_OK)
		return (NULL);
	expr->left = arg;
	expr->list = list;
	expr->negate = negate;

	return (expr);
}

SQL_EXPR *sql_expr_between(SQL_EXPR *arg, SQL_EXPR *low, SQL_EXPR *high,
    int negate) {
	SQL_EXPR *expr;

	if (sql_expr_alloc(EX_BETWEEN, &expr) != SQL_OK)
		return (NULL);
	expr->left = arg;
	expr->right = low;
	expr->third = high;
	expr->negate = negate;

	return (expr);
}

SQL_EXPR *sql_expr_like(SQL_EXPR *arg, SQL_EXPR *pat, int negate) {
	SQL_EXPR *expr;

	if (sql_expr_alloc(EX_LIKE, &expr) != SQL_OK)
		return (NULL);
	expr->left = arg;
	expr->right = pat;
	expr->negate = negate;

	return (expr);
}

SQL_EXPR *sql_expr_func(sql_kw kw) {
	SQL_EXPR *expr;

	if (sql_expr_alloc(EX_FUNC, &expr) != SQL_OK)
		return (NULL);
	expr->kw = kw;

	return (expr);
}

int sql_exprlist_alloc(SQL_EXPRLIST **listp) {
	SQL_EXPRLIST *list;

	list = sql_calloc(1, sizeof(SQL_EXPRLIST));
	if (list == NULL)
		return (SQL_ERR_NOMEM);
	*listp = list;

	return (SQL_OK);
}

void sql_exprlist_free(SQL_EXPRLIST *list) {
	size_t i;

	if (list == NULL)
		return;
	for (i = 0; i < list->nitems; i++)
		sql_expr_free(list->items[i]);
	sql_free(list->items);
	sql_free(list);
}

int sql_exprlist_add(SQL_EXPRLIST *list, SQL_EXPR *expr) {
	int ret;

	ret = sql_ptrarray_grow((void ***)&list->items, list->nitems,
	    &list->capacity, list->nitems + 1);
	if (ret != SQL_OK)
		return (ret);
	list->items[list->nitems++] = expr;

	return (SQL_OK);
}

int sql_stmt_alloc(sql_stmt_type type, SQL_STMT **stmtp) {
	SQL_STMT *stmt;

	stmt = sql_calloc(1, sizeof(SQL_STMT));
	if (stmt == NULL)
		return (SQL_ERR_NOMEM);
	stmt->type = type;
	*stmtp = stmt;

	return (SQL_OK);
}

void sql_stmt_free(SQL_STMT *stmt) {
	size_t i;

	if (stmt == NULL)
		return;

	sql_free(stmt->tablename);
	sql_free(stmt->ltablename);

	for (i = 0; i < stmt->ncols; i++)
		sql_column_clear(&stmt->columns[i]);
	sql_free(stmt->columns);

	for (i = 0; i < stmt->ninscols; i++) {
		sql_free(stmt->inscols[i]);
		sql_free(stmt->linscols[i]);
	}
	sql_free(stmt->inscols);
	sql_free(stmt->linscols);

	for (i = 0; i < stmt->nrows_ins; i++)
		sql_exprlist_free(stmt->insert_rows[i]);
	sql_free(stmt->insert_rows);

	for (i = 0; i < stmt->nselect; i++) {
		sql_expr_free(stmt->select_items[i].expr);
		sql_free(stmt->select_items[i].alias);
		sql_free(stmt->select_items[i].lalias);
	}
	sql_free(stmt->select_items);

	sql_expr_free(stmt->where);

	for (i = 0; i < stmt->norder; i++)
		sql_expr_free(stmt->order_by[i].expr);
	sql_free(stmt->order_by);

	sql_expr_free(stmt->limit);
	sql_expr_free(stmt->offset);

	for (i = 0; i < stmt->nset; i++) {
		sql_free(stmt->set_items[i].colname);
		sql_free(stmt->set_items[i].lcolname);
		sql_expr_free(stmt->set_items[i].expr);
	}
	sql_free(stmt->set_items);

	sql_free(stmt);
}
