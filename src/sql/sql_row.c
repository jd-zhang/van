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
 * Row layout inside the value stored for a row id:
 *
 *	[2 bytes: number of columns, big endian]
 *	per column:
 *		[1 byte:  0 => SQL NULL, 1 => present]
 *		[4 bytes: payload length, omitted when NULL]
 *		[n bytes: payload]
 *
 * Fixed width types use exactly their wire size as the payload:
 *
 *	SMALLINT 2, INT 4, BIGINT 8, DATE 4, TIME 8, TIMESTAMP 8
 *	FLOAT 4 and DOUBLE 8 IEEE 754 in network byte order
 *
 * Everything else (character and binary strings) is stored verbatim.
 */

void sql_rowkey_encode(uint64 rowid, unsigned char *buf) {
	int i;

	for (i = 0; i < 8; i++)
		buf[i] = (unsigned char)((rowid >> (56 - 8 * i)) & 0xFF);
}

void sql_rowkey_decode(const unsigned char *buf, uint64 *rowidp) {
	uint64 v;
	int i;

	v = 0;
	for (i = 0; i < 8; i++)
		v = (v << 8) | (uint64)buf[i];
	*rowidp = v;
}

static void put_be16(unsigned char *p, unsigned int v) {

	p[0] = (unsigned char)((v >> 8) & 0xFF);
	p[1] = (unsigned char)(v & 0xFF);
}

static unsigned int get_be16(const unsigned char *p) {

	return (((unsigned int)p[0]) << 8) | (unsigned int)p[1];
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

static void put_be64(unsigned char *p, uint64 v) {
	int i;

	for (i = 0; i < 8; i++)
		p[i] = (unsigned char)((v >> (56 - 8 * i)) & 0xFF);
}

/* Wire size of the payload of a value of this type. */
static size_t fixed_size(sql_type_t type) {

	switch (type) {
		case SQL_T_SMALLINT:
			return (2);
		case SQL_T_INT:
		case SQL_T_DATE:
		case SQL_T_FLOAT:
			return (4);
		case SQL_T_BIGINT:
		case SQL_T_TIME:
		case SQL_T_TIMESTAMP:
		case SQL_T_DOUBLE:
			return (8);
		default:
			break;
	}

	return (0);
}

void sql_row_values_clear(SQL_VALUE *vals, size_t ncols) {
	size_t i;

	if (vals == NULL)
		return;
	for (i = 0; i < ncols; i++)
		sql_value_clear(&vals[i]);
}

int sql_row_encode(SQL_SCHEMA *schema, const SQL_VALUE *vals,
    unsigned char **bufp, size_t *lenp) {
	unsigned char *buf, *p;
	size_t i, total;
	size_t plen;

	if (schema == NULL || vals == NULL || bufp == NULL || lenp == NULL)
		return (SQL_ERR_ARG);
	*bufp = NULL;
	*lenp = 0;

	total = 2;
	for (i = 0; i < schema->ncols; i++) {
		total += 1;
		if (vals[i].isnull)
			continue;
		plen = fixed_size(schema->cols[i].type);
		if (plen == 0)
			plen = vals[i].slen;
		total += 4 + plen;
	}

	buf = sql_calloc(total, 1);
	if (buf == NULL)
		return (SQL_ERR_NOMEM);

	p = buf;
	put_be16(p, (unsigned int)schema->ncols);
	p += 2;
	for (i = 0; i < schema->ncols; i++) {
		const SQL_VALUE *v;
		sql_type_t type;

		v = &vals[i];
		type = schema->cols[i].type;
		if (v->isnull) {
			*p++ = 0;
			continue;
		}
		*p++ = 1;
		if (SQL_TYPE_IS_STRING(type) || SQL_TYPE_IS_BINARY(type)) {
			put_be32(p, (uint32)v->slen);
			p += 4;
			if (v->slen > 0) {
				memcpy(p, v->sval, v->slen);
				p += v->slen;
			}
			continue;
		}
		put_be32(p, (uint32)fixed_size(type));
		p += 4;
		switch (type) {
			case SQL_T_SMALLINT:
			case SQL_T_INT:
			case SQL_T_BIGINT:
			case SQL_T_DATE:
			case SQL_T_TIME:
			case SQL_T_TIMESTAMP:
			{
				size_t sz = fixed_size(type);
				uint64 mag = (uint64)v->ival;
				int k;
				for (k = 0; k < (int)sz; k++)
					p[k] = (unsigned char)(
					    (mag >> (8 * (sz - 1 - k))) &
					    0xFF);
				p += sz;
				break;
			}
			case SQL_T_FLOAT:
			{
				float f;
				uint32 bits;
				f = (float)v->dval;
				memcpy(&bits, &f, sizeof(bits));
				put_be32(p, bits);
				p += 4;
				break;
			}
			case SQL_T_DOUBLE:
			{
				uint64 bits;
				memcpy(&bits, &v->dval, sizeof(bits));
				put_be64(p, bits);
				p += 8;
				break;
			}
			default:
				sql_free(buf);
				return (SQL_ERR_TYPE);
		}
	}

	*bufp = buf;
	*lenp = (size_t)(p - buf);

	return (SQL_OK);
}

/*
 * Decode a stored row into 'vals', which must already hold initialized
 * values.  Existing contents are released first.
 */
int sql_row_decode(SQL_SCHEMA *schema, const void *data, size_t len,
    SQL_VALUE *vals) {
	const unsigned char *p;
	size_t i, ncols;
	int ret;

	if (schema == NULL || data == NULL || vals == NULL)
		return (SQL_ERR_ARG);
	if (len < 2)
		return (SQL_ERR_STORE);

	p = (const unsigned char *)data;
	ncols = get_be16(p);
	if (ncols != schema->ncols)
		return (SQL_ERR_STORE);
	p += 2;

	for (i = 0; i < schema->ncols; i++)
		sql_value_clear(&vals[i]);

	for (i = 0; i < ncols; i++) {
		sql_type_t type;
		unsigned char present;

		type = schema->cols[i].type;
		if ((size_t)(p - (const unsigned char *)data) + 1 > len)
			return (SQL_ERR_STORE);
		present = *p++;
		if (present == 0) {
			ret = sql_value_set_null(&vals[i], type);
			if (ret != SQL_OK)
				return (ret);
			continue;
		}
		if (present != 1)
			return (SQL_ERR_STORE);
		if ((size_t)(p - (const unsigned char *)data) + 4 > len)
			return (SQL_ERR_STORE);

		if (SQL_TYPE_IS_STRING(type) || SQL_TYPE_IS_BINARY(type)) {
			uint32 plen;

			plen = get_be32(p);
			p += 4;
			if ((size_t)(p - (const unsigned char *)data) +
			    plen > len)
				return (SQL_ERR_STORE);
			ret = sql_value_set_string(&vals[i], type, p, plen);
			if (ret != SQL_OK)
				return (ret);
			p += plen;
			continue;
		}

		{
			size_t sz;
			uint64 mag;
			int k;

			sz = fixed_size(type);
			if (sz == 0)
				return (SQL_ERR_STORE);
			if ((size_t)(p - (const unsigned char *)data) +
			    sz > len)
				return (SQL_ERR_STORE);
			/* Skip the explicit length field. */
			p += 4;
			if ((size_t)(p - (const unsigned char *)data) +
			    sz > len)
				return (SQL_ERR_STORE);
			mag = 0;
			for (k = 0; k < (int)sz; k++)
				mag = (mag << 8) | (uint64)p[k];
			p += sz;
			if (type == SQL_T_FLOAT) {
				uint32 bits;
				float f;
				bits = (uint32)mag;
				memcpy(&f, &bits, sizeof(f));
				ret = sql_value_set_double(&vals[i], type,
				    (double)f);
			} else if (type == SQL_T_DOUBLE) {
				double d;
				memcpy(&d, &mag, sizeof(d));
				ret = sql_value_set_double(&vals[i], type, d);
			} else {
				/*
				 * Signed values were stored in two's
				 * complement, widen them back.
				 */
				int64 v;
				if (sz < 8 && (mag >> (sz * 8 - 1)) & 1) {
					uint64 mask = ~((uint64)0) <<
					    (sz * 8);
					mag |= mask;
				}
				v = (int64)mag;
				ret = sql_value_set_int(&vals[i], type, v);
			}
			if (ret != SQL_OK)
				return (ret);
		}
	}

	return (SQL_OK);
}
