/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LINENOISE_H
#define LINENOISE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tiny, dependency-free line-editing helper exposing the subset of the
 * linenoise API used by vansql:
 *
 *   linenoise()              - read one line (raw-mode editing on a tty)
 *   linenoiseHistoryAdd()    - append a line to the in-memory history
 *   linenoiseHistorySetMaxLen() - cap the history length
 *   linenoiseHistoryLoad()   - load history from a plain-text file
 *   linenoiseHistorySave()   - persist history to a plain-text file
 *
 * No external libraries are required; everything it needs lives in the
 * C standard library, termios, signal and unistd.
 */

/* Read one line after printing 'prompt'.  Returns a malloc'd, NUL-terminated
 * string without the trailing newline, or NULL on EOF (Ctrl-D on an empty
 * line) or Ctrl-C.  The caller must free() the result. */
char *linenoise(const char *prompt);

/* History management.  Return 0 on success, -1 on error. */
int linenoiseHistoryAdd(const char *line);
int linenoiseHistorySetMaxLen(int len);
int linenoiseHistoryLoad(const char *path);
int linenoiseHistorySave(const char *path);

#ifdef __cplusplus
}
#endif
#endif
