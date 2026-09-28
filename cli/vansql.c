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
#include "van_sql.h"
#include <unistd.h>
#include "linenoise.h"

/*
 * vansql -- a tiny front end for the VanDB SQL layer.
 *
 *	usage: vansql [-readonly] [-column|-list] [dir [sql ...]]
 *	       vansql -c sql [dir]
 *	       vansql -f script [dir]
 *
 * Without -c/-f it reads statements interactively.
 */

#define	MODE_COLUMN	0
#define	MODE_LIST	1

static const char *progname = "vansql";
static int out_mode = MODE_COLUMN;
static int show_stats = 0;

static void usage(void) {

	(void)fprintf(stderr,
	    "usage: %s [-readonly] [-column|-list] [dir [sql ...]]\n"
	    "       %s -c sql [-readonly] [dir]\n"
	    "       %s -f script [-readonly] [dir]\n",
	    progname, progname, progname);
}

/* Return true when 'str' starts with 'word', ignoring case. */
static int starts_with(const char *str, const char *word) {
	size_t i;

	for (i = 0; word[i] != '\0'; i++)
		if (tolower((unsigned char)str[i]) !=
		    tolower((unsigned char)word[i]))
			return (0);

	return (1);
}

static int is_select(const char *stmt) {

	return (starts_with(stmt, "select"));
}

/*
 * True for the DML statements that report an affected-row count.  This is a
 * cheap CLI-level prefix test (case insensitive); the engine keeps the real
 * count in sess->rows_affected regardless of how the statement arrived.
 */
static int is_dml(const char *stmt) {

	return (starts_with(stmt, "insert") ||
	    starts_with(stmt, "update") ||
	    starts_with(stmt, "delete"));
}

static void print_list(SQL_RESULTSET *rs, FILE *out) {
	size_t i, j;

	for (i = 0; i < rs->nrows; i++) {
		for (j = 0; j < rs->ncols; j++) {
			char *s;

			s = NULL;
			if (j != 0)
				(void)fprintf(out, "|");
			if (rs->rows[i][j].isnull)
				(void)fprintf(out, "");
			else if (sql_value_tostring(&rs->rows[i][j], &s) ==
			    SQL_OK && s != NULL)
				(void)fprintf(out, "%s", s);
			sql_free(s);
		}
		(void)fprintf(out, "\n");
	}
}

/*
 * True when the text holds a single statement: only then may it be run
 * through sql_session_query(), which hands back a result set instead of
 * printing it.
 */
static int is_single_stmt(const char *sql) {
	SQL_TOKENSTREAM *ts;
	size_t i, semi;
	int ret;

	ts = NULL;
	semi = 0;
	ret = sql_lex(sql, 0, &ts);
	if (ret != SQL_OK)
		return (0);
	for (i = 0; i < ts->ntokens; i++)
		if (ts->tokens[i].type == TK_PUNCT &&
		    ts->tokens[i].len == 1 && ts->tokens[i].text[0] == ';')
			semi++;
	sql_tokenstream_free(ts);

	return (semi <= 1);
}

static int run_sql(SQL_SESSION *sess, const char *sql) {
	SQL_RESULTSET *rs;
	size_t nrows;
	int ret;

	rs = NULL;
	nrows = 0;

	if (is_select(sql) && is_single_stmt(sql)) {
		ret = sql_session_query(sess, sql, &rs);
		if (ret == SQL_OK && rs != NULL) {
			if (out_mode == MODE_LIST)
				print_list(rs, stdout);
			else
				sql_resultset_print(rs, stdout);
			nrows = rs->nrows;
			sql_resultset_free(rs);
		}
	} else {
		size_t nstmt;

		nstmt = 0;
		ret = sql_session_executescript(sess, sql, stdout, &nstmt);
		if (ret == SQL_OK && nstmt > 1 && show_stats)
			(void)fprintf(stdout, "%zu statements executed.\n",
			    nstmt);
		if (ret == SQL_OK && is_dml(sql)) {
			(void)fprintf(stdout, "Query OK, %zu %s affected\n",
			    sess->rows_affected,
			    sess->rows_affected == 1 ? "row" : "rows");
		}
	}

	if (ret != SQL_OK) {
		const char *msg = sql_session_errmsg(sess);

		if (msg != NULL && *msg != '\0')
			(void)fprintf(stderr, "%s\n", msg);
		else
			(void)fprintf(stderr, "Error: %s\n", sql_errstr(ret));
		return (ret);
	}
	if (show_stats && nrows > 0)
		(void)fprintf(stdout, "%zu row(s).\n", nrows);

	return (SQL_OK);
}

/********************************************************************
 * Dot commands
 ********************************************************************/
static int dot_tables(SQL_SESSION *sess) {
	char **names;
	size_t n, i;
	int ret;

	ret = sql_catalog_list(sess, &names, &n);
	if (ret != SQL_OK) {
		(void)fprintf(stderr, "Error: cannot read catalog: %s\n",
		    sql_errstr(ret));
		return (SQL_OK);
	}
	if (n == 0)
		(void)printf("(no tables)\n");
	for (i = 0; i < n; i++)
		(void)printf("%s\n", names[i]);
	sql_catalog_list_free(names, n);

	return (SQL_OK);
}

static void print_create_table(SQL_SCHEMA *schema) {
	size_t c;

	(void)printf("CREATE TABLE %s (\n", schema->name);
	for (c = 0; c < schema->ncols; c++) {
		SQL_COLUMN *col;
		char *def;

		col = &schema->cols[c];
		def = NULL;
		if (col->has_default)
			(void)sql_value_tostring(&col->defval, &def);
		(void)printf("    %s %s", col->name, sql_type_name(col->type));
		if (col->length != 0)
			(void)printf("(%zu)", col->length);
		if (col->not_null)
			(void)printf(" NOT NULL");
		if (col->primary_key)
			(void)printf(" PRIMARY KEY");
		if (col->has_default) {
			(void)printf(" DEFAULT ");
			if (def != NULL) {
				if (SQL_TYPE_IS_STRING(col->type) ||
				    SQL_TYPE_IS_BINARY(col->type) ||
				    SQL_TYPE_IS_TEMPORAL(col->type))
					(void)fprintf(stdout, "'%s'", def);
				else
					(void)fprintf(stdout, "%s", def);
			}
		}
		(void)printf("%s\n", (c + 1 < schema->ncols ? "," : ""));
		sql_free(def);
	}
	(void)printf(");\n");
}

static int dot_schema(SQL_SESSION *sess, const char *arg) {
	char **names;
	size_t n, i;
	int found;
	int ret;

	ret = sql_catalog_list(sess, &names, &n);
	if (ret != SQL_OK) {
		(void)fprintf(stderr, "Error: cannot read catalog: %s\n",
		    sql_errstr(ret));
		return (SQL_OK);
	}
	found = 0;
	for (i = 0; i < n; i++) {
		SQL_SCHEMA *schema = NULL;

		if (arg != NULL && strcasecmp(names[i], arg) != 0)
			continue;
		if (sql_catalog_get_schema(sess, names[i],
		    &schema) != SQL_OK || schema == NULL)
			continue;
		print_create_table(schema);
		sql_schema_free(schema);
		found = 1;
	}
	sql_catalog_list_free(names, n);
	if (!found && arg != NULL)
		(void)printf("no such table: %s\n", arg);

	return (SQL_OK);
}

static int dot_command(SQL_SESSION *sess, char *line) {

	if (strcmp(line, ".quit") == 0 || strcmp(line, ".exit") == 0 ||
	    strcmp(line, ".q") == 0)
		return (1);
	if (strncmp(line, ".tables", 7) == 0) {
		char *arg = line + 7;

		while (*arg == ' ')
			arg++;
		if (*arg == ';')		/* tolerate a trailing ';' */
			*arg = '\0';
		(void)dot_tables(sess);
		return (0);
	}
	if (strncmp(line, ".schema", 7) == 0) {
		char *arg = line + 7;

		while (*arg == ' ')
			arg++;
		if (*arg != '\0') {
			size_t al = strlen(arg);
			if (arg[al - 1] == ';')
				arg[al - 1] = '\0';
		}
		(void)dot_schema(sess, (*arg == '\0' ? NULL : arg));
		return (0);
	}
	if (strncmp(line, ".mode", 5) == 0) {
		const char *arg;

		arg = line + 5;
		while (*arg == ' ')
			arg++;
		if (strncasecmp(arg, "list", 4) == 0)
			out_mode = MODE_LIST;
		else
			out_mode = MODE_COLUMN;
		return (0);
	}
	if (strncmp(line, ".stats", 6) == 0) {
		show_stats = !show_stats;
		return (0);
	}
	if (strncmp(line, ".help", 5) == 0) {
		(void)printf(
		    ".tables		  list the tables\n"
		    ".schema [table]  show the CREATE TABLE statement\n"
		    ".mode column|list  choose the output format\n"
		    ".stats		  toggle row counters\n"
		    ".quit		  leave the shell\n");
		return (0);
	}

	(void)fprintf(stderr, "unknown command: %s\n", line);
	return (0);
}

/********************************************************************
 * Input handling
 ********************************************************************/
static char *read_line(FILE *in, int *eofp) {
	size_t len, cap;
	char *buf;
	int c;

	cap = 256;
	len = 0;
	buf = (char *)malloc(cap);
	if (buf == NULL)
		return (NULL);
	for (;;) {
		c = fgetc(in);
		if (c == EOF && len == 0) {
			*eofp = 1;
			break;
		}
		if (c == EOF)
			break;
		if (c == '\r')
			continue;
		if (c == '\n')
			break;
		if (len + 1 >= cap) {
			char *p;

			cap *= 2;
			p = (char *)realloc(buf, cap);
			if (p == NULL) {
				free(buf);
				return (NULL);
			}
			buf = p;
		}
		buf[len++] = (char)c;
	}
	if (buf != NULL)
		buf[len] = '\0';

	return (buf);
}

static void trim_ws(char *str) {
	size_t len;

	while (*str != '\0' && isspace((unsigned char)*str))
		str++;
	len = strlen(str);
	while (len > 0 && isspace((unsigned char)str[len - 1]))
		str[--len] = '\0';
}

/*
 * Read one line of input.  On a tty we use linenoise so the user gets line
 * editing and command history; otherwise we fall back to the plain reader so
 * that piped input, -c and -f keep their existing behaviour unchanged.
 * Returns NULL on EOF; *eof is set to 1 in that case.
 */
static char *read_input(int tty, const char *prompt, int *eof) {
	char *s;

	if (tty) {
		s = linenoise(prompt);
		if (s == NULL)
			*eof = 1;
		return (s);
	}
	(void)printf("%s", prompt);
	(void)fflush(stdout);
	return (read_line(stdin, eof));
}

/* Build the per-user history file path into 'buf'; returns 0 on success. */
static int history_path(char *buf, size_t bufsz) {
	const char *home;

	home = getenv("HOME");
	if (home == NULL || *home == '\0')
		return (-1);
	if ((size_t)snprintf(buf, bufsz, "%s/.vansql_history", home) >= bufsz)
		return (-1);
	return (0);
}

static int interactive(SQL_SESSION *sess) {
	int eof, quit;
	int tty;
	char hpath[1024];

	tty = isatty(fileno(stdin));
	hpath[0] = '\0';
	if (tty && history_path(hpath, sizeof(hpath)) == 0) {
		(void)linenoiseHistoryLoad(hpath);
		(void)linenoiseHistorySetMaxLen(1000);
	}
	quit = 0;
	while (!quit) {
		char *line, *stmt;

		line = read_input(tty, "vansql> ", &eof);
		if (line == NULL) {
			if (eof)
				break;
			continue;
		}
		/* An empty line that hit end of file ends the session. */
		if (eof && line[0] == '\0') {
			free(line);
			break;
		}
		if (line[0] == '.') {
			quit = dot_command(sess, line);
			free(line);
			continue;
		}
		stmt = line;
		/* Statements may span lines. */
		while (strchr(stmt, ';') == NULL && !eof) {
			char *more, *joined;
			size_t l1, l2;

			more = read_input(tty, "   ...> ", &eof);
			if (more == NULL)
				break;
			l1 = strlen(stmt);
			l2 = strlen(more);
			joined = (char *)malloc(l1 + l2 + 2);
			if (joined == NULL) {
				free(more);
				break;
			}
			memcpy(joined, stmt, l1);
			joined[l1] = ' ';
			memcpy(joined + l1 + 1, more, l2 + 1);
			free(more);
			if (stmt != line)
				free(stmt);
			stmt = joined;
		}
		trim_ws(stmt);
		if (*stmt != '\0') {
			(void)run_sql(sess, stmt);
			if (tty)
				(void)linenoiseHistoryAdd(stmt);
		}
		if (stmt != line)
			free(stmt);
		free(line);
	}

	(void)printf("\n");
	if (tty && hpath[0] != '\0')
		(void)linenoiseHistorySave(hpath);
	return (SQL_OK);
}

/********************************************************************
 * Main
 ********************************************************************/
static int parse_args(int argc, char **argv, const char **dirp,
    const char **cmdsqlp, const char **scriptp, flags_t *oflagsp) {
	int i, npos;

	*dirp = ".";
	*cmdsqlp = NULL;
	*scriptp = NULL;
	*oflagsp = SQL_OPEN_CREATE;
	npos = 0;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			/*
			 * Free arguments are the store directory first,
			 * then the statement to run.
			 */
			if (npos == 0)
				*dirp = argv[i];
			else if (*cmdsqlp == NULL)
				*cmdsqlp = argv[i];
			npos++;
			continue;
		}
		if (strcmp(argv[i], "-c") == 0) {
			if (i + 1 >= argc)
				return (1);
			*cmdsqlp = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "-f") == 0) {
			if (i + 1 >= argc)
				return (1);
			*scriptp = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "-readonly") == 0) {
			*oflagsp |= SQL_OPEN_READONLY;
			continue;
		}
		if (strcmp(argv[i], "-column") == 0) {
			out_mode = MODE_COLUMN;
			continue;
		}
		if (strcmp(argv[i], "-list") == 0) {
			out_mode = MODE_LIST;
			continue;
		}
		if (strcmp(argv[i], "-help") == 0 ||
		    strcmp(argv[i], "-h") == 0)
			return (2);
		usage();
		return (1);
	}

	return (0);
}

/* Read a whole file into memory. */
static char *read_file(const char *path) {
	FILE *f;
	char *buf;
	size_t len, cap, got;

	f = fopen(path, "r");
	if (f == NULL)
		return (NULL);
	cap = 8192;
	len = 0;
	buf = (char *)malloc(cap);
	if (buf == NULL) {
		(void)fclose(f);
		return (NULL);
	}
	for (;;) {
		if (len + 4096 >= cap) {
			char *p;

			cap *= 2;
			p = (char *)realloc(buf, cap);
			if (p == NULL) {
				free(buf);
				(void)fclose(f);
				return (NULL);
			}
			buf = p;
		}
		got = fread(buf + len, 1, 4096, f);
		len += got;
		if (got < 4096)
			break;
	}
	buf[len] = '\0';
	(void)fclose(f);

	return (buf);
}

int main(int argc, char **argv) {
	SQL_SESSION *sess;
	const char *dir, *cmdsql, *script;
	char *filebuf;
	flags_t oflags;
	int ret, rc, status;

	if (argc > 0 && argv[0] != NULL)
		progname = argv[0];

	filebuf = NULL;
	sess = NULL;
	status = 0;

	rc = parse_args(argc, argv, &dir, &cmdsql, &script, &oflags);
	if (rc == 2) {
		usage();
		return (0);
	}
	if (rc != 0) {
		usage();
		return (1);
	}

	ret = sql_session_open(dir, oflags, &sess);
	if (ret != SQL_OK) {
		(void)fprintf(stderr, "%s: cannot open store '%s': %s\n",
		    progname, dir, sql_errstr(ret));
		return (1);
	}

	if (script != NULL) {
		filebuf = read_file(script);
		if (filebuf == NULL) {
			(void)fprintf(stderr, "%s: cannot read '%s'\n",
			    progname, script);
			status = 1;
			goto done;
		}
		ret = run_sql(sess, filebuf);
		if (ret != SQL_OK)
			status = 1;
		goto done;
	}
	if (cmdsql != NULL) {
		ret = run_sql(sess, cmdsql);
		if (ret != SQL_OK)
			status = 1;
		goto done;
	}

	(void)interactive(sess);

done:
	ret = sql_session_close(sess);
	if (ret != SQL_OK) {
		(void)fprintf(stderr, "%s: close failed: %s\n", progname,
		    sql_errstr(ret));
		if (status == 0)
			status = 1;
	}
	free(filebuf);

	return (status);
}
