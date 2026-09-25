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
 * The catalog.
 *
 * The VanDB engine does not remember which tables exist in a store: a
 * table is simply a directory-less set of files opened by name and the
 * recovery log refers to tables by their id.  The SQL layer therefore
 * keeps its own catalog in a dedicated table:
 *
 *	key   "__van_sql_tables"	-> newline separated table names
 *	key   <table name>	-> serialized schema (see below)
 *
 * The serialized schema is text so that it stays readable in the data
 * files, one record per line, fields separated by tabs:
 *
 *	V<version>
 *	T<table name>
 *	C<name>\t<type id>\t<length>\t<not null>\t<pk>\t<has default>\t<hex>
 *	R<next row id>
 */
#define	SQL_TABLELIST_KEY	"__van_sql_tables"
#define	SQL_RESERVED_PREFIX	"__van_"

/* In-memory threshold for the tables we open. */
#define	SQL_TABLE_THRESHOLD	(64 * 1024 * 1024)

/********************************************************************
 * Table handles
 ********************************************************************/
int sql_table_alloc(SQL_SESSION *sess, const char *name, SQL_SCHEMA *schema,
    SQL_TABLE **tbp) {
	SQL_TABLE *tb;
	size_t len;

	if (sess == NULL || name == NULL || tbp == NULL)
		return (SQL_ERR_ARG);
	len = strlen(name);
	if (len == 0 || len > SQL_MAX_IDENT_LEN)
		return (SQL_ERR_ARG);
	if (strncasecmp(name, SQL_RESERVED_PREFIX,
	    strlen(SQL_RESERVED_PREFIX)) == 0)
		return (SQL_ERR_ARG);

	tb = sql_calloc(1, sizeof(SQL_TABLE));
	if (tb == NULL)
		return (SQL_ERR_NOMEM);
	tb->name = sql_strdup(name);
	tb->lname = sql_strlower(name, len);
	tb->kvname = sql_strdup(name);
	if (tb->name == NULL || tb->lname == NULL || tb->kvname == NULL) {
		sql_table_free(tb);
		return (SQL_ERR_NOMEM);
	}
	tb->schema = schema;
	tb->table = NULL;
	tb->opened = 0;

	*tbp = tb;
	return (SQL_OK);
}

void sql_table_free(SQL_TABLE *tb) {

	if (tb == NULL)
		return;
	sql_free(tb->name);
	sql_free(tb->lname);
	sql_free(tb->kvname);
	sql_schema_free(tb->schema);
	tb->schema = NULL;
	sql_free(tb);
}

int sql_table_open(SQL_SESSION *sess, SQL_TABLE *tb) {
	VAN_TABLE *table;
	int ret;

	if (sess == NULL || tb == NULL || tb->kvname == NULL)
		return (SQL_ERR_ARG);
	if (tb->opened)
		return (SQL_OK);

	ret = van_table_create((VAN_STORE *)sess->store, &table);
	if (ret != 0)
		return (SQL_ERR_STORE);
	ret = table->set_inmem_threshold(table, SQL_TABLE_THRESHOLD);
	if (ret != 0)
		return (SQL_ERR_STORE);
	ret = table->open(table, tb->kvname, VAN_TABLE_SKIP_THREAD_START);
	if (ret != 0)
		return (SQL_ERR_STORE);
	ret = table->start_threads(table);
	if (ret != 0)
		return (SQL_ERR_STORE);

	tb->table = table;
	tb->opened = 1;

	return (SQL_OK);
}

int sql_table_ensure_open(SQL_SESSION *sess, SQL_TABLE *tb) {

	if (tb == NULL)
		return (SQL_ERR_ARG);
	if (tb->opened)
		return (SQL_OK);

	return (sql_table_open(sess, tb));
}

int sql_table_close(SQL_TABLE *tb) {
	int ret;

	if (tb == NULL)
		return (SQL_OK);
	ret = SQL_OK;
	if (tb->opened && tb->table != NULL) {
		if (tb->table->close(tb->table, 0) != 0)
			ret = SQL_ERR_STORE;
		tb->table = NULL;
		tb->opened = 0;
	}

	return (ret);
}

SQL_TABLE *sql_session_find(SQL_SESSION *sess, const char *lname) {
	size_t i;

	if (sess == NULL || lname == NULL)
		return (NULL);
	for (i = 0; i < sess->ntables; i++) {
		if (sess->tables[i] != NULL &&
		    strcmp(sess->tables[i]->lname, lname) == 0)
			return (sess->tables[i]);
	}

	return (NULL);
}

/********************************************************************
 * Key/value access
 ********************************************************************/
static void datum_key(uint64 rowid, VAN_DATUM *key, unsigned char *keybuf) {

	sql_rowkey_encode(rowid, keybuf);
	memset(key, 0, sizeof(*key));
	key->data = keybuf;
	key->size = SQL_KVSIZE;
	key->ulen = SQL_KVSIZE;
	key->flags = 0;
}

int sql_kv_put(SQL_SESSION *sess, SQL_TABLE *tb, uint64 rowid,
    const void *data, size_t len) {
	VAN_DATUM key, val;
	unsigned char keybuf[SQL_KVSIZE];
	int ret;

	if (tb == NULL || tb->table == NULL)
		return (SQL_ERR_STORE);
	if (data == NULL)
		len = 0;

	datum_key(rowid, &key, keybuf);
	memset(&val, 0, sizeof(val));
	val.data = (void *)data;
	val.size = len;
	val.ulen = len;
	val.flags = 0;

	(void)sess;
	ret = tb->table->put(tb->table, &key, &val, 0);
	if (ret != 0)
		return (SQL_ERR_STORE);

	return (SQL_OK);
}

/*
 * Read a row.  '*datap' is NULL when the row does not exist, otherwise
 * it points at engine memory owned by the caller, release it with
 * sql_kv_free().
 */
int sql_kv_get(SQL_SESSION *sess, SQL_TABLE *tb, uint64 rowid,
    void **datap, size_t *lenp) {
	VAN_DATUM key, val;
	unsigned char keybuf[SQL_KVSIZE];
	int ret;

	if (tb == NULL || tb->table == NULL || datap == NULL)
		return (SQL_ERR_STORE);
	*datap = NULL;
	if (lenp != NULL)
		*lenp = 0;
	(void)sess;

	datum_key(rowid, &key, keybuf);
	memset(&val, 0, sizeof(val));
	val.data = NULL;
	val.size = 0;
	val.ulen = 0;
	val.flags = VAN_MALLOC;

	ret = tb->table->get(tb->table, &key, &val, 0);
	if (ret == VAN_NOTFOUND || ret == VAN_DELETED) {
		if (val.data != NULL)
			_van_datum_copyfree(&val);
		return (SQL_OK);
	}
	if (ret != 0) {
		if (val.data != NULL)
			_van_datum_copyfree(&val);
		return (SQL_ERR_STORE);
	}

	*datap = val.data;
	if (lenp != NULL)
		*lenp = val.size;

	return (SQL_OK);
}

void sql_kv_free(void *p) {

	if (p != NULL)
		_van_free(NULL, p);
}

int sql_kv_del(SQL_SESSION *sess, SQL_TABLE *tb, uint64 rowid) {
	VAN_DATUM key;
	unsigned char keybuf[SQL_KVSIZE];
	int ret;

	if (tb == NULL || tb->table == NULL)
		return (SQL_ERR_STORE);
	(void)sess;

	datum_key(rowid, &key, keybuf);
	ret = tb->table->del(tb->table, &key, 0);
	if (ret != 0 && ret != VAN_NOTFOUND)
		return (SQL_ERR_STORE);

	return (SQL_OK);
}

/*
 * The catalog is keyed by name instead of by row id, so it gets its own
 * pair of helpers.
 */
static int cat_put(SQL_SESSION *sess, const char *key, const void *data,
    size_t len) {
	VAN_DATUM k, v;
	int ret;

	if (sess == NULL || sess->catalog == NULL ||
	    sess->catalog->table == NULL)
		return (SQL_ERR_STORE);

	memset(&k, 0, sizeof(k));
	k.data = (void *)key;
	k.size = strlen(key);
	k.ulen = k.size;
	k.flags = 0;
	memset(&v, 0, sizeof(v));
	v.data = (void *)data;
	v.size = len;
	v.ulen = len;
	v.flags = 0;

	ret = sess->catalog->table->put(sess->catalog->table, &k, &v, 0);
	if (ret != 0)
		return (SQL_ERR_STORE);

	return (SQL_OK);
}

static int cat_get(SQL_SESSION *sess, const char *key, char **valp) {
	VAN_DATUM k, v;
	int ret;

	if (sess == NULL || sess->catalog == NULL ||
	    sess->catalog->table == NULL || valp == NULL)
		return (SQL_ERR_STORE);
	*valp = NULL;

	memset(&k, 0, sizeof(k));
	k.data = (void *)key;
	k.size = strlen(key);
	k.ulen = k.size;
	k.flags = 0;
	memset(&v, 0, sizeof(v));
	v.data = NULL;
	v.size = 0;
	v.ulen = 0;
	v.flags = VAN_MALLOC;

	ret = sess->catalog->table->get(sess->catalog->table, &k, &v, 0);
	if (ret == VAN_NOTFOUND || ret == VAN_DELETED) {
		if (v.data != NULL)
			_van_datum_copyfree(&v);
		return (SQL_OK);
	}
	if (ret != 0) {
		if (v.data != NULL)
			_van_datum_copyfree(&v);
		return (SQL_ERR_STORE);
	}
	if (v.data == NULL)
		return (SQL_OK);

	*valp = sql_strndup((const char *)v.data, v.size);
	_van_datum_copyfree(&v);
	if (*valp == NULL)
		return (SQL_ERR_NOMEM);

	return (SQL_OK);
}

static int cat_del(SQL_SESSION *sess, const char *key) {
	VAN_DATUM k;
	int ret;

	if (sess == NULL || sess->catalog == NULL ||
	    sess->catalog->table == NULL)
		return (SQL_ERR_STORE);

	memset(&k, 0, sizeof(k));
	k.data = (void *)key;
	k.size = strlen(key);
	k.ulen = k.size;
	k.flags = 0;
	ret = sess->catalog->table->del(sess->catalog->table, &k, 0);
	if (ret != 0 && ret != VAN_NOTFOUND)
		return (SQL_ERR_STORE);

	return (SQL_OK);
}

/********************************************************************
 * Serializing a schema
 ********************************************************************/
typedef struct _sql_strbuf {
	char	*buf;
	size_t	len;
	size_t	cap;
} SQL_STRBUF;

static int sb_need(SQL_STRBUF *sb, size_t extra) {
	size_t need;
	char *p;

	need = sb->len + extra + 1;
	if (need <= sb->cap)
		return (SQL_OK);
	while (sb->cap < need)
		sb->cap = (sb->cap == 0 ? 128 : sb->cap * 2);
	p = realloc(sb->buf, sb->cap);
	if (p == NULL)
		return (SQL_ERR_NOMEM);
	sb->buf = p;

	return (SQL_OK);
}

static int sb_addf(SQL_STRBUF *sb, const char *fmt, ...) {
	va_list ap;
	int n, ret;
	size_t room;

	/* Two passes so that the buffer always fits. */
	for (;;) {
		if (sb_need(sb, 256) != SQL_OK)
			return (SQL_ERR_NOMEM);
		room = sb->cap - sb->len;
		va_start(ap, fmt);
		n = vsnprintf(sb->buf + sb->len, room, fmt, ap);
		va_end(ap);
		if (n < 0)
			return (SQL_ERR_ARG);
		if ((size_t)n < room) {
			sb->len += (size_t)n;
			return (SQL_OK);
		}
		ret = sb_need(sb, (size_t)n + 1);
		if (ret != SQL_OK)
			return (ret);
	}
}

int sql_schema_serialize(SQL_SCHEMA *schema, char **textp) {
	SQL_STRBUF sb;
	size_t i;
	int ret;
	char *hex;

	if (schema == NULL || textp == NULL)
		return (SQL_ERR_ARG);
	*textp = NULL;

	memset(&sb, 0, sizeof(sb));
	ret = sb_addf(&sb, "V%s\n", SQL_SCHEMA_VERSION);
	if (ret != SQL_OK)
		goto err;
	ret = sb_addf(&sb, "T%s\n", schema->name);
	if (ret != SQL_OK)
		goto err;
	for (i = 0; i < schema->ncols; i++) {
		SQL_COLUMN *col;

		col = &schema->cols[i];
		hex = NULL;
		if (col->has_default) {
			ret = sql_value_to_hex(&col->defval, &hex);
			if (ret != SQL_OK)
				goto err;
		}
		ret = sb_addf(&sb, "C%s\t%u\t%u\t%d\t%d\t%d\t%s\n",
		    col->name, (unsigned)col->type, (unsigned)col->length,
		    col->not_null, col->primary_key, col->has_default,
		    (col->has_default && hex != NULL ? hex : "-"));
		sql_free(hex);
		if (ret != SQL_OK)
			goto err;
	}
	ret = sb_addf(&sb, "R%llu\n", (unsigned long long)schema->next_rowid);
	if (ret != SQL_OK)
		goto err;

	*textp = sb.buf;
	return (SQL_OK);

err:
	sql_free(sb.buf);
	return (ret);
}

/* Split a line into tab separated fields, returns the field count. */
static size_t split_fields(char *line, char **fields, size_t max) {
	size_t n;

	n = 0;
	while (*line != '\0' && n < max) {
		fields[n++] = line;
		while (*line != '\0' && *line != '\t')
			line++;
		if (*line == '\t') {
			*line = '\0';
			line++;
		}
	}

	return (n);
}

int sql_schema_deserialize(const char *text, SQL_SCHEMA **schemap) {
	SQL_SCHEMA *schema;
	char *copy, *p, *next;
	size_t i, nf;
	int ret;

	if (text == NULL || schemap == NULL)
		return (SQL_ERR_ARG);
	*schemap = NULL;

	copy = sql_strdup(text);
	if (copy == NULL)
		return (SQL_ERR_NOMEM);

	schema = sql_calloc(1, sizeof(SQL_SCHEMA));
	if (schema == NULL) {
		sql_free(copy);
		return (SQL_ERR_NOMEM);
	}
	schema->pk_indx = -1;
	schema->next_rowid = 1;

	ret = SQL_ERR_CATALOG;
	p = copy;
	while (p != NULL && *p != '\0') {
		char *fields[8];

		next = strchr(p, '\n');
		if (next != NULL) {
			*next = '\0';
			next++;
		}
		nf = split_fields(p, fields, 8);
		if (nf == 0)
			goto done;
		switch (p[0]) {
			case 'V':
				if (nf != 1 ||
				    strcmp(fields[0] + 1,
				    SQL_SCHEMA_VERSION) != 0)
					goto err;
				break;
			case 'T':
				if (nf != 1)
					goto err;
				sql_free(schema->name);
				sql_free(schema->lname);
				schema->name = sql_strdup(fields[0] + 1);
				schema->lname = sql_strlower(fields[0] + 1,
				    strlen(fields[0] + 1));
				break;
			case 'C':
			{
				SQL_COLUMN col;
				/* name, type, length, null, key, def, hex */
				if (nf != 7)
					goto err;
				sql_column_init(&col);
				col.name = sql_strdup(fields[0] + 1);
				col.lname = sql_strlower(fields[0] + 1,
				    strlen(fields[0] + 1));
				col.type = (sql_type_t)strtoul(fields[1],
				    NULL, 10);
				col.length = (size_t)strtoul(fields[2],
				    NULL, 10);
				col.not_null = atoi(fields[3]);
				col.primary_key = atoi(fields[4]);
				col.has_default = atoi(fields[5]);
				if (col.has_default) {
					ret = sql_value_from_hex(fields[6],
					    &col.defval);
					if (ret != SQL_OK) {
						sql_column_clear(&col);
						goto err;
					}
				}
				if (schema->ncols == schema->capacity) {
					SQL_COLUMN *newv;
					size_t newcap;

					newcap = (schema->capacity == 0 ?
					    8 : schema->capacity * 2);
					newv = realloc(schema->cols,
					    newcap * sizeof(SQL_COLUMN));
					if (newv == NULL) {
						sql_column_clear(&col);
						ret = SQL_ERR_NOMEM;
						goto err;
					}
					schema->cols = newv;
					schema->capacity = newcap;
				}
				schema->cols[schema->ncols++] = col;
				if (col.primary_key)
					schema->pk_indx =
					    (int)(schema->ncols - 1);
				break;
			}
			case 'R':
				if (nf != 1)
					goto err;
				schema->next_rowid = (uint64)strtoull(
				    fields[0] + 1, NULL, 10);
				break;
			default:
				goto err;
		}
		p = next;
	}

	if (schema->name == NULL || schema->ncols == 0)
		goto err;

	/*
	 * A rowid alias only works for exactly one integer column, any
	 * other primary key is informational only.
	 */
	if (schema->pk_indx >= 0) {
		SQL_COLUMN *col;

		col = &schema->cols[schema->pk_indx];
		if (!SQL_TYPE_IS_INT(col->type))
			schema->pk_indx = -1;
	}

	for (i = 0; i < schema->ncols; i++) {
		if (schema->cols[i].has_default == 0)
			continue;
		if (SQL_TYPE_HAS_LENGTH(schema->cols[i].type) &&
		    schema->cols[i].length == 0)
			goto err;
	}

done:
	sql_free(copy);
	*schemap = schema;
	return (SQL_OK);

err:
	sql_free(copy);
	sql_schema_free(schema);
	return (ret);
}

/********************************************************************
 * Loading & maintaining the catalog
 ********************************************************************/
static int add_table_to_session(SQL_SESSION *sess, SQL_TABLE *tb) {
	size_t cnt;

	if (sess->ntables == sess->cap_tables) {
		SQL_TABLE **newv;
		size_t newcap;

		newcap = (sess->cap_tables == 0 ? 8 :
		    sess->cap_tables * 2);
		newv = realloc(sess->tables, newcap * sizeof(SQL_TABLE *));
		if (newv == NULL)
			return (SQL_ERR_NOMEM);
		sess->tables = newv;
		sess->cap_tables = newcap;
	}
	cnt = sess->ntables;
	sess->tables[cnt] = tb;
	sess->ntables = cnt + 1;

	return (SQL_OK);
}

int sql_session_addtable(SQL_SESSION *sess, SQL_TABLE *tb) {

	return (add_table_to_session(sess, tb));
}

static int remove_table_from_session(SQL_SESSION *sess, const char *lname) {
	size_t i, j;

	for (i = 0; i < sess->ntables; i++) {
		SQL_TABLE *tb;

		tb = sess->tables[i];
		if (tb == NULL || strcmp(tb->lname, lname) != 0)
			continue;
		sql_table_close(tb);
		sql_table_free(tb);
		for (j = i + 1; j < sess->ntables; j++)
			sess->tables[j - 1] = sess->tables[j];
		sess->ntables--;

		return (SQL_OK);
	}

	return (SQL_ERR_NO_TABLE);
}

static int save_tablelist(SQL_SESSION *sess) {
	SQL_STRBUF sb;
	size_t i;
	int ret;

	memset(&sb, 0, sizeof(sb));
	ret = SQL_OK;
	for (i = 0; i < sess->ntables; i++) {
		ret = sb_addf(&sb, "%s\n", sess->tables[i]->name);
		if (ret != SQL_OK)
			goto err;
	}
	ret = cat_put(sess, SQL_TABLELIST_KEY, sb.buf, sb.len);
	if (ret != SQL_OK)
		goto err;
	sql_free(sb.buf);

	return (SQL_OK);

err:
	sql_free(sb.buf);
	return (ret);
}

int sql_catalog_open(SQL_SESSION *sess) {
	SQL_TABLE *tb;
	int ret;

	if (sess == NULL || sess->catalog != NULL)
		return (sess == NULL ? SQL_ERR_ARG : SQL_OK);

	tb = sql_calloc(1, sizeof(SQL_TABLE));
	if (tb == NULL)
		return (SQL_ERR_NOMEM);
	tb->name = sql_strdup(SQL_CATALOG_TABLE);
	tb->lname = sql_strdup(SQL_CATALOG_TABLE);
	tb->kvname = sql_strdup(SQL_CATALOG_TABLE);
	if (tb->name == NULL || tb->lname == NULL || tb->kvname == NULL) {
		sql_free(tb->name);
		sql_free(tb->lname);
		sql_free(tb->kvname);
		sql_free(tb);
		return (SQL_ERR_NOMEM);
	}
	sess->catalog = tb;

	ret = sql_table_open(sess, tb);
	if (ret != SQL_OK)
		return (ret);

	return (sql_catalog_load(sess));
}

int sql_catalog_close(SQL_SESSION *sess) {
	int ret;

	if (sess == NULL)
		return (SQL_ERR_ARG);
	ret = SQL_OK;
	if (sess->catalog != NULL) {
		if (sql_table_close(sess->catalog) != SQL_OK)
			ret = SQL_ERR_STORE;
		sql_table_free(sess->catalog);
		sess->catalog = NULL;
	}

	return (ret);
}

int sql_catalog_load(SQL_SESSION *sess) {
	char *list, *p, *next;
	int ret;

	if (sess == NULL)
		return (SQL_ERR_ARG);

	ret = cat_get(sess, SQL_TABLELIST_KEY, &list);
	if (ret != SQL_OK)
		return (ret);
	if (list == NULL)
		return (SQL_OK);	/* Nothing has been created yet. */

	p = list;
	while (p != NULL && *p != '\0') {
		char *name, *text;
		SQL_SCHEMA *schema;
		SQL_TABLE *tb;

		next = strchr(p, '\n');
		if (next != NULL) {
			*next = '\0';
			next++;
		}
		name = p;
		if (*name != '\0') {
			text = NULL;
			schema = NULL;
			tb = NULL;
			ret = cat_get(sess, name, &text);
			if (ret != SQL_OK)
				goto err;
			if (text == NULL) {
				/* Orphan entry, ignore it. */
				p = next;
				continue;
			}
			ret = sql_schema_deserialize(text, &schema);
			sql_free(text);
			if (ret != SQL_OK)
				goto err;
			ret = sql_table_alloc(sess, schema->name, schema,
			    &tb);
			if (ret != SQL_OK) {
				sql_schema_free(schema);
				goto err;
			}
			ret = add_table_to_session(sess, tb);
			if (ret != SQL_OK) {
				sql_table_free(tb);
				goto err;
			}
		}
		p = next;
	}

	sql_free(list);
	return (SQL_OK);

err:
	sql_free(list);
	return (ret);
}

int sql_catalog_save_schema(SQL_SESSION *sess, SQL_SCHEMA *schema) {
	char *text;
	int ret;

	text = NULL;
	ret = sql_schema_serialize(schema, &text);
	if (ret != SQL_OK)
		return (ret);
	ret = cat_put(sess, schema->lname, text, strlen(text));
	sql_free(text);
	if (ret != SQL_OK)
		return (ret);

	return (save_tablelist(sess));
}

int sql_session_deltable(SQL_SESSION *sess, const char *lname) {

	return (remove_table_from_session(sess, lname));
}

int sql_catalog_drop(SQL_SESSION *sess, const char *lname) {
	int ret;

	if (sess == NULL || lname == NULL)
		return (SQL_ERR_ARG);

	ret = cat_del(sess, lname);
	if (ret != SQL_OK)
		return (ret);
	ret = remove_table_from_session(sess, lname);
	if (ret != SQL_OK)
		return (ret);

	return (save_tablelist(sess));
}
