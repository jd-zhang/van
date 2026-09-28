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

const char *sql_type_name(sql_type_t type) {

	switch (type) {
		case SQL_T_NULL:
			return ("NULL");
		case SQL_T_SMALLINT:
			return ("SMALLINT");
		case SQL_T_INT:
			return ("INT");
		case SQL_T_BIGINT:
			return ("BIGINT");
		case SQL_T_FLOAT:
			return ("FLOAT");
		case SQL_T_DOUBLE:
			return ("DOUBLE");
		case SQL_T_VARCHAR:
			return ("VARCHAR");
		case SQL_T_CHAR:
			return ("CHAR");
		case SQL_T_TEXT:
			return ("TEXT");
		case SQL_T_BINARY:
			return ("BINARY");
		case SQL_T_VARBINARY:
			return ("VARBINARY");
		case SQL_T_BLOB:
			return ("BLOB");
		case SQL_T_DATE:
			return ("DATE");
		case SQL_T_TIME:
			return ("TIME");
		case SQL_T_TIMESTAMP:
			return ("TIMESTAMP");
		default:
			break;
	}

	return ("UNKNOWN");
}

const char *sql_errstr(int err) {

	switch (err) {
		case SQL_OK:
			return ("success");
		case SQL_ERR_LEX:
			return ("syntax error (illegal token)");
		case SQL_ERR_PARSE:
			return ("syntax error");
		case SQL_ERR_NOMEM:
			return ("out of memory");
		case SQL_ERR_NO_TABLE:
			return ("no such table");
		case SQL_ERR_DUP_TABLE:
			return ("table already exists");
		case SQL_ERR_DUP_KEY:
			return ("duplicate key value");
		case SQL_ERR_NO_COLUMN:
			return ("no such column");
		case SQL_ERR_AMBIGUOUS:
			return ("ambiguous column name");
		case SQL_ERR_TYPE:
			return ("type mismatch");
		case SQL_ERR_NOT_NULL:
			return ("NOT NULL constraint failed");
		case SQL_ERR_ARG:
			return ("invalid argument");
		case SQL_ERR_FEATURE:
			return ("feature not supported");
		case SQL_ERR_STORE:
			return ("storage engine failure");
		case SQL_ERR_CATALOG:
			return ("corrupted catalog");
		case SQL_ERR_CONVERT:
			return ("value conversion failed");
		case SQL_ERR_LIMIT:
			return ("internal limit exceeded");
		case SQL_ERR_READONLY:
			return ("session is read only");
		case SQL_ERR_COLUMN_COUNT:
			return ("column count mismatch");
		default:
			break;
	}

	return ("unknown error");
}

void sql_value_init(SQL_VALUE *val) {

	memset(val, 0, sizeof(*val));
	val->type = SQL_T_NULL;
	val->isnull = 1;
}

void sql_value_clear(SQL_VALUE *val) {

	if (val == NULL)
		return;
	if (val->sval != NULL)
		sql_free(val->sval);
	sql_value_init(val);
}

int sql_value_set_null(SQL_VALUE *val, sql_type_t type) {

	if (val == NULL)
		return (SQL_ERR_ARG);
	sql_value_clear(val);
	val->type = type;
	val->isnull = 1;

	return (SQL_OK);
}

int sql_value_set_int(SQL_VALUE *val, sql_type_t type, int64 v) {

	if (val == NULL)
		return (SQL_ERR_ARG);
	sql_value_clear(val);
	val->type = type;
	val->isnull = 0;
	val->ival = v;
	val->dval = (double)v;

	return (SQL_OK);
}

int sql_value_set_double(SQL_VALUE *val, sql_type_t type, double v) {

	if (val == NULL)
		return (SQL_ERR_ARG);
	sql_value_clear(val);
	val->type = type;
	val->isnull = 0;
	val->dval = v;
	val->ival = (int64)v;

	return (SQL_OK);
}

int sql_value_set_string(SQL_VALUE *val, sql_type_t type, const void *data,
    size_t len) {
	unsigned char *p;

	if (val == NULL)
		return (SQL_ERR_ARG);
	sql_value_clear(val);
	if (data == NULL)
		len = 0;
	p = sql_calloc(len + 1, 1);
	if (p == NULL)
		return (SQL_ERR_NOMEM);
	if (len > 0)
		memcpy(p, data, len);
	val->type = type;
	val->isnull = 0;
	val->sval = p;
	val->slen = len;

	return (SQL_OK);
}

int sql_value_from_text(SQL_VALUE *val, sql_type_t type, const char *text,
    size_t len) {

	return (sql_value_set_string(val, type, text, len));
}

int sql_value_copy(const SQL_VALUE *src, SQL_VALUE *dst) {
	int ret;

	if (src == NULL || dst == NULL)
		return (SQL_ERR_ARG);
	sql_value_clear(dst);
	dst->type = src->type;
	dst->isnull = src->isnull;
	dst->ival = src->ival;
	dst->dval = src->dval;
	dst->sval = NULL;
	dst->slen = 0;
	if (src->sval != NULL && !src->isnull) {
		ret = sql_value_set_string(dst, src->type,
		    src->sval, src->slen);
		if (ret != SQL_OK)
			return (ret);
	}

	return (SQL_OK);
}

int sql_value_tostring(const SQL_VALUE *val, char **sp) {
	char buf[256], *res;
	size_t len, i;
	const char *hexdig = "0123456789ABCDEF";

	if (val == NULL || sp == NULL)
		return (SQL_ERR_ARG);
	*sp = NULL;
	if (val->isnull)
		return (SQL_OK);

	if (SQL_TYPE_IS_STRING(val->type)) {
		res = sql_calloc(val->slen + 1, 1);
		if (res == NULL)
			return (SQL_ERR_NOMEM);
		if (val->slen > 0)
			memcpy(res, val->sval, val->slen);
		*sp = res;
		return (SQL_OK);
	}

	if (SQL_TYPE_IS_BINARY(val->type)) {
		len = val->slen;
		res = sql_calloc(len * 2 + 1, 1);
		if (res == NULL)
			return (SQL_ERR_NOMEM);
		for (i = 0; i < len; i++) {
			res[i * 2] = hexdig[(val->sval[i] >> 4) & 0x0F];
			res[i * 2 + 1] = hexdig[val->sval[i] & 0x0F];
		}
		*sp = res;
		return (SQL_OK);
	}

	buf[0] = '\0';
	switch (val->type) {
		case SQL_T_DATE:
		case SQL_T_TIME:
		case SQL_T_TIMESTAMP:
		{
			char *ts;
			int ret;

			ts = NULL;
			if (val->type == SQL_T_DATE)
				ret = sql_date_to_string(val->ival, &ts);
			else if (val->type == SQL_T_TIME)
				ret = sql_time_to_string(val->ival, &ts);
			else
				ret = sql_timestamp_to_string(val->ival, &ts);
			if (ret != SQL_OK)
				return (ret);
			*sp = ts;
			return (SQL_OK);
		}
		case SQL_T_FLOAT:
		case SQL_T_DOUBLE:
			(void)snprintf(buf, sizeof(buf), "%.15g", val->dval);
			break;
		default:
			(void)snprintf(buf, sizeof(buf), "%lld",
			    (long long)val->ival);
			break;
	}

	res = sql_strdup(buf);
	if (res == NULL)
		return (SQL_ERR_NOMEM);
	*sp = res;

	return (SQL_OK);
}

static int sql_value_class(const SQL_VALUE *val) {

	if (val->isnull)
		return (SQL_CMP_NULL);
	if (SQL_TYPE_IS_NUMERIC(val->type))
		return (SQL_CMP_NUMERIC);
	if (SQL_TYPE_IS_BINARY(val->type))
		return (SQL_CMP_BINARY);
	if (SQL_TYPE_IS_TEMPORAL(val->type))
		return (SQL_CMP_TEMPORAL);

	return (SQL_CMP_STRING);
}

/*
 * Compare two values, -1 / 0 / 1.  NULL sorts before everything else,
 * then numbers, temporal values, strings and finally binary blobs.
 * Within a class the comparison is the natural one for that class.
 */
int sql_value_cmp(const SQL_VALUE *a, const SQL_VALUE *b) {
	int ca, cb, ret;
	double da, db;
	int64 ia, ib;
	size_t limit, i;

	if (a == NULL && b == NULL)
		return (0);
	if (a == NULL)
		return (-1);
	if (b == NULL)
		return (1);

	ca = sql_value_class(a);
	cb = sql_value_class(b);
	if (ca != cb)
		return (ca < cb ? -1 : 1);
	if (ca == SQL_CMP_NULL)
		return (0);

	if (ca == SQL_CMP_NUMERIC) {
		if (SQL_TYPE_IS_FLOAT(a->type) ||
		    SQL_TYPE_IS_FLOAT(b->type)) {
			ret = sql_value_numeric(a, &da);
			if (ret != SQL_OK)
				return (-1);
			ret = sql_value_numeric(b, &db);
			if (ret != SQL_OK)
				return (1);
			if (da < db)
				return (-1);
			return (da > db ? 1 : 0);
		}
		ia = a->ival;
		ib = b->ival;
		if (ia < ib)
			return (-1);
		return (ia > ib ? 1 : 0);
	}

	if (ca == SQL_CMP_TEMPORAL) {
		/*
		 * Only values of the very same temporal type compare
		 * numerically, everything else falls back to the ISO
		 * text representation which sorts chronologically.
		 */
		if (a->type == b->type) {
			if (a->ival < b->ival)
				return (-1);
			return (a->ival > b->ival ? 1 : 0);
		}
	}

	/* Strings, binary data and mixed temporal values. */
	limit = (a->slen < b->slen ? a->slen : b->slen);
	for (i = 0; i < limit; i++) {
		if (a->sval[i] != b->sval[i])
			return (a->sval[i] < b->sval[i] ? -1 : 1);
	}
	if (a->slen == b->slen)
		return (0);
	return (a->slen < b->slen ? -1 : 1);
}

/********************************************************************
 * Catalog friendly encoding of a single value
 *
 * The catalog stores DEFAULT values as text, so a value is encoded as
 *   [1 byte isnull][1 byte type id][payload]
 * and the whole thing is printed as hexadecimal digits.  The payload is
 * the 8 byte big endian magnitude for numbers, a length prefixed byte
 * string otherwise.
 ********************************************************************/
static void put_be64(unsigned char *p, uint64 v) {
	int i;

	for (i = 0; i < 8; i++)
		p[i] = (unsigned char)((v >> (56 - 8 * i)) & 0xFF);
}

static uint64 get_be64(const unsigned char *p) {
	uint64 v;
	int i;

	v = 0;
	for (i = 0; i < 8; i++)
		v = (v << 8) | (uint64)p[i];

	return (v);
}

static void put_be32(unsigned char *p, uint32 v) {
	int i;

	for (i = 0; i < 4; i++)
		p[i] = (unsigned char)((v >> (24 - 8 * i)) & 0xFF);
}

static uint32 get_be32(const unsigned char *p) {
	uint32 v;
	int i;

	v = 0;
	for (i = 0; i < 4; i++)
		v = (v << 8) | (uint32)p[i];

	return (v);
}

int sql_value_to_hex(const SQL_VALUE *val, char **sp) {
	unsigned char *buf, *out;
	size_t blen, i;
	char *res;
	uint64 mag;

	if (val == NULL || sp == NULL)
		return (SQL_ERR_ARG);
	*sp = NULL;

	blen = 2;
	if (!val->isnull) {
		if (SQL_TYPE_IS_STRING(val->type) ||
		    SQL_TYPE_IS_BINARY(val->type))
			blen += 4 + val->slen;
		else
			blen += 8;
	}
	buf = sql_calloc(blen, 1);
	if (buf == NULL)
		return (SQL_ERR_NOMEM);

	buf[0] = (unsigned char)(val->isnull ? 0 : 1);
	buf[1] = (unsigned char)val->type;
	if (!val->isnull) {
		if (SQL_TYPE_IS_STRING(val->type) ||
		    SQL_TYPE_IS_BINARY(val->type)) {
			put_be32(buf + 2, (uint32)val->slen);
			if (val->slen > 0)
				memcpy(buf + 6, val->sval, val->slen);
		} else {
			if (SQL_TYPE_IS_FLOAT(val->type))
				memcpy(&mag, &val->dval, sizeof(mag));
			else
				mag = (uint64)val->ival;
			put_be64(buf + 2, mag);
		}
	}

	res = sql_calloc(blen * 2 + 1, 1);
	if (res == NULL) {
		sql_free(buf);
		return (SQL_ERR_NOMEM);
	}
	out = buf;
	for (i = 0; i < blen; i++) {
		static const char digits[] = "0123456789ABCDEF";
		res[i * 2] = digits[(out[i] >> 4) & 0x0F];
		res[i * 2 + 1] = digits[out[i] & 0x0F];
	}
	sql_free(buf);
	*sp = res;

	return (SQL_OK);
}

static int hexval(char c) {

	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return (-1);
}

int sql_value_from_hex(const char *hex, SQL_VALUE *val) {
	unsigned char *buf;
	size_t hlen, blen, i;
	int hi, lo;
	uint64 mag;
	double dval;
	int ret;
	sql_type_t type;

	if (hex == NULL || val == NULL)
		return (SQL_ERR_ARG);
	hlen = strlen(hex);
	if (hlen == 0 || (hlen % 2) != 0)
		return (SQL_ERR_CATALOG);
	blen = hlen / 2;
	if (blen < 2)
		return (SQL_ERR_CATALOG);

	buf = sql_calloc(blen, 1);
	if (buf == NULL)
		return (SQL_ERR_NOMEM);
	for (i = 0; i < blen; i++) {
		hi = hexval(hex[i * 2]);
		lo = hexval(hex[i * 2 + 1]);
		if (hi < 0 || lo < 0) {
			sql_free(buf);
			return (SQL_ERR_CATALOG);
		}
		buf[i] = (unsigned char)((hi << 4) | lo);
	}

	type = (sql_type_t)buf[1];
	sql_value_clear(val);
	if (buf[0] == 0) {
		sql_free(buf);
		return (sql_value_set_null(val, type));
	}

	ret = SQL_OK;
	if (SQL_TYPE_IS_STRING(type) || SQL_TYPE_IS_BINARY(type)) {
		uint32 len;

		if (blen < 6) {
			sql_free(buf);
			return (SQL_ERR_CATALOG);
		}
		len = get_be32(buf + 2);
		if (blen != 6 + len) {
			sql_free(buf);
			return (SQL_ERR_CATALOG);
		}
		ret = sql_value_set_string(val, type, buf + 6, len);
	} else {
		if (blen != 10) {
			sql_free(buf);
			return (SQL_ERR_CATALOG);
		}
		mag = get_be64(buf + 2);
		if (SQL_TYPE_IS_FLOAT(type)) {
			memcpy(&dval, &mag, sizeof(dval));
			ret = sql_value_set_double(val, type, dval);
		} else
			ret = sql_value_set_int(val, type, (int64)mag);
	}
	sql_free(buf);

	return (ret);
}

/********************************************************************
 * Columns & schemas
 ********************************************************************/
void sql_column_init(SQL_COLUMN *col) {

	memset(col, 0, sizeof(*col));
	col->type = SQL_T_NULL;
	sql_value_init(&col->defval);
}

void sql_column_clear(SQL_COLUMN *col) {

	if (col == NULL)
		return;
	sql_free(col->name);
	sql_free(col->lname);
	sql_value_clear(&col->defval);
	memset(col, 0, sizeof(*col));
}

void sql_schema_free(SQL_SCHEMA *schema) {
	size_t i;

	if (schema == NULL)
		return;
	sql_free(schema->name);
	sql_free(schema->lname);
	for (i = 0; i < schema->ncols; i++)
		sql_column_clear(&schema->cols[i]);
	sql_free(schema->cols);
	sql_free(schema);
}

/********************************************************************
 * Result sets
 ********************************************************************/
int sql_resultset_alloc(size_t ncols, SQL_RESULTSET **rsp) {
	SQL_RESULTSET *rs;

	if (rsp == NULL || ncols == 0)
		return (SQL_ERR_ARG);
	rs = sql_calloc(1, sizeof(SQL_RESULTSET));
	if (rs == NULL)
		return (SQL_ERR_NOMEM);
	rs->ncols = ncols;
	rs->colnames = sql_calloc(ncols, sizeof(char *));
	if (rs->colnames == NULL) {
		sql_resultset_free(rs);
		return (SQL_ERR_NOMEM);
	}
	rs->caprows = 16;
	rs->rows = sql_calloc(rs->caprows, sizeof(SQL_VALUE *));
	if (rs->rows == NULL) {
		sql_resultset_free(rs);
		return (SQL_ERR_NOMEM);
	}
	*rsp = rs;

	return (SQL_OK);
}

void sql_resultset_free(SQL_RESULTSET *rs) {
	size_t i, j;

	if (rs == NULL)
		return;
	for (i = 0; i < rs->ncols; i++)
		sql_free(rs->colnames[i]);
	sql_free(rs->colnames);
	for (i = 0; i < rs->nrows; i++) {
		if (rs->rows[i] != NULL) {
			for (j = 0; j < rs->ncols; j++)
				sql_value_clear(&rs->rows[i][j]);
			sql_free(rs->rows[i]);
		}
	}
	sql_free(rs->rows);
	sql_free(rs);
}

int sql_resultset_setcolnames(SQL_RESULTSET *rs, char **names) {
	size_t i;

	if (rs == NULL || names == NULL)
		return (SQL_ERR_ARG);
	for (i = 0; i < rs->ncols; i++) {
		sql_free(rs->colnames[i]);
		rs->colnames[i] = sql_strdup(names[i]);
		if (rs->colnames[i] == NULL)
			return (SQL_ERR_NOMEM);
	}

	return (SQL_OK);
}

int sql_resultset_addrow(SQL_RESULTSET *rs, const SQL_VALUE *vals) {
	SQL_VALUE *row;
	SQL_VALUE **newv;
	size_t i, newcap;

	if (rs == NULL || vals == NULL)
		return (SQL_ERR_ARG);

	if (rs->nrows == rs->caprows) {
		newcap = rs->caprows * 2;
		newv = realloc(rs->rows, newcap * sizeof(SQL_VALUE *));
		if (newv == NULL)
			return (SQL_ERR_NOMEM);
		rs->rows = newv;
		rs->caprows = newcap;
	}

	row = sql_calloc(rs->ncols, sizeof(SQL_VALUE));
	if (row == NULL)
		return (SQL_ERR_NOMEM);
	for (i = 0; i < rs->ncols; i++) {
		size_t k;

		sql_value_init(&row[i]);
		if (sql_value_copy(&vals[i], &row[i]) != SQL_OK) {
			for (k = 0; k < rs->ncols; k++)
				sql_value_clear(&row[k]);
			sql_free(row);
			return (SQL_ERR_NOMEM);
		}
	}
	rs->rows[rs->nrows++] = row;

	return (SQL_OK);
}

/*
 * Print a result set as an aligned table, NULLs render as an empty
 * field.  Only used when no explicit output format was requested.
 */
void sql_resultset_print(SQL_RESULTSET *rs, FILE *f) {
	char ***text;
	size_t *width;
	size_t i, j, len, total;
	int ret;

	if (rs == NULL || f == NULL)
		return;

	text = (char ***)sql_calloc(rs->nrows + 1, sizeof(char **));
	width = sql_calloc(rs->ncols, sizeof(size_t));
	if (text == NULL || width == NULL) {
		sql_free(text);
		sql_free(width);
		return;
	}

	for (i = 0; i < rs->ncols; i++) {
		len = 0;
		if (rs->colnames[i] != NULL)
			len = strlen(rs->colnames[i]);
		if (len < 1)
			len = 1;
		width[i] = len;
	}

	ret = SQL_OK;
	for (j = 0; j < rs->nrows; j++) {
		text[j] = (char **)sql_calloc(rs->ncols, sizeof(char *));
		if (text[j] == NULL) {
			ret = SQL_ERR_NOMEM;
			break;
		}
		for (i = 0; i < rs->ncols; i++) {
			char *s;
			size_t l;

			s = NULL;
			l = 0;
			if (sql_value_tostring(&rs->rows[j][i], &s) == SQL_OK)
				l = (s == NULL ? 0 : strlen(s));
			if (l < 1) {
				l = 4;	/* The width of "NULL".	*/
				if (rs->rows[j][i].isnull)
					l = 0;
			}
			text[j][i] = s;
			if (l > width[i])
				width[i] = l;
		}
	}

	if (ret == SQL_OK) {
		total = 0;
		for (i = 0; i < rs->ncols; i++) {
			if (i != 0)
				(void)fprintf(f, " ");
			(void)fprintf(f, "%-*s", (int)width[i],
			    rs->colnames[i] == NULL ? "" : rs->colnames[i]);
			total += width[i] + (i != 0 ? 1 : 0);
		}
		(void)fprintf(f, "\n");
		for (i = 0; i < total; i++)
			(void)fprintf(f, "-");
		(void)fprintf(f, "\n");
		for (j = 0; j < rs->nrows; j++) {
			for (i = 0; i < rs->ncols; i++) {
				const char *s;
				if (i != 0)
					(void)fprintf(f, " ");
				s = text[j][i];
				if (s == NULL) {
					if (rs->rows[j][i].isnull)
						s = "";
					else
						s = "NULL";
				}
				(void)fprintf(f, "%-*s", (int)width[i], s);
			}
			(void)fprintf(f, "\n");
		}
	}

	for (j = 0; j < rs->nrows + 1; j++) {
		if (text[j] != NULL) {
			for (i = 0; i < rs->ncols; i++)
				sql_free(text[j][i]);
			sql_free(text[j]);
		}
	}
	sql_free(text);
	sql_free(width);
}
