/*
 * The jail's block shtetl writes: see src/core/jail.h.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/build.h"
#include "core/err.h"
#include "core/jail.h"
#include "core/run.h"
#include "core/tree.h"

/* MERGED's jailconf pieces, joined, each line a tab in; "" for none. */
static char *
jl_conf(const struct sh_node *merged)
{
	const struct sh_node *t;
	const char *v, *l, *nl;
	size_t i, len, n;
	char *s;

	t = merged != NULL ? sh_obj_get(merged, "jailconf") : NULL;
	len = 1;
	for (i = 0; t != NULL && i < t->sn_obj.ob_n; i++)
		len += 2 * strlen(t->sn_obj.ob_v[i].pr_val->sn_str) + 2;
	if ((s = calloc(1, len)) == NULL)
		return (NULL);
	n = 0;
	for (i = 0; t != NULL && i < t->sn_obj.ob_n; i++) {
		v = t->sn_obj.ob_v[i].pr_val->sn_str;
		for (l = v; *l != '\0'; l = nl) {
			if ((nl = strchr(l, '\n')) != NULL)
				nl++;
			else
				nl = l + strlen(l);
			if (nl - l == 1 && *l == '\n')
				continue;
			n += (size_t)snprintf(s + n, len - n, "\t%.*s%s",
			    (int)(nl - l), l, nl[-1] == '\n' ? "" : "\n");
		}
	}
	return (s);
}

int
sh_jail_write(const char *file, const char *name, const char *host,
    const char *path, const struct sh_node *merged, struct sh_err *e)
{
	char *conf;
	FILE *f;
	int bad;

	if ((conf = jl_conf(merged)) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	if ((f = fopen(file, "w")) == NULL) {
		sh_errf(e, "%s: %s", file, strerror(errno));
		free(conf);
		return (-1);
	}
	(void) fprintf(f, "# written by shtetl for the container %s; the "
	    "recipe's jailconf\n# is where to change it\n%s {\n"
	    "\tpath = \"%s\";\n\thost.hostname = \"%s\";\n"
	    "\texec.start = \"/bin/sh /etc/rc\";\n"
	    "\texec.stop = \"/bin/sh /etc/rc.shutdown jail\";\n"
	    "\texec.clean;\n\tmount.devfs;\n\tdevfs_ruleset = 4;\n"
	    "\tip4 = inherit;\n\tip6 = inherit;\n%s}\n", name, name, path,
	    host, conf);
	free(conf);
	bad = ferror(f);
	if (fclose(f) != 0 || bad) {
		sh_errf(e, "%s: %s", file, strerror(errno));
		return (-1);
	}
	return (0);
}

/* WORDS (four, and NULL) run, the output to LOG or nowhere; E or none. */
static int
jl_run(const char *const *words, const char *log, struct sh_err *e)
{
	struct sh_err q;
	struct sh_run r;
	char *argv[6];
	size_t i, n;
	FILE *null;
	int fd, rc;

	for (n = 0; words[n] != NULL && n < 5; n++)
		;
	for (i = 0; i < n; i++) {
		if ((argv[i] = strdup(words[i])) == NULL) {
			while (i > 0)
				free(argv[--i]);
			if (e != NULL)
				sh_err_nomem(e);
			return (-1);
		}
	}
	argv[n] = NULL;
	null = NULL;
	if (e == NULL) {
		if ((null = fopen("/dev/null", "w")) == NULL) {
			for (i = 0; i < n; i++)
				free(argv[i]);
			return (-1);
		}
		sh_err_init(&q, null, "");
	}
	fd = log != NULL ? open(log, O_WRONLY | O_CREAT | O_APPEND |
	    O_CLOEXEC, 0644) : -1;
	(void) memset(&r, 0, sizeof (r));
	r.ru_argv = argv;
	r.ru_log = fd;
	rc = sh_run(&r, e != NULL ? e : &q) == 0 ? 0 : -1;
	free(r.ru_out);
	if (fd != -1)
		(void) close(fd);
	if (null != NULL)
		(void) fclose(null);
	for (i = 0; i < n; i++)
		free(argv[i]);
	return (rc);
}

int
sh_jail_running(const struct sh_tools *t, const char *name)
{
	const char *w[5];

	w[0] = t->tl_jls;
	w[1] = "-j";
	w[2] = name;
	w[3] = "jid";
	w[4] = NULL;
	return (jl_run(w, NULL, NULL) == 0);
}

int
sh_jail_do(const struct sh_tools *t, const char *file, const char *name,
    const char *verb, const char *log, struct sh_err *e)
{
	const char *w[6];

	w[0] = t->tl_jail;
	w[1] = "-f";
	w[2] = file;
	w[3] = verb;
	w[4] = name;
	w[5] = NULL;
	return (jl_run(w, log, e));
}
