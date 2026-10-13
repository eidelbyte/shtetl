/*
 * A message a person writes in an editor, as git commit takes one
 * (planning.md 6.2: a save's message when -m is not given).
 */

#ifndef	SH_EDIT_H
#define	SH_EDIT_H

#include "core/err.h"

/*
 * $VISUAL, or $EDITOR, or vi, run by /bin/sh on a temporary file
 * holding HINT, each of its lines as a # line; what comes back, the
 * lines beginning # and blank lines at either end left out, into
 * *MSG (the caller frees it). Refused: the editor failing; the
 * message empty.
 */
int sh_edit(const char *hint, char **msg, struct sh_err *e);

/* TEXT as a message: # lines and blank lines at either end gone. */
char *sh_edit_strip(const char *text);

#endif	/* SH_EDIT_H */
