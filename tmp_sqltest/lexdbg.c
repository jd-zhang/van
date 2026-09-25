/* Temporary lexer debugging helper, reads a file. */
#include "sql_internal.h"
#include "sql_ext.h"

static char *slurp(const char *path) {
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
			if (p == NULL)
				break;
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
	SQL_TOKENSTREAM *ts;
	char *text;
	size_t i;
	int ret;

	if (argc < 2)
		return (1);
	ts = NULL;
	text = slurp(argv[1]);
	if (text == NULL)
		return (1);
	ret = sql_lex(text, 0, &ts);
	(void)printf("lex ret=%d ntokens=%zu\n", ret,
	    (ts == NULL ? 0 : ts->ntokens));
	if (ret == SQL_OK && ts != NULL) {
		for (i = 0; i < ts->ntokens; i++)
			(void)printf("%2zu type=%d kw=%d off=%zu len=%zu "
			    "[%.*s]\n", i, ts->tokens[i].type,
			    ts->tokens[i].kw, ts->tokens[i].off,
			    ts->tokens[i].len, (int)ts->tokens[i].len,
			    ts->tokens[i].text);
	}
	if (ts != NULL)
		sql_tokenstream_free(ts);
	free(text);

	return (0);
}
