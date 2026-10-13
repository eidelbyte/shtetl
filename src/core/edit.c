/*
 * A message from an editor (planning.md 6.2), as git commit takes one:
 * the editor named by $VISUAL or $EDITOR, which may carry arguments,
 * so run by /bin/sh with the file as its $1, as git runs it. It is
 * the one program shtetl runs on the terminal itself, which sh_run
 * keeps every other from (src/core/run.c), so it is run here, as
 * system(3) runs one: shtetl waits, ignoring the terminal's signals.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/edit.h"
#include "core/err.h"

/* CMD run by /bin/sh, PATH its $1, on the terminal: 0 if it exited 0. */
static int
ed_run(const char *cmd, const char *path)
{
	struct sigaction ign, oint, oquit;
	pid_t pid;
	int st, r;

	(void) memset(&ign, 0, sizeof (ign));
	ign.sa_handler = SIG_IGN;
	(void) sigemptyset(&ign.sa_mask);
	(void) sigaction(SIGINT, &ign, &oint);
	(void) sigaction(SIGQUIT, &ign, &oquit);
	r = -1;
	if ((pid = fork()) == 0) {
		(void) sigaction(SIGINT, &oint, NULL);
		(void) sigaction(SIGQUIT, &oquit, NULL);
		(void) execl("/bin/sh", "sh", "-c", cmd, "sh", path,
		    (char *)NULL);
		_exit(127);
	}
	if (pid > 0) {
		while (waitpid(pid, &st, 0) == -1 && errno == EINTR)
			;
		r = WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
	}
	(void) sigaction(SIGINT, &oint, NULL);
	(void) sigaction(SIGQUIT, &oquit, NULL);
	return (r);
}

char *
sh_edit_strip(const char *text)
{
	const char *p, *nl;
	char *out;
	size_t n, len;

	if ((out = malloc(strlen(text) + 1)) == NULL)
		return (NULL);
	n = 0;
	for (p = text; *p != '\0'; p = nl) {
		if ((nl = strchr(p, '\n')) != NULL)
			nl++;
		else
			nl = p + strlen(p);
		len = (size_t)(nl - p);
		if (*p == '#')
			continue;
		/* leading blank lines: none kept before the first word */
		if (n == 0 && strspn(p, " \t\r\n") >= len)
			continue;
		(void) memcpy(out + n, p, len);
		n += len;
	}
	/* trailing blank lines and spaces */
	while (n > 0 && strchr(" \t\r\n", out[n - 1]) != NULL)
		n--;
	out[n] = '\0';
	return (out);
}

int
sh_edit(const char *hint, char **msg, struct sh_err *e)
{
	char path[1024], cmd[2048], *text;
	const char *editor, *t, *p, *nl;
	FILE *f;
	long size;
	int fd, rc;

	*msg = NULL;
	if ((editor = getenv("VISUAL")) == NULL || *editor == '\0')
		editor = getenv("EDITOR");
	if (editor == NULL || *editor == '\0')
		editor = "vi";
	t = getenv("TMPDIR");
	(void) snprintf(path, sizeof (path), "%s/shtetl-message.XXXXXX",
	    t != NULL && *t != '\0' ? t : "/tmp");
	if ((fd = mkstemp(path)) == -1 || (f = fdopen(fd, "w")) == NULL) {
		sh_errf(e, "%s: %s", path, strerror(errno));
		if (fd != -1)
			(void) close(fd);
		return (-1);
	}
	(void) fputc('\n', f);
	for (p = hint; *p != '\0'; p = nl) {
		if ((nl = strchr(p, '\n')) != NULL)
			nl++;
		else
			nl = p + strlen(p);
		(void) fprintf(f, "# %.*s%s", (int)(nl - p), p,
		    nl[-1] == '\n' ? "" : "\n");
	}
	if (fclose(f) != 0) {
		sh_errf(e, "%s: %s", path, strerror(errno));
		(void) unlink(path);
		return (-1);
	}
	(void) snprintf(cmd, sizeof (cmd), "%s \"$1\"", editor);
	rc = ed_run(cmd, path);
	text = NULL;
	if (rc == 0 && (f = fopen(path, "r")) != NULL) {
		if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 &&
		    fseek(f, 0, SEEK_SET) == 0 &&
		    (text = calloc(1, (size_t)size + 1)) != NULL &&
		    fread(text, 1, (size_t)size, f) != (size_t)size) {
			free(text);
			text = NULL;
		}
		(void) fclose(f);
	}
	(void) unlink(path);
	if (rc != 0) {
		sh_errf(e, "the message's editor failed");
		return (-1);
	}
	if (text == NULL || (*msg = sh_edit_strip(text)) == NULL) {
		free(text);
		sh_errf(e, "%s: cannot be read back", path);
		return (-1);
	}
	free(text);
	if (**msg == '\0') {
		free(*msg);
		*msg = NULL;
		sh_errf(e, "empty message");
		return (-1);
	}
	return (0);
}
