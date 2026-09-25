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
 * Keywords.  Everything that is not listed here is an ordinary
 * identifier, which keeps the grammar tolerant towards user identifiers
 * that happen to look like reserved words.
 */
typedef struct _sql_kwent {
	const char	*name;
	sql_kw		kw;
} SQL_KWENT;

static SQL_KWENT sql_keywords[] = {
	{ "ADD",		KW_ADD			},
	{ "ALL",		KW_ALL			},
	{ "AND",		KW_AND			},
	{ "AS",			KW_AS			},
	{ "ASC",		KW_ASC			},
	{ "BETWEEN",		KW_BETWEEN		},
	{ "BIGINT",		KW_BIGINT		},
	{ "BINARY",		KW_BINARY		},
	{ "BLOB",		KW_BLOB			},
	{ "BY",			KW_BY			},
	{ "CHAR",		KW_CHAR			},
	{ "CREATE",		KW_CREATE		},
	{ "CURRENT_DATE",	KW_CURRENT_DATE		},
	{ "CURRENT_TIME",	KW_CURRENT_TIME		},
	{ "CURRENT_TIMESTAMP",	KW_CURRENT_TIMESTAMP	},
	{ "DATE",		KW_DATE			},
	{ "DEFAULT",		KW_DEFAULT		},
	{ "DELETE",		KW_DELETE		},
	{ "DESC",		KW_DESC			},
	{ "DISTINCT",		KW_DISTINCT		},
	{ "DOUBLE",		KW_DOUBLE		},
	{ "DROP",		KW_DROP			},
	{ "EXISTS",		KW_EXISTS		},
	{ "FLOAT",		KW_FLOAT		},
	{ "FROM",		KW_FROM			},
	{ "IF",			KW_IF			},
	{ "IN",			KW_IN			},
	{ "INSERT",		KW_INSERT		},
	{ "INT",		KW_INT			},
	{ "INTEGER",		KW_INT			},
	{ "INTO",		KW_INTO			},
	{ "IS",			KW_IS			},
	{ "KEY",		KW_KEY			},
	{ "LIKE",		KW_LIKE			},
	{ "LIMIT",		KW_LIMIT		},
	{ "NOT",		KW_NOT			},
	{ "NULL",		KW_NULL			},
	{ "NUMERIC",		KW_DOUBLE		},
	{ "OFFSET",		KW_OFFSET		},
	{ "OR",			KW_OR			},
	{ "ORDER",		KW_ORDER		},
	{ "PRECISION",		KW_NONE			},
	{ "PRIMARY",		KW_PRIMARY		},
	{ "REAL",		KW_FLOAT		},
	{ "SELECT",		KW_SELECT		},
	{ "SET",		KW_SET			},
	{ "SMALLINT",		KW_SMALLINT		},
	{ "TABLE",		KW_TABLE		},
	{ "TEXT",		KW_TEXT			},
	{ "TIME",		KW_TIME			},
	{ "TIMESTAMP",		KW_TIMESTAMP		},
	{ "UPDATE",		KW_UPDATE		},
	{ "VALUES",		KW_VALUES		},
	{ "VARBINARY",		KW_VARBINARY		},
	{ "VARCHAR",		KW_VARCHAR		},
	{ "WHERE",		KW_WHERE		},
	{ NULL,			KW_NONE			}
};

static sql_kw sql_kw_lookup_exact(const char *text, size_t len) {
	SQL_KWENT *ent;
	size_t i;

	for (i = 0; sql_keywords[i].name != NULL; i++) {
		ent = &sql_keywords[i];
		if (strlen(ent->name) != len)
			continue;
		if (strncasecmp(ent->name, text, len) == 0)
			return (ent->kw);
	}

	return (KW_NONE);
}

int sql_kw_lookup(const char *text, size_t len) {

	/*
	 * Underscored variants: CURRENT_DATE and CURRENT_TIME are also
	 * accepted spelled with a blank.
	 */
	if (len == 11 && strncasecmp(text, "CURRENT DATE", len) == 0)
		return (KW_CURRENT_DATE);
	if (len == 11 && strncasecmp(text, "CURRENT TIME", len) == 0)
		return (KW_CURRENT_TIME);

	return (sql_kw_lookup_exact(text, len));
}

static int sql_ts_init(SQL_TOKENSTREAM **tsp, const char *sql, size_t len) {
	SQL_TOKENSTREAM *ts;

	ts = sql_calloc(1, sizeof(SQL_TOKENSTREAM));
	if (ts == NULL)
		return (SQL_ERR_NOMEM);
	ts->input = sql_strndup(sql, len);
	if (ts->input == NULL) {
		sql_free(ts);
		return (SQL_ERR_NOMEM);
	}
	ts->capacity = 32;
	ts->tokens = sql_calloc(ts->capacity, sizeof(SQL_TOKEN));
	if (ts->tokens == NULL) {
		sql_free(ts->input);
		sql_free(ts);
		return (SQL_ERR_NOMEM);
	}

	*tsp = ts;
	return (SQL_OK);
}

static int sql_ts_addtoken(SQL_TOKENSTREAM *ts, SQL_TOKEN *tk) {
	int ret;
	size_t need;
	SQL_TOKEN *newv;

	ret = SQL_OK;
	need = ts->ntokens + 2;	/* The new token plus room for EOF. */
	if (need > ts->capacity) {
		size_t newcap = ts->capacity;
		while (newcap < need)
			newcap *= 2;
		newv = realloc(ts->tokens, newcap * sizeof(SQL_TOKEN));
		if (newv == NULL)
			return (SQL_ERR_NOMEM);
		ts->tokens = newv;
		ts->capacity = newcap;
	}
	ts->tokens[ts->ntokens++] = *tk;

	return (ret);
}

void sql_tokenstream_free(SQL_TOKENSTREAM *ts) {
	size_t i;

	if (ts == NULL)
		return;
	for (i = 0; i < ts->ntokens; i++)
		sql_free(ts->tokens[i].sval);
	sql_free(ts->tokens);
	sql_free(ts->input);
	sql_free(ts);
}

SQL_TOKEN *sql_ts_peek(SQL_TOKENSTREAM *ts, size_t ahead) {
	size_t indx;

	if (ts == NULL)
		return (NULL);
	indx = ts->pos + ahead;
	if (indx >= ts->ntokens)
		indx = ts->ntokens - 1;	/* Always at least the EOF token. */

	return (&ts->tokens[indx]);
}

void sql_ts_consume(SQL_TOKENSTREAM *ts) {

	if (ts == NULL)
		return;
	if (ts->pos < ts->ntokens - 1)
		ts->pos++;
}

static int is_idstart(int c) {

	return (isalpha(c) || c == '_');
}

static int is_idchar(int c) {

	return (isalnum(c) || c == '_');
}

static int is_punct_start(int c) {

	return (strchr("+-*/%=<>(),;.|", c) != NULL);
}

static int is_digit_char(int c) {

	return (c >= '0' && c <= '9');
}

/* Convert one hexadecimal character pair into a byte. */
static int sql_hexval(int c) {

	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return (-1);
}

/*
 * Read a single quoted string starting at 'p' (which points at the
 * opening quote).  Returns the unescaped value in '*valp'.
 */
static int sql_lex_quoted(const char *p, size_t end, char quote, int expand,
    unsigned char **valp, size_t *lenp, size_t *consumedp) {
	size_t i, j, n, rlen;
	unsigned char *res;

	i = 1;		/* Skip the opening quote. */
	j = 1;
	n = 1;		/* Room for the trailing NUL. */
	rlen = 0;
	res = NULL;
	while (i < end) {
		if (p[i] == quote) {
			if (i + 1 < end && p[i + 1] == quote) {
				n++;
				i += 2;
				continue;
			}
			i++;
			goto done;
		}
		if (expand && p[i] == '\\' && i + 1 < end) {
			n++;
			i += 2;
			continue;
		}
		n++;
		i++;
	}

	/* Unterminated literal. */
	return (SQL_ERR_LEX);

done:
	res = sql_calloc(n + 1, 1);
	if (res == NULL)
		return (SQL_ERR_NOMEM);
	/*
	 * 'i' already points past the closing quote, the copy pass uses
	 * its own cursor.
	 */
	for (j = 1; j < end;) {
		if (p[j] == quote) {
			if (j + 1 < end && p[j + 1] == quote) {
				res[rlen++] = (unsigned char)quote;
				j += 2;
				continue;
			}
			break;
		}
		if (expand && p[j] == '\\' && j + 1 < end) {
			int c = p[j + 1];
			switch (c) {
				case 'n': res[rlen++] = '\n'; break;
				case 't': res[rlen++] = '\t'; break;
				case 'r': res[rlen++] = '\r'; break;
				case '0': res[rlen++] = '\0'; break;
				case '\\': res[rlen++] = '\\'; break;
				default: res[rlen++] = (unsigned char)c; break;
			}
			j += 2;
			continue;
		}
		res[rlen++] = (unsigned char)p[j];
		j++;
	}
	res[rlen] = '\0';
	*valp = res;
	*lenp = rlen;
	*consumedp = i;

	return (SQL_OK);
}

/*
 * Read a hexadecimal or binary literal such as X'AABB'.  'p' points at
 * the type introducer.
 */
static int sql_lex_bitstring(const char *p, size_t end, unsigned char **valp,
    size_t *lenp, size_t *consumedp) {
	size_t i, n, rlen, skip;
	unsigned char *res;
	int binary;

	binary = (p[0] == 'B' || p[0] == 'b');
	i = 0;
	/* Locate the opening quote. */
	while (i < end && p[i] != '\'') {
		if (!is_idchar(p[i]))
			return (SQL_ERR_LEX);
		i++;
	}
	if (i >= end)
		return (SQL_ERR_LEX);
	i++;	/* Now past the quote. */

	n = (end - i) / (binary ? 8 : 2) + 1;
	res = sql_calloc(n + 1, 1);
	if (res == NULL)
		return (SQL_ERR_NOMEM);

	rlen = 0;
	while (i < end && p[i] != '\'') {
		int b, k, bits, got;
		b = 0;
		got = 0;
		bits = binary ? 8 : 2;
		for (k = 0; k < bits && i < end && p[i] != '\''; k++) {
			int v;
			v = sql_hexval(p[i]);
			if (v < 0 || (binary && v > 1)) {
				sql_free(res);
				return (SQL_ERR_LEX);
			}
			b = (b << (binary ? 1 : 4)) | v;
			got = 1;
			i++;
		}
		if (got == 0) {
			sql_free(res);
			return (SQL_ERR_LEX);
		}
		res[rlen++] = (unsigned char)b;
	}
	if (i >= end) {
		sql_free(res);
		return (SQL_ERR_LEX);
	}
	skip = i + 1;	/* Plus the closing quote. */

	*valp = res;
	*lenp = rlen;
	*consumedp = skip;

	return (SQL_OK);
}

/*
 * Tokenize 'sql' (the first 'len' bytes, or the whole NUL terminated
 * string when 'len' is zero) into '*tsp'.
 */
int sql_lex(const char *sql, size_t len, SQL_TOKENSTREAM **tsp) {
	const char *p;
	size_t i, end, start, consumed;
	int ret;
	SQL_TOKENSTREAM *ts;
	SQL_TOKEN tk;
	unsigned char *sval;
	size_t slen;

	ret = SQL_OK;
	ts = NULL;
	p = NULL;
	i = 0;
	end = (len == 0 ? strlen(sql) : len);

	if (sql == NULL)
		return (SQL_ERR_ARG);

	ret = sql_ts_init(&ts, sql, end);
	if (ret != SQL_OK)
		return (ret);
	p = ts->input;

	while (i < end) {
		/* White space */
		if (isspace((unsigned char)p[i])) {
			i++;
			continue;
		}
		/* Comments */
		if (i + 1 < end && p[i] == '-' && p[i + 1] == '-') {
			i += 2;
			while (i < end && p[i] != '\n')
				i++;
			continue;
		}
		if (i + 1 < end && p[i] == '#' && p[i + 1] != '\'') {
			i++;
			while (i < end && p[i] != '\n')
				i++;
			continue;
		}
		if (i + 1 < end && p[i] == '/' && p[i + 1] == '*') {
			i += 2;
			while (i + 1 < end &&
			    !(p[i] == '*' && p[i + 1] == '/'))
				i++;
			if (i + 1 >= end) {
				ret = SQL_ERR_LEX;
				goto err;
			}
			i += 2;
			continue;
		}

		memset(&tk, 0, sizeof(tk));
		tk.off = i;

		/* Quoted identifiers */
		if (p[i] == '"' || p[i] == '`') {
			sval = NULL;
			ret = sql_lex_quoted(&p[i], end - i, p[i], 0,
			    &sval, &slen, &consumed);
			if (ret != SQL_OK)
				goto err;
			tk.type = TK_IDENT;
			tk.kw = KW_NONE;
			tk.text = (const char *)sval;
			tk.len = slen;
			tk.sval = sval;	/* Owned copy, freed with the stream */
			tk.slen = slen;
			i += consumed;
			ret = sql_ts_addtoken(ts, &tk);
			if (ret != SQL_OK)
				goto err;
			continue;
		}

		/* Strings */
		if (p[i] == '\'') {
			sval = NULL;
			ret = sql_lex_quoted(&p[i], end - i, '\'', 0,
			    &sval, &slen, &consumed);
			if (ret != SQL_OK)
				goto err;
			tk.type = TK_STRING;
			tk.text = &p[i];
			tk.len = consumed;
			tk.sval = sval;
			tk.slen = slen;
			i += consumed;
			ret = sql_ts_addtoken(ts, &tk);
			if (ret != SQL_OK)
				goto err;
			continue;
		}

		/* Hexadecimal/binary literals */
		if ((p[i] == 'X' || p[i] == 'x' || p[i] == 'B' ||
		    p[i] == 'b') && i + 1 < end && p[i + 1] == '\'') {
			sval = NULL;
			ret = sql_lex_bitstring(&p[i], end - i,
			    &sval, &slen, &consumed);
			if (ret != SQL_OK)
				goto err;
			tk.type = TK_BLOB;
			tk.text = &p[i];
			tk.len = consumed;
			tk.sval = sval;
			tk.slen = slen;
			i += consumed;
			ret = sql_ts_addtoken(ts, &tk);
			if (ret != SQL_OK)
				goto err;
			continue;
		}

		/* Numbers */
		if (is_digit_char(p[i]) ||
		    (p[i] == '.' && i + 1 < end && is_digit_char(p[i + 1]))) {
			int isfloat = 0;
			size_t n = i;

			while (n < end && is_digit_char(p[n]))
				n++;
			if (n < end && p[n] == '.') {
				isfloat = 1;
				n++;
				while (n < end && is_digit_char(p[n]))
					n++;
			}
			if (n < end && (p[n] == 'e' || p[n] == 'E')) {
				size_t m = n + 1;
				if (m < end && (p[m] == '+' || p[m] == '-'))
					m++;
				if (m < end && is_digit_char(p[m])) {
					isfloat = 1;
					n = m;
					while (n < end && is_digit_char(p[n]))
						n++;
				}
			}
			tk.type = isfloat ? TK_FLOAT : TK_INTEGER;
			tk.text = &p[i];
			tk.len = n - i;
			if (isfloat)
				tk.fval = strtod(p + i, NULL);
			else
				tk.ival = (int64)strtoll(p + i, NULL, 10);
			i = n;
			ret = sql_ts_addtoken(ts, &tk);
			if (ret != SQL_OK)
				goto err;
			continue;
		}

		/* Identifiers and keywords */
		if (is_idstart((unsigned char)p[i])) {
			start = i;
			while (i < end && is_idchar((unsigned char)p[i]))
				i++;
			tk.type = TK_IDENT;
			tk.text = &p[start];
			tk.len = i - start;
			tk.kw = sql_kw_lookup(tk.text, tk.len);
			ret = sql_ts_addtoken(ts, &tk);
			if (ret != SQL_OK)
				goto err;
			continue;
		}

		/* Punctuation */
		if (is_punct_start(p[i])) {
			size_t plen = 1;
			char two[3];

			if (i + 1 < end) {
				two[0] = p[i];
				two[1] = p[i + 1];
				two[2] = '\0';
				if (strcmp(two, "<=") == 0 ||
				    strcmp(two, ">=") == 0 ||
				    strcmp(two, "<>") == 0 ||
				    strcmp(two, "!=") == 0 ||
				    strcmp(two, "||") == 0)
					plen = 2;
			}
			tk.type = TK_PUNCT;
			tk.text = &p[i];
			tk.len = plen;
			i += plen;
			ret = sql_ts_addtoken(ts, &tk);
			if (ret != SQL_OK)
				goto err;
			continue;
		}

		/* '!' on its own is only valid as part of '!=' */
		ret = SQL_ERR_LEX;
		goto err;
	}

	/* Always terminate the stream with an EOF token. */
	memset(&tk, 0, sizeof(tk));
	tk.type = TK_EOF;
	tk.off = end;
	ret = sql_ts_addtoken(ts, &tk);
	if (ret != SQL_OK)
		goto err;

	*tsp = ts;
	return (SQL_OK);

err:
	sql_tokenstream_free(ts);
	return (ret);
}
