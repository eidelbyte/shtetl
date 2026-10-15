/*
 * zfs_rebase as shtetl runs it (planning.md 9, 7.6.1, 7.7).
 *
 * The real rebaser runs the tool by its name on SH_RUN_PATH, its output
 * into the caller's log, but for the picker (--continue -i), which is
 * given the terminal shtetl has, as an editor is (src/core/edit.c). The
 * resolution is the one text the tool hands shtetl (v4-manifest.md 8):
 * a header, then the tree, each directory's children after it and ".."
 * closing it, a name's bytes outside 0x21 - 0x7e (and the backslash and
 * the hash) written as a backslash and three octal digits. shtetl reads
 * its conflict and drift lines and answers some of them keep; every
 * other byte goes back as it came, and #unanswered, which the tool
 * holds to the lines, is counted again.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/err.h"
#include "core/rebase.h"
#include "core/run.h"

#define	RB_WORKDIR	"/var/db/zfs_rebase"	/* the tool's (run.c) */
#define	RB_DEPTH	256			/* directories deep */

const char *
sh_rb_said(int rc)
{
	switch (rc) {
	case SH_RB_DONE:
		return ("done");
	case SH_RB_GATE:
		return ("stopped at conflicts");
	case SH_RB_REFUSED:
		return ("refused, nothing touched");
	case SH_RB_FAILED:
		return ("failed part way, or drifted");
	}
	return ("not run");
}

void
sh_rb_privmnt(const char *result, char *buf, size_t len)
{
	(void) snprintf(buf, len, "%s/%s/mnt", RB_WORKDIR, result);
}

/* WORDS copied, NULL-ended, for exec: N words; NULL out of memory. */
static char **
rb_argv(const char *const *words, size_t *n)
{
	char **argv;
	size_t i;

	for (*n = 0; words[*n] != NULL; (*n)++)
		;
	if ((argv = calloc(*n + 1, sizeof (*argv))) == NULL)
		return (NULL);
	for (i = 0; i < *n; i++) {
		if ((argv[i] = strdup(words[i])) == NULL) {
			while (i > 0)
				free(argv[--i]);
			free(argv);
			return (NULL);
		}
	}
	return (argv);
}

static void
rb_argv_free(char **argv, size_t n)
{
	size_t i;

	for (i = 0; argv != NULL && i < n; i++)
		free(argv[i]);
	free(argv);
}

/*
 * WORDS run with the output to LOG: the tool's exit, or -1 if it did
 * not exit as the tool exits, sh_run's word on it passed on to E. An
 * exit above 0 is the tool's answer, not a failure of the run.
 */
static int
rb_run(const char *const *words, int log, struct sh_err *e)
{
	struct sh_err q;
	struct sh_run r;
	char **argv, *said;
	size_t n, saidlen;
	FILE *f;
	int rc, rr;

	if ((argv = rb_argv(words, &n)) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	said = NULL;
	if ((f = open_memstream(&said, &saidlen)) == NULL) {
		rb_argv_free(argv, n);
		sh_err_nomem(e);
		return (-1);
	}
	sh_err_init(&q, f, "");
	(void) memset(&r, 0, sizeof (r));
	r.ru_argv = argv;
	r.ru_log = log;
	rr = sh_run(&r, &q);
	(void) fclose(f);
	rb_argv_free(argv, n);
	free(r.ru_out);
	/* 0, or the tool's own answer; anything else it did not run */
	if (rr == 0)
		rc = SH_RB_DONE;
	else if (r.ru_signal == 0 && WIFEXITED(r.ru_status) &&
	    WEXITSTATUS(r.ru_status) >= SH_RB_GATE &&
	    WEXITSTATUS(r.ru_status) <= SH_RB_FAILED)
		rc = WEXITSTATUS(r.ru_status);
	else {
		if (said != NULL && saidlen > 0 && said[saidlen - 1] == '\n')
			said[saidlen - 1] = '\0';
		sh_errf(e, "%s", said != NULL && *said != '\0' ? said :
		    "zfs_rebase: could not be run");
		rc = -1;
	}
	free(said);
	return (rc);
}

/*
 * WORDS run on the terminal, as an editor is: SIGINT and SIGQUIT
 * ignored here while the child has them. The exit, or -1.
 */
static int
rb_tty(const char *const *words, struct sh_err *e)
{
	struct sigaction ign, oint, oquit;
	char path[1024], **argv;
	size_t n;
	pid_t pid;
	int st, rc;

	if (sh_run_find(words[0], path, sizeof (path)) != 0) {
		sh_errf(e, "%s: not found on %s", words[0], SH_RUN_PATH);
		return (-1);
	}
	if ((argv = rb_argv(words, &n)) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	(void) memset(&ign, 0, sizeof (ign));
	ign.sa_handler = SIG_IGN;
	(void) sigemptyset(&ign.sa_mask);
	(void) sigaction(SIGINT, &ign, &oint);
	(void) sigaction(SIGQUIT, &ign, &oquit);
	rc = -1;
	if ((pid = fork()) == 0) {
		(void) sigaction(SIGINT, &oint, NULL);
		(void) sigaction(SIGQUIT, &oquit, NULL);
		(void) execv(path, argv);
		_exit(127);
	}
	if (pid > 0) {
		while (waitpid(pid, &st, 0) == -1 && errno == EINTR)
			;
		if (WIFEXITED(st) && WEXITSTATUS(st) <= SH_RB_FAILED)
			rc = WEXITSTATUS(st);
	}
	(void) sigaction(SIGINT, &oint, NULL);
	(void) sigaction(SIGQUIT, &oquit, NULL);
	rb_argv_free(argv, n);
	if (rc == -1)
		sh_errf(e, "%s: could not be run", words[0]);
	return (rc);
}

static int
zr_start(const struct sh_rebaser *self, const struct sh_rbstart *s, int log,
    struct sh_err *e)
{
	const char *w[14];
	size_t n;

	n = 0;
	w[n++] = self->rb_tool;
	if (s->rs_base != NULL) {
		w[n++] = "--allow-unrelated";
		w[n++] = "--base";
		w[n++] = s->rs_base;
	}
	w[n++] = "--from";
	w[n++] = s->rs_from;
	w[n++] = "--onto";
	w[n++] = s->rs_onto;
	w[n++] = "--result";
	w[n++] = s->rs_result;
	w[n++] = "-o";
	w[n++] = s->rs_manifest;
	if (s->rs_merge)
		w[n++] = "-m";
	w[n] = NULL;
	return (rb_run(w, log, e));
}

static int
zr_continue(const struct sh_rebaser *self, const char *ident,
    unsigned flags, int log, struct sh_err *e)
{
	const char *w[8];
	size_t n;

	n = 0;
	w[n++] = self->rb_tool;
	w[n++] = "--continue";
	if (flags & SH_RB_MERGE)
		w[n++] = "-m";
	if (flags & SH_RB_NOMERGE)
		w[n++] = "--no-merge";
	if (flags & SH_RB_PICK)
		w[n++] = "-i";
	w[n++] = ident;
	w[n] = NULL;
	return ((flags & SH_RB_PICK) ? rb_tty(w, e) : rb_run(w, log, e));
}

static int
zr_abort(const struct sh_rebaser *self, const char *ident, int log,
    struct sh_err *e)
{
	const char *w[4];

	w[0] = self->rb_tool;
	w[1] = "--abort";
	w[2] = ident;
	w[3] = NULL;
	return (rb_run(w, log, e));
}

const struct sh_rebaser sh_zfs_rebaser = {
	zr_start, zr_continue, zr_abort, "zfs_rebase", NULL
};

/* ---- the resolution ------------------------------------------------ */

/* NAME as the tree escapes it, decoded: malloc'd, or NULL if not one. */
static char *
rs_decode(const char *name, size_t len)
{
	char *out;
	size_t i, n;
	int b, k;

	if ((out = malloc(len + 1)) == NULL)
		return (NULL);
	for (i = n = 0; i < len; i++) {
		if (name[i] != '\\') {
			out[n++] = name[i];
			continue;
		}
		for (b = 0, k = 1; k <= 3; k++) {
			if (i + (size_t)k >= len || name[i + k] < '0' ||
			    name[i + k] > '7') {
				free(out);
				return (NULL);
			}
			b = b * 8 + (name[i + k] - '0');
		}
		if (b > 255) {
			free(out);
			return (NULL);
		}
		out[n++] = (char)b;
		i += 3;
	}
	out[n] = '\0';
	return (out);
}

/* The words of LINE, at most MAX, their starts and lengths. */
static size_t
rs_words(const char *line, const char **w, size_t *wl, size_t max)
{
	size_t n;

	for (n = 0; n < max; ) {
		while (*line == ' ' || *line == '\t')
			line++;
		if (*line == '\0')
			break;
		w[n] = line;
		while (*line != '\0' && *line != ' ' && *line != '\t')
			line++;
		wl[n] = (size_t)(line - w[n]);
		n++;
	}
	return (n);
}

static int
rs_choice(const char *w, size_t len)
{
	return ((len == 1 && *w == '-') ||
	    (len == 4 && strncmp(w, "keep", 4) == 0) ||
	    (len == 4 && strncmp(w, "onto", 4) == 0) ||
	    (len == 4 && strncmp(w, "from", 4) == 0));
}

/* A count, the word W: 0, or -1 when it is none. */
static int
rs_count(const char *w, size_t len, unsigned *v)
{
	size_t i;

	if (len == 0 || len > 9)
		return (-1);
	for (*v = 0, i = 0; i < len; i++) {
		if (w[i] < '0' || w[i] > '9')
			return (-1);
		*v = *v * 10 + (unsigned)(w[i] - '0');
	}
	return (0);
}

/* The file at PATH as lines, no newlines, into R. */
static int
rs_lines(const char *path, struct sh_resolution *r, struct sh_err *e)
{
	char buf[4096], *line, **nv;
	size_t len, cap;
	FILE *f;

	if ((f = fopen(path, "r")) == NULL) {
		sh_errf(e, "%s: %s", path, strerror(errno));
		return (-1);
	}
	cap = 0;
	line = NULL;
	len = 0;
	while (fgets(buf, sizeof (buf), f) != NULL) {
		size_t n = strlen(buf);
		char *nl = realloc(line, len + n + 1);

		if (nl == NULL)
			goto nomem;
		line = nl;
		(void) memcpy(line + len, buf, n + 1);
		len += n;
		if (len == 0 || line[len - 1] != '\n')
			continue;
		line[len - 1] = '\0';
		if (r->rs_ntext == cap) {
			cap = cap == 0 ? 64 : cap * 2;
			if ((nv = realloc(r->rs_text, cap * sizeof (*nv))) ==
			    NULL)
				goto nomem;
			r->rs_text = nv;
		}
		r->rs_text[r->rs_ntext++] = line;
		line = NULL;
		len = 0;
	}
	(void) fclose(f);
	if (line != NULL) {
		sh_errf(e, "%s: its last line has no end", path);
		free(line);
		return (-1);
	}
	return (0);
nomem:
	(void) fclose(f);
	free(line);
	sh_err_nomem(e);
	return (-1);
}

int
sh_res_read(const char *path, struct sh_resolution *r, struct sh_err *e)
{
	const char *w[5];
	char *dirs[RB_DEPTH];
	size_t wl[5], depth, i, k, n, plen;
	struct sh_resline *nl, *l;
	unsigned declared;
	char *name, *full;
	int intree, haveun, root;

	(void) memset(r, 0, sizeof (*r));
	if (rs_lines(path, r, e) != 0)
		return (-1);
	intree = haveun = root = 0;
	depth = 0;
	declared = 0;
	for (i = 0; i < r->rs_ntext; i++) {
		n = rs_words(r->rs_text[i], w, wl, 5);
		if (n == 0)
			continue;
		if (*w[0] == '#') {
			if (wl[0] == 11 &&
			    strncmp(w[0], "#unanswered", 11) == 0 && n == 2 &&
			    rs_count(w[1], wl[1], &declared) == 0) {
				haveun = 1;
				r->rs_unline = i;
			} else if (wl[0] == 6 &&
			    strncmp(w[0], "#names", 6) == 0 && n == 2)
				(void) rs_count(w[1], wl[1], &r->rs_names);
			continue;
		}
		if (!intree) {
			if (n != 1 || wl[0] != 1 || *w[0] != '/')
				goto bad;
			intree = root = 1;
			continue;
		}
		if (n == 1 && wl[0] == 2 && strncmp(w[0], "..", 2) == 0) {
			if (depth == 0) {
				/* the root's: the tree ends */
				root = 0;
				continue;
			}
			free(dirs[--depth]);
			continue;
		}
		if (!root)
			goto bad;
		/* a name, a directory's with a trailing slash */
		plen = wl[0];
		k = plen > 1 && w[0][plen - 1] == '/' ? plen - 1 : plen;
		if (n > 1) {
			if ((r->rs_n % 32) == 0) {
				if ((nl = realloc(r->rs_lines, (r->rs_n + 32) *
				    sizeof (*nl))) == NULL)
					goto nomem;
				r->rs_lines = nl;
			}
			l = &r->rs_lines[r->rs_n];
			(void) memset(l, 0, sizeof (*l));
			if (n == 4 && wl[1] == 8 &&
			    strncmp(w[1], "conflict", 8) == 0 &&
			    rs_count(w[2], wl[2], &l->rl_group) == 0 &&
			    rs_choice(w[3], wl[3])) {
				l->rl_conflict = 1;
				(void) snprintf(l->rl_choice,
				    sizeof (l->rl_choice), "%.*s", (int)wl[3],
				    w[3]);
			} else if (n == 3 && wl[1] == 5 &&
			    strncmp(w[1], "drift", 5) == 0 &&
			    rs_choice(w[2], wl[2])) {
				(void) snprintf(l->rl_choice,
				    sizeof (l->rl_choice), "%.*s", (int)wl[2],
				    w[2]);
			} else {
				goto bad;
			}
			if ((name = rs_decode(w[0], k)) == NULL)
				goto bad;
			plen = 2;
			for (k = 0; k < depth; k++)
				plen += strlen(dirs[k]) + 1;
			plen += strlen(name);
			if ((full = malloc(plen + 1)) == NULL) {
				free(name);
				goto nomem;
			}
			full[0] = '\0';
			for (k = 0; k < depth; k++)
				(void) snprintf(full + strlen(full),
				    plen + 1 - strlen(full), "/%s", dirs[k]);
			(void) snprintf(full + strlen(full),
			    plen + 1 - strlen(full), "/%s", name);
			free(name);
			l->rl_path = full;
			l->rl_line = i;
			r->rs_n++;
		}
		if (w[0][wl[0] - 1] == '/' && wl[0] > 1) {
			if (depth == RB_DEPTH)
				goto bad;
			if ((dirs[depth] = rs_decode(w[0], wl[0] - 1)) ==
			    NULL)
				goto bad;
			depth++;
		}
	}
	if (!intree || root || depth != 0) {
		while (depth > 0)
			free(dirs[--depth]);
		sh_errf(e, "%s: the tree does not close", path);
		return (-1);
	}
	if (!haveun) {
		sh_errf(e, "%s: no #unanswered", path);
		return (-1);
	}
	if (sh_res_unanswered(r) != declared) {
		sh_errf(e, "%s: #unanswered says %u but %u of the lines are",
		    path, declared, sh_res_unanswered(r));
		return (-1);
	}
	return (0);
bad:
	while (depth > 0)
		free(dirs[--depth]);
	sh_errf(e, "%s:%zu: not a resolution's line", path, i + 1);
	return (-1);
nomem:
	while (depth > 0)
		free(dirs[--depth]);
	sh_err_nomem(e);
	return (-1);
}

unsigned
sh_res_unanswered(const struct sh_resolution *r)
{
	unsigned n;
	size_t i;

	for (n = 0, i = 0; i < r->rs_n; i++) {
		if (strcmp(r->rs_lines[i].rl_choice, "-") == 0)
			n++;
	}
	return (n);
}

const struct sh_resline *
sh_res_find(const struct sh_resolution *r, const char *path)
{
	size_t i;

	for (i = 0; i < r->rs_n; i++) {
		if (strcmp(r->rs_lines[i].rl_path, path) == 0)
			return (&r->rs_lines[i]);
	}
	return (NULL);
}

/* LINE with its last word, the choice, made CHOICE: malloc'd, or NULL. */
static char *
rs_rechoose(const char *line, const char *choice)
{
	const char *end, *start;
	size_t lead;
	char *out;

	end = line + strlen(line);
	while (end > line && (end[-1] == ' ' || end[-1] == '\t'))
		end--;
	start = end;
	while (start > line && start[-1] != ' ' && start[-1] != '\t')
		start--;
	lead = (size_t)(start - line);
	if ((out = malloc(lead + strlen(choice) + strlen(end) + 1)) == NULL)
		return (NULL);
	(void) memcpy(out, line, lead);
	(void) strcpy(out + lead, choice);
	(void) strcat(out, end);
	return (out);
}

size_t
sh_res_keep(struct sh_resolution *r, const char *const *paths, size_t n)
{
	struct sh_resline *l;
	char *nl, *un;
	size_t i, j, done;

	done = 0;
	for (i = 0; i < r->rs_n; i++) {
		l = &r->rs_lines[i];
		if (strcmp(l->rl_choice, "-") != 0)
			continue;
		for (j = 0; j < n; j++) {
			if (strcmp(l->rl_path, paths[j]) == 0)
				break;
		}
		if (j == n ||
		    (nl = rs_rechoose(r->rs_text[l->rl_line], "keep")) == NULL)
			continue;
		free(r->rs_text[l->rl_line]);
		r->rs_text[l->rl_line] = nl;
		(void) snprintf(l->rl_choice, sizeof (l->rl_choice), "keep");
		done++;
	}
	if (done > 0 && (un = malloc(32)) != NULL) {
		(void) snprintf(un, 32, "#unanswered %u", sh_res_unanswered(r));
		free(r->rs_text[r->rs_unline]);
		r->rs_text[r->rs_unline] = un;
	}
	return (done);
}

int
sh_res_write(const char *path, const struct sh_resolution *r,
    struct sh_err *e)
{
	char tmp[1100];
	size_t i;
	FILE *f;
	int bad;

	(void) snprintf(tmp, sizeof (tmp), "%s.shtetl", path);
	if ((f = fopen(tmp, "w")) == NULL) {
		sh_errf(e, "%s: %s", tmp, strerror(errno));
		return (-1);
	}
	bad = 0;
	for (i = 0; i < r->rs_ntext; i++) {
		if (fprintf(f, "%s\n", r->rs_text[i]) < 0)
			bad = 1;
	}
	if (fclose(f) != 0 || bad || rename(tmp, path) != 0) {
		sh_errf(e, "%s: %s", path, strerror(errno));
		(void) unlink(tmp);
		return (-1);
	}
	return (0);
}

void
sh_res_free(struct sh_resolution *r)
{
	size_t i;

	for (i = 0; i < r->rs_ntext; i++)
		free(r->rs_text[i]);
	free(r->rs_text);
	for (i = 0; i < r->rs_n; i++)
		free(r->rs_lines[i].rl_path);
	free(r->rs_lines);
	(void) memset(r, 0, sizeof (*r));
}
