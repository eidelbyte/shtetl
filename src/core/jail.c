/*
 * The jail's block shtetl writes: see src/core/jail.h.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/jail.h"
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
