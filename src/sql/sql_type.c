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
 * Type names & value conversions.
 *
 * Temporal values are counted from the Unix epoch and are always
 * interpreted in UTC, which keeps stored data independent of the TZ of
 * the machine that wrote it.
 */

#define	SECS_PER_DAY	86400
#define	USECS_PER_SEC	1000000
#define	MSECS_PER_SEC	1000

#define	SQL_MAX_CHARLEN	65535

static int sql_isdigit(int c) {

	return (c >= '0' && c <= '9');
}

/*
 * Map a type keyword onto a datatype.
 *
 * 'has_arg' tells whether the type was spelled with a parenthesized
 * argument, 'arg' is that argument.  '*lengthp' receives the declared
 * length for the types that care about it.
 */
int sql_type_parse(sql_kw kw, int has_arg, size_t arg, sql_type_t *typep,
    size_t *lengthp) {
	sql_type_t type;

	if (typep == NULL)
		return (SQL_ERR_ARG);
	if (lengthp != NULL)
		*lengthp = 0;
	type = SQL_T_NULL;

	switch (kw) {
		case KW_INT:
			type = SQL_T_INT;
			break;
		case KW_BIGINT:
			type = SQL_T_BIGINT;
			break;
		case KW_SMALLINT:
			type = SQL_T_SMALLINT;
			break;
		case KW_FLOAT:
			type = SQL_T_FLOAT;
			break;
		case KW_DOUBLE:
			type = SQL_T_DOUBLE;
			break;
		case KW_VARCHAR:
		case KW_CHAR:
		case KW_BINARY:
		case KW_VARBINARY:
			/*
			 * These four require a length, the count is in
			 * characters (== bytes here, the layer has no
			 * character set support).
			 */
			if (!has_arg)
				return (SQL_ERR_ARG);
			if (arg == 0 || arg > SQL_MAX_CHARLEN)
				return (SQL_ERR_TYPE);
			if (kw == KW_VARCHAR)
				type = SQL_T_VARCHAR;
			else if (kw == KW_CHAR)
				type = SQL_T_CHAR;
			else if (kw == KW_BINARY)
				type = SQL_T_BINARY;
			else
				type = SQL_T_VARBINARY;
			if (lengthp != NULL)
				*lengthp = arg;
			break;
		case KW_TEXT:
			type = SQL_T_TEXT;
			break;
		case KW_BLOB:
			type = SQL_T_BLOB;
			break;
		case KW_DATE:
			type = SQL_T_DATE;
			break;
		case KW_TIME:
			type = SQL_T_TIME;
			break;
		case KW_TIMESTAMP:
			type = SQL_T_TIMESTAMP;
			break;
		default:
			return (SQL_ERR_TYPE);
	}

	*typep = type;
	return (SQL_OK);
}

int sql_value_numeric(const SQL_VALUE *val, double *dp) {

	if (val == NULL || dp == NULL)
		return (SQL_ERR_ARG);
	if (val->isnull || !SQL_TYPE_IS_NUMERIC(val->type))
		return (SQL_ERR_TYPE);
	if (SQL_TYPE_IS_FLOAT(val->type))
		*dp = val->dval;
	else
		*dp = (double)val->ival;

	return (SQL_OK);
}

/********************************************************************
 * Number scanning
 ********************************************************************/
static int scan_int(const char *str, size_t len, int64 *outp) {
	char *end;
	const char *start;
	size_t n;
	int64 v;

	if (str == NULL)
		return (SQL_ERR_CONVERT);
	start = str;
	n = len;
	while (n > 0 && isspace((unsigned char)*start)) {
		start++;
		n--;
	}
	while (n > 0 && isspace((unsigned char)start[n - 1]))
		n--;
	if (n == 0)
		return (SQL_ERR_CONVERT);
	if (n >= 64)	/* Ridiculously long, cannot be valid anyway */
		return (SQL_ERR_CONVERT);

	errno = 0;
	v = (int64)strtoll(start, &end, 10);
	if (errno == ERANGE)
		return (SQL_ERR_CONVERT);
	if (end != start + n)
		return (SQL_ERR_CONVERT);

	*outp = v;
	return (SQL_OK);
}

static int scan_double(const char *str, size_t len, double *outp) {
	char buf[512];
	char *end;
	double v;
	size_t i;

	if (len >= sizeof(buf))
		return (SQL_ERR_CONVERT);
	memcpy(buf, str, len);
	buf[len] = '\0';
	for (i = 0; i < len; i++) {
		if (buf[i] == ' ' || buf[i] == '\t')
			buf[i] = ' ';
	}
	errno = 0;
	v = strtod(buf, &end);
	if (errno == ERANGE)
		return (SQL_ERR_CONVERT);
	while (*end == ' ')
		end++;
	if (*end != '\0')
		return (SQL_ERR_CONVERT);

	*outp = v;
	return (SQL_OK);
}

/********************************************************************
 * Date / time arithmetic
 ********************************************************************/
static int64 days_from_civil(int64 y, int64 m, int64 d) {
	int64 era, yoe, doy, doe;

	y -= (m <= 2 ? 1 : 0);
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

	return (era * 146097 + doe - 719468);
}

static void civil_from_days(int64 z, int64 *yp, int64 *mp, int64 *dp) {
	uint64 zu;
	int64 era, doe, yoe, doy, mpv, dv, yv;

	zu = (uint64)z + 719468ULL;
	era = (int64)(zu / 146097ULL);
	doe = (int64)(zu - (uint64)era * 146097ULL);
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	yv = yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mpv = (5 * doy + 2) / 153;
	dv = doy - (153 * mpv + 2) / 5 + 1;

	*yp = yv + (mpv >= 10 ? 1 : 0);
	*mp = mpv < 10 ? mpv + 3 : mpv - 9;
	*dp = dv;
}

static int is_leap(int64 y) {

	return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
}

static int days_in_month(int64 y, int64 m) {
	static const int dpm[12] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};

	if (m < 1 || m > 12)
		return (0);
	if (m == 2 && is_leap(y))
		return (29);

	return (dpm[m - 1]);
}

/* Read a fixed width decimal number, return SQL_OK or a conversion error. */
static int scan_decimal(const char *str, size_t len, size_t *ip, size_t width,
    int64 *outp) {
	size_t i;
	int64 v;

	if (*ip + width > len)
		return (SQL_ERR_CONVERT);
	v = 0;
	for (i = 0; i < width; i++) {
		char c;

		c = str[*ip + i];
		if (!sql_isdigit((unsigned char)c))
			return (SQL_ERR_CONVERT);
		v = v * 10 + (c - '0');
	}
	*ip += width;
	*outp = v;

	return (SQL_OK);
}

static int scan_fraction(const char *str, size_t len, size_t *ip,
    int64 *fracp, int64 *scale) {
	size_t start;
	int64 v, sc;

	if (*ip >= len || str[*ip] != '.')
		return (SQL_ERR_CONVERT);
	(*ip)++;
	start = *ip;
	v = 0;
	sc = 1;
	while (*ip < len && sql_isdigit((unsigned char)str[*ip])) {
		if (sc < USECS_PER_SEC) {
			v = v * 10 + (str[*ip] - '0');
			sc *= 10;
		}
		(*ip)++;
	}
	if (*ip == start)
		return (SQL_ERR_CONVERT);
	*fracp = v;
	*scale = sc;

	return (SQL_OK);
}

/* Skip white space. */
static void skip_ws(const char *str, size_t len, size_t *ip) {

	while (*ip < len && isspace((unsigned char)str[*ip]))
		(*ip)++;
}

static int scan_daytime(const char *str, size_t len, size_t *ip) {
	int64 hh, mi, ss;

	if (scan_decimal(str, len, ip, 2, &hh) != SQL_OK)
		return (SQL_ERR_CONVERT);
	if (*ip >= len || str[*ip] != ':')
		return (SQL_ERR_CONVERT);
	(*ip)++;
	if (scan_decimal(str, len, ip, 2, &mi) != SQL_OK)
		return (SQL_ERR_CONVERT);
	ss = 0;
	if (*ip < len && str[*ip] == ':') {
		(*ip)++;
		if (scan_decimal(str, len, ip, 2, &ss) != SQL_OK)
			return (SQL_ERR_CONVERT);
	}
	if (hh > 23 || mi > 59 || ss > 59)
		return (SQL_ERR_CONVERT);

	return (SQL_OK);
}

int sql_string_to_date(const char *str, size_t len, int64 *daysp) {
	size_t i;
	int64 y, m, d, days;

	i = 0;
	if (scan_decimal(str, len, &i, 4, &y) != SQL_OK)
		return (SQL_ERR_CONVERT);
	if (i >= len || str[i] != '-')
		return (SQL_ERR_CONVERT);
	i++;
	if (scan_decimal(str, len, &i, 2, &m) != SQL_OK)
		return (SQL_ERR_CONVERT);
	if (i >= len || str[i] != '-')
		return (SQL_ERR_CONVERT);
	i++;
	if (scan_decimal(str, len, &i, 2, &d) != SQL_OK)
		return (SQL_ERR_CONVERT);

	/* An optional time of day is accepted and ignored. */
	skip_ws(str, len, &i);
	if (i < len && (str[i] == 'T' || str[i] == 't'))
		i++;
	skip_ws(str, len, &i);
	if (i < len && sql_isdigit((unsigned char)str[i]))
		if (scan_daytime(str, len, &i) != SQL_OK)
			return (SQL_ERR_CONVERT);
	skip_ws(str, len, &i);
	if (i != len)
		return (SQL_ERR_CONVERT);

	if (y < 1 || m < 1 || m > 12 || d < 1 || d > days_in_month(y, m))
		return (SQL_ERR_CONVERT);
	days = days_from_civil(y, m, d);
	*daysp = days;

	return (SQL_OK);
}

int sql_string_to_time(const char *str, size_t len, int64 *usecp) {
	size_t i;
	int64 hh, mi, ss, frac, scale, total;

	i = 0;
	if (scan_decimal(str, len, &i, 2, &hh) != SQL_OK)
		return (SQL_ERR_CONVERT);
	if (i >= len || str[i] != ':')
		return (SQL_ERR_CONVERT);
	i++;
	if (scan_decimal(str, len, &i, 2, &mi) != SQL_OK)
		return (SQL_ERR_CONVERT);
	ss = 0;
	if (i < len && str[i] == ':') {
		i++;
		if (scan_decimal(str, len, &i, 2, &ss) != SQL_OK)
			return (SQL_ERR_CONVERT);
	}
	frac = 0;
	scale = 1;
	if (i < len && str[i] == '.') {
		if (scan_fraction(str, len, &i, &frac, &scale) != SQL_OK)
			return (SQL_ERR_CONVERT);
	}
	while (i < len && isspace((unsigned char)str[i]))
		i++;
	if (i != len)
		return (SQL_ERR_CONVERT);
	if (hh > 24 || mi > 59 || ss > 59 || (hh == 24 && (mi != 0 || ss != 0)))
		return (SQL_ERR_CONVERT);

	total = ((hh * 3600 + mi * 60 + ss) ) * USECS_PER_SEC;
	total += frac * (USECS_PER_SEC / scale);
	*usecp = total;

	return (SQL_OK);
}

int sql_string_to_timestamp(const char *str, size_t len, int64 *msecp) {
	size_t i;
	int64 y, m, d, hh, mi, ss, frac, scale, days, total;

	i = 0;
	if (scan_decimal(str, len, &i, 4, &y) != SQL_OK)
		return (SQL_ERR_CONVERT);
	if (i >= len || str[i] != '-')
		return (SQL_ERR_CONVERT);
	i++;
	if (scan_decimal(str, len, &i, 2, &m) != SQL_OK)
		return (SQL_ERR_CONVERT);
	if (i >= len || str[i] != '-')
		return (SQL_ERR_CONVERT);
	i++;
	if (scan_decimal(str, len, &i, 2, &d) != SQL_OK)
		return (SQL_ERR_CONVERT);

	hh = 0;
	mi = 0;
	ss = 0;
	frac = 0;
	scale = 1;
	skip_ws(str, len, &i);
	if (i < len && (str[i] == 'T' || str[i] == 't'))
		i++;
	skip_ws(str, len, &i);
	if (i < len && sql_isdigit((unsigned char)str[i])) {
		if (scan_decimal(str, len, &i, 2, &hh) != SQL_OK)
			return (SQL_ERR_CONVERT);
		if (i >= len || str[i] != ':')
			return (SQL_ERR_CONVERT);
		i++;
		if (scan_decimal(str, len, &i, 2, &mi) != SQL_OK)
			return (SQL_ERR_CONVERT);
		if (i < len && str[i] == ':') {
			i++;
			if (scan_decimal(str, len, &i, 2, &ss) != SQL_OK)
				return (SQL_ERR_CONVERT);
		}
		if (i < len && str[i] == '.') {
			if (scan_fraction(str, len, &i, &frac,
			    &scale) != SQL_OK)
				return (SQL_ERR_CONVERT);
		}
	}
	while (i < len && isspace((unsigned char)str[i]))
		i++;
	if (i != len)
		return (SQL_ERR_CONVERT);
	if (y < 1 || m < 1 || m > 12 || d < 1 || d > days_in_month(y, m))
		return (SQL_ERR_CONVERT);
	if (hh > 23 || mi > 59 || ss > 59)
		return (SQL_ERR_CONVERT);

	days = days_from_civil(y, m, d);
	total = days * SECS_PER_DAY + hh * 3600 + mi * 60 + ss;
	total = total * MSECS_PER_SEC;
	total += (frac * MSECS_PER_SEC) / scale;
	*msecp = total;

	return (SQL_OK);
}

static char *format_alloc(size_t size) {

	return ((char *)sql_calloc(size, 1));
}

int sql_date_to_string(int64 days, char **sp) {
	char *res;
	int64 y, m, d;

	civil_from_days(days, &y, &m, &d);
	res = format_alloc(32);
	if (res == NULL)
		return (SQL_ERR_NOMEM);
	(void)snprintf(res, 32, "%04lld-%02lld-%02lld",
	    (long long)y, (long long)m, (long long)d);
	*sp = res;

	return (SQL_OK);
}

int sql_time_to_string(int64 usec, char **sp) {
	char *res;
	int64 hh, mi, ss, frac;

	hh = usec / (3600LL * USECS_PER_SEC);
	usec -= hh * 3600LL * USECS_PER_SEC;
	mi = usec / (60LL * USECS_PER_SEC);
	usec -= mi * 60LL * USECS_PER_SEC;
	ss = usec / USECS_PER_SEC;
	frac = usec - ss * USECS_PER_SEC;

	res = format_alloc(32);
	if (res == NULL)
		return (SQL_ERR_NOMEM);
	if (frac != 0)
		(void)snprintf(res, 32, "%02lld:%02lld:%02lld.%06lld",
		    (long long)hh, (long long)mi, (long long)ss,
		    (long long)frac);
	else
		(void)snprintf(res, 32, "%02lld:%02lld:%02lld",
		    (long long)hh, (long long)mi, (long long)ss);
	*sp = res;

	return (SQL_OK);
}

int sql_timestamp_to_string(int64 msec, char **sp) {
	char *res, *dpart, *tpart;
	int64 days, rem, ret2;
	int ret;

	dpart = NULL;
	tpart = NULL;
	if (msec < 0) {
		days = (msec / (SECS_PER_DAY * MSECS_PER_SEC)) - 1;
		rem = msec - days * SECS_PER_DAY * MSECS_PER_SEC;
	} else {
		days = msec / (SECS_PER_DAY * MSECS_PER_SEC);
		rem = msec - days * SECS_PER_DAY * MSECS_PER_SEC;
	}
	ret = sql_date_to_string(days, &dpart);
	if (ret != SQL_OK) {
		sql_free(dpart);
		return (ret);
	}
	ret2 = rem / MSECS_PER_SEC;
	rem -= ret2 * MSECS_PER_SEC;
	ret = sql_time_to_string(ret2 * USECS_PER_SEC, &tpart);
	if (ret != SQL_OK) {
		sql_free(dpart);
		sql_free(tpart);
		return (ret);
	}
	res = format_alloc(strlen(dpart) + strlen(tpart) + 8);
	if (res == NULL) {
		sql_free(dpart);
		sql_free(tpart);
		return (SQL_ERR_NOMEM);
	}
	if (rem != 0)
		(void)snprintf(res, strlen(dpart) + strlen(tpart) + 8,
		    "%s %s.%03lld", dpart, tpart, (long long)rem);
	else
		(void)snprintf(res, strlen(dpart) + strlen(tpart) + 8,
		    "%s %s", dpart, tpart);
	sql_free(dpart);
	sql_free(tpart);
	*sp = res;

	return (SQL_OK);
}

int sql_now_date(int64 *daysp) {
	time_t now;
	int64 secs;

	now = time(NULL);
	secs = (int64)now;
	*daysp = (secs >= 0 ? secs : secs - (SECS_PER_DAY - 1)) /
	    SECS_PER_DAY;

	return (SQL_OK);
}

int sql_now_time(int64 *usecp) {
	time_t now;
	int64 secs;

	now = time(NULL);
	secs = (int64)now % SECS_PER_DAY;
	if (secs < 0)
		secs += SECS_PER_DAY;
	*usecp = secs * USECS_PER_SEC;

	return (SQL_OK);
}

int sql_now_timestamp(int64 *msecp) {
	time_t now;

	now = time(NULL);
	*msecp = (int64)now * MSECS_PER_SEC;

	return (SQL_OK);
}

/********************************************************************
 * Casting
 ********************************************************************/
static int cast_to_int(const SQL_VALUE *src, sql_type_t type, size_t length,
    SQL_VALUE *dst) {
	int64 v;
	double d;
	char *s;
	size_t slen;
	int ret;

	(void)length;
	if (SQL_TYPE_IS_INT(src->type)) {
		v = src->ival;
	} else if (SQL_TYPE_IS_FLOAT(src->type)) {
		if (isnan(src->dval) || isinf(src->dval))
			return (SQL_ERR_CONVERT);
		if (src->dval > 9.2e18 || src->dval < -9.2e18)
			return (SQL_ERR_CONVERT);
		v = (int64)src->dval;
	} else if (SQL_TYPE_IS_STRING(src->type) ||
	    SQL_TYPE_IS_BINARY(src->type)) {
		ret = sql_value_tostring(src, &s);
		if (ret != SQL_OK)
			return (ret);
		if (s == NULL)
			return (SQL_ERR_CONVERT);
		slen = strlen(s);
		ret = scan_int(s, slen, &v);
		sql_free(s);
		if (ret != SQL_OK)
			return (ret);
	} else
		return (SQL_ERR_TYPE);

	if (type == SQL_T_SMALLINT && (v < -32768 || v > 32767))
		return (SQL_ERR_CONVERT);
	if (type == SQL_T_INT && (v < -2147483648LL || v > 2147483647LL))
		return (SQL_ERR_CONVERT);
	(void)d;

	return (sql_value_set_int(dst, type, v));
}

static int cast_to_float(const SQL_VALUE *src, sql_type_t type, size_t length,
    SQL_VALUE *dst) {
	double v;
	char *s;
	size_t slen;
	int ret;

	(void)length;
	if (SQL_TYPE_IS_FLOAT(src->type)) {
		v = src->dval;
	} else if (SQL_TYPE_IS_INT(src->type)) {
		v = (double)src->ival;
	} else if (SQL_TYPE_IS_STRING(src->type) ||
	    SQL_TYPE_IS_BINARY(src->type)) {
		ret = sql_value_tostring(src, &s);
		if (ret != SQL_OK)
			return (ret);
		if (s == NULL)
			return (SQL_ERR_CONVERT);
		slen = strlen(s);
		ret = scan_double(s, slen, &v);
		sql_free(s);
		if (ret != SQL_OK)
			return (ret);
	} else
		return (SQL_ERR_TYPE);

	if (type == SQL_T_FLOAT && !isnan(v) &&
	    (v > 3.4e38 || v < -3.4e38))
		return (SQL_ERR_CONVERT);

	return (sql_value_set_double(dst, type, v));
}

static int cast_to_string(const SQL_VALUE *src, sql_type_t type, size_t length,
    SQL_VALUE *dst) {
	char *s;
	size_t slen;
	int ret;

	if (src->isnull)
		return (sql_value_set_null(dst, type));

	if (SQL_TYPE_IS_STRING(src->type) || SQL_TYPE_IS_BINARY(src->type))
		return (sql_value_set_string(dst, type, src->sval, src->slen));

	ret = sql_value_tostring(src, &s);
	if (ret != SQL_OK)
		return (ret);
	if (s == NULL)
		return (SQL_ERR_CONVERT);
	slen = strlen(s);
	if (length != 0 && slen > length) {
		sql_free(s);
		return (SQL_ERR_TYPE);
	}
	ret = sql_value_set_string(dst, type, s, slen);
	sql_free(s);

	return (ret);
}

static int cast_to_binary(const SQL_VALUE *src, sql_type_t type,
    size_t length, SQL_VALUE *dst) {

	if (src->isnull)
		return (sql_value_set_null(dst, type));
	if (!SQL_TYPE_IS_BINARY(src->type) && !SQL_TYPE_IS_STRING(src->type))
		return (SQL_ERR_TYPE);
	if (length != 0 && src->slen > length)
		return (SQL_ERR_TYPE);

	return (sql_value_set_string(dst, type, src->sval, src->slen));
}

static int cast_to_date(const SQL_VALUE *src, sql_type_t type, size_t length,
    SQL_VALUE *dst) {
	int64 days;
	char *s;
	size_t slen;
	int ret;

	(void)length;
	if (SQL_TYPE_IS_BINARY(src->type))
		return (SQL_ERR_TYPE);
	if (src->type == SQL_T_DATE)
		return (sql_value_set_int(dst, type, src->ival));
	if (src->type == SQL_T_TIMESTAMP) {
		int64 d = src->ival / MSECS_PER_SEC;
		days = (d >= 0 ? d : d - (SECS_PER_DAY - 1)) / SECS_PER_DAY;
		return (sql_value_set_int(dst, type, days));
	}
	if (src->type != SQL_T_NULL && SQL_TYPE_IS_NUMERIC(src->type))
		return (SQL_ERR_TYPE);

	ret = sql_value_tostring(src, &s);
	if (ret != SQL_OK)
		return (ret);
	if (s == NULL)
		return (SQL_ERR_CONVERT);
	slen = strlen(s);
	ret = sql_string_to_date(s, slen, &days);
	sql_free(s);
	if (ret != SQL_OK)
		return (ret);

	return (sql_value_set_int(dst, type, days));
}

static int cast_to_time(const SQL_VALUE *src, sql_type_t type, size_t length,
    SQL_VALUE *dst) {
	int64 usec;
	char *s;
	size_t slen;
	int ret;

	(void)length;
	if (SQL_TYPE_IS_BINARY(src->type))
		return (SQL_ERR_TYPE);
	if (src->type == SQL_T_TIME)
		return (sql_value_set_int(dst, type, src->ival));
	if (src->type == SQL_T_TIMESTAMP) {
		int64 t = src->ival;
		t %= (int64)SECS_PER_DAY * MSECS_PER_SEC;
		if (t < 0)
			t += (int64)SECS_PER_DAY * MSECS_PER_SEC;
		usec = (t * USECS_PER_SEC) / MSECS_PER_SEC;
		return (sql_value_set_int(dst, type, usec));
	}
	if (SQL_TYPE_IS_NUMERIC(src->type))
		return (SQL_ERR_TYPE);

	ret = sql_value_tostring(src, &s);
	if (ret != SQL_OK)
		return (ret);
	if (s == NULL)
		return (SQL_ERR_CONVERT);
	slen = strlen(s);
	ret = sql_string_to_time(s, slen, &usec);
	sql_free(s);
	if (ret != SQL_OK)
		return (ret);

	return (sql_value_set_int(dst, type, usec));
}

static int cast_to_timestamp(const SQL_VALUE *src, sql_type_t type,
    size_t length, SQL_VALUE *dst) {
	int64 msec;
	char *s;
	size_t slen;
	int ret;

	(void)length;
	if (SQL_TYPE_IS_BINARY(src->type))
		return (SQL_ERR_TYPE);
	if (src->type == SQL_T_TIMESTAMP)
		return (sql_value_set_int(dst, type, src->ival));
	if (src->type == SQL_T_DATE)
		return (sql_value_set_int(dst, type,
		    src->ival * SECS_PER_DAY * MSECS_PER_SEC));
	if (SQL_TYPE_IS_NUMERIC(src->type))
		return (SQL_ERR_TYPE);

	ret = sql_value_tostring(src, &s);
	if (ret != SQL_OK)
		return (ret);
	if (s == NULL)
		return (SQL_ERR_CONVERT);
	slen = strlen(s);
	ret = sql_string_to_timestamp(s, slen, &msec);
	sql_free(s);
	if (ret != SQL_OK)
		return (ret);

	return (sql_value_set_int(dst, type, msec));
}

/*
 * Convert 'src' to 'type'.  'dst' may alias 'src'.  'length' is the
 * declared column length, zero means unrestricted.
 */
int sql_value_cast(const SQL_VALUE *src, sql_type_t type, size_t length,
    SQL_VALUE *dst) {
	SQL_VALUE tmp;
	int ret;

	if (src == NULL || dst == NULL)
		return (SQL_ERR_ARG);

	sql_value_init(&tmp);
	ret = sql_value_copy(src, &tmp);
	if (ret != SQL_OK)
		return (ret);

	if (tmp.isnull) {
		sql_value_clear(&tmp);
		return (sql_value_set_null(dst, type));
	}

	switch (type) {
		case SQL_T_SMALLINT:
		case SQL_T_INT:
		case SQL_T_BIGINT:
			ret = cast_to_int(&tmp, type, length, dst);
			break;
		case SQL_T_FLOAT:
		case SQL_T_DOUBLE:
			ret = cast_to_float(&tmp, type, length, dst);
			break;
		case SQL_T_CHAR:
		case SQL_T_VARCHAR:
		case SQL_T_TEXT:
			ret = cast_to_string(&tmp, type, length, dst);
			break;
		case SQL_T_BINARY:
		case SQL_T_VARBINARY:
		case SQL_T_BLOB:
			ret = cast_to_binary(&tmp, type, length, dst);
			break;
		case SQL_T_DATE:
			ret = cast_to_date(&tmp, type, length, dst);
			break;
		case SQL_T_TIME:
			ret = cast_to_time(&tmp, type, length, dst);
			break;
		case SQL_T_TIMESTAMP:
			ret = cast_to_timestamp(&tmp, type, length, dst);
			break;
		default:
			ret = SQL_ERR_TYPE;
			break;
	}

	sql_value_clear(&tmp);
	return (ret);
}
