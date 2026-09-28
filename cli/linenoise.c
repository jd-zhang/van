/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Minimal linenoise-compatible line editing, used by vansql.
 *
 * Single file, no third-party dependencies.  Implements raw-mode editing
 * (left/right, home/end, backspace, delete, and up/down history recall) and a
 * plain-text history file.  Only the API surface that vansql consumes is
 * provided; behaviour is intentionally close to antirez/linenoise for the
 * functions that are present.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>

#define LINENOISE_MAX_LINE 4096
#define LINENOISE_HISTORY_CAP 1000

typedef struct {
	char	buf[LINENOISE_MAX_LINE];
	int	len;
	int	pos;
	int	hindex;		/* history_len == bottom (current input) */
	int	saved_valid;
	char	saved[LINENOISE_MAX_LINE];
	int	saved_len;
	int	saved_pos;
	const char *prompt;
} linenoiseState;

static char *history[LINENOISE_HISTORY_CAP];
static int history_len = 0;
static int history_maxlen = LINENOISE_HISTORY_CAP;

static struct termios orig_termios;
static int rawmode = 0;
static volatile sig_atomic_t got_signal = 0;

static void
linenoiseSignalHandler(int sig)
{
	(void)sig;
	got_signal = 1;
}

static int
enableRawMode(void)
{
	struct termios raw;

	if (rawmode)
		return (0);
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
		return (-1);
	raw = orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		return (-1);
	rawmode = 1;
	return (0);
}

static void
disableRawMode(void)
{
	if (!rawmode)
		return;
	(void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
	rawmode = 0;
}

static void
linenoiseWrite(const char *s, int n)
{
	int w = (int)write(STDOUT_FILENO, s, n);
	(void)w;
}

static void
refreshLine(linenoiseState *st)
{
	char seq[512];
	int n = 0;
	int plen, back, ml;
	char m[16];

	(void)fflush(stdout);
	seq[n++] = '\r';
	seq[n++] = 0x1b; seq[n++] = '['; seq[n++] = 'K';	/* clear to EOL */
	plen = (int)strlen(st->prompt);
	memcpy(seq + n, st->prompt, plen); n += plen;
	memcpy(seq + n, st->buf, st->len); n += st->len;
	if (st->pos < st->len) {
		back = st->len - st->pos;
		ml = snprintf(m, sizeof(m), "\x1b[%dD", back);
		memcpy(seq + n, m, ml); n += ml;
	}
	linenoiseWrite(seq, n);
}

static void
historySet(linenoiseState *st, const char *s)
{
	int l = (int)strlen(s);

	if (l > LINENOISE_MAX_LINE - 1)
		l = LINENOISE_MAX_LINE - 1;
	memcpy(st->buf, s, l);
	st->buf[l] = '\0';
	st->len = l;
	st->pos = l;
}

static void
histPrev(linenoiseState *st)
{
	if (history_len == 0)
		return;
	if (st->hindex == history_len && !st->saved_valid) {
		memcpy(st->saved, st->buf, st->len + 1);
		st->saved_len = st->len;
		st->saved_pos = st->pos;
		st->saved_valid = 1;
	}
	if (st->hindex > 0)
		st->hindex--;
	historySet(st, history[st->hindex]);
}

static void
histNext(linenoiseState *st)
{
	if (history_len == 0)
		return;
	if (st->hindex < history_len)
		st->hindex++;
	if (st->hindex >= history_len) {
		if (st->saved_valid) {
			memcpy(st->buf, st->saved, st->saved_len + 1);
			st->len = st->saved_len;
			st->pos = st->saved_pos;
		} else {
			st->buf[0] = '\0';
			st->len = 0;
			st->pos = 0;
		}
	} else {
		historySet(st, history[st->hindex]);
	}
}

static void
handleEscape(linenoiseState *st)
{
	char b0, b1;

	if (read(STDIN_FILENO, &b0, 1) != 1)
		return;
	if (b0 != '[' && b0 != 'O')
		return;
	if (read(STDIN_FILENO, &b1, 1) != 1)
		return;
	switch (b1) {
	case 'A': histPrev(st); break;		/* up */
	case 'B': histNext(st); break;		/* down */
	case 'C': if (st->pos < st->len) st->pos++; break;	/* right */
	case 'D': if (st->pos > 0) st->pos--; break;		/* left */
	case 'H': st->pos = 0; break;		/* home */
	case 'F': st->pos = st->len; break;	/* end */
	case '3': {				/* delete */
		char t;
		if (read(STDIN_FILENO, &t, 1) == 1 && t == '~' &&
		    st->pos < st->len) {
			memmove(st->buf + st->pos, st->buf + st->pos + 1,
			    st->len - st->pos);
			st->len--;
		}
	} break;
	default: break;
	}
}

/* Used only when stdin is not a tty; keeps a sane fallback path. */
static char *
fallbackLine(const char *prompt)
{
	char tmp[LINENOISE_MAX_LINE];

	if (prompt != NULL)
		(void)fputs(prompt, stdout);
	(void)fflush(stdout);
	if (fgets(tmp, sizeof(tmp), stdin) == NULL)
		return (NULL);
	tmp[strcspn(tmp, "\r\n")] = '\0';
	return (strdup(tmp));
}

char *
linenoise(const char *prompt)
{
	linenoiseState st;
	struct sigaction sa, sa_oldint, sa_oldquit;
	char c;
	int eofflag = 0;

	memset(&st, 0, sizeof(st));
	st.prompt = prompt != NULL ? prompt : "";
	st.hindex = history_len;

	if (!isatty(STDIN_FILENO) || enableRawMode() != 0)
		return (fallbackLine(prompt));

	got_signal = 0;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = linenoiseSignalHandler;
	sigemptyset(&sa.sa_mask);
	(void)sigaction(SIGINT, &sa, &sa_oldint);
	(void)sigaction(SIGQUIT, &sa, &sa_oldquit);

	refreshLine(&st);
	for (;;) {
		ssize_t n = read(STDIN_FILENO, &c, 1);

		if (n <= 0) {
			if (errno == EINTR) {
				if (got_signal)
					break;
				continue;
			}
			break;
		}
		if (c == '\n' || c == '\r')
			break;
		if (c == 3) {			/* Ctrl-C */
			got_signal = 1;
			break;
		}
		if (c == 4 && st.len == 0) {	/* Ctrl-D on empty line */
			eofflag = 1;
			break;
		}
		if (c == 127 || c == 8) {	/* backspace */
			if (st.pos > 0) {
				memmove(st.buf + st.pos - 1, st.buf + st.pos,
				    st.len - st.pos);
				st.len--;
				st.pos--;
			}
		} else if (c == 27) {		/* escape */
			handleEscape(&st);
		} else if (c >= 32 && c != 127) {	/* printable */
			if (st.len < LINENOISE_MAX_LINE - 1) {
				if (st.pos < st.len)
					memmove(st.buf + st.pos + 1, st.buf + st.pos,
					    st.len - st.pos);
				st.buf[st.pos] = c;
				st.len++;
				st.pos++;
			}
		}
		refreshLine(&st);
	}

	linenoiseWrite("\r\n", 2);
	(void)sigaction(SIGINT, &sa_oldint, NULL);
	(void)sigaction(SIGQUIT, &sa_oldquit, NULL);
	disableRawMode();

	if (got_signal || eofflag)
		return (NULL);
	st.buf[st.len] = '\0';
	return (strdup(st.buf));
}

int
linenoiseHistoryAdd(const char *line)
{
	char *copy;

	if (history_maxlen <= 0)
		return (0);
	if (history_len > 0 &&
	    strcmp(history[history_len - 1], line) == 0)
		return (0);			/* skip duplicates */
	if (history_len >= history_maxlen) {
		free(history[0]);
		memmove(history, history + 1,
		    (history_maxlen - 1) * sizeof(char *));
		history_len--;
	}
	copy = strdup(line);
	if (copy == NULL)
		return (-1);
	history[history_len++] = copy;
	return (0);
}

int
linenoiseHistorySetMaxLen(int len)
{
	if (len > LINENOISE_HISTORY_CAP)
		len = LINENOISE_HISTORY_CAP;
	if (len < 1)
		len = 1;
	history_maxlen = len;
	if (history_len > history_maxlen)
		history_len = history_maxlen;
	return (0);
}

int
linenoiseHistoryLoad(const char *path)
{
	FILE *f;
	char *line = NULL;
	size_t cap = 0;
	ssize_t n;

	if (path == NULL)
		return (-1);
	f = fopen(path, "r");
	if (f == NULL)
		return (-1);
	while ((n = getline(&line, &cap, f)) != -1) {
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = '\0';
		if (n > 0)
			(void)linenoiseHistoryAdd(line);
	}
	free(line);
	(void)fclose(f);
	return (0);
}

int
linenoiseHistorySave(const char *path)
{
	FILE *f;
	int i;

	if (path == NULL)
		return (-1);
	f = fopen(path, "w");
	if (f == NULL)
		return (-1);
	for (i = 0; i < history_len; i++)
		if (fprintf(f, "%s\n", history[i]) < 0) {
			(void)fclose(f);
			return (-1);
		}
	(void)fclose(f);
	return (0);
}
