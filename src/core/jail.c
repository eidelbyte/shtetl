/*
 * A container's jail: see src/core/jail.h.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "core/err.h"
#include "core/jail.h"
#include "core/job.h"
#include "core/tree.h"

int
sh_jail_write(const char *file, const char *name, const char *path,
    const struct sh_node *conf, struct sh_err *e)
{
	const char *l, *nl;
	size_t i;
	FILE *f;
	int bad;

	if ((f = fopen(file, "w")) == NULL) {
		sh_errf(e, "%s: %s", file, strerror(errno));
		return (-1);
	}
	(void) fprintf(f, "# written by shtetl for the container %s; the "
	    "recipe's jailconf\n# is where to change it\n%s {\n"
	    "\tpath = \"%s\";\n\thost.hostname = \"%s\";\n"
	    "\texec.start = \"/bin/sh /etc/rc\";\n"
	    "\texec.stop = \"/bin/sh /etc/rc.shutdown jail\";\n"
	    "\texec.clean;\n\tmount.devfs;\n\tdevfs_ruleset = 4;\n"
	    "\tip4 = inherit;\n\tip6 = inherit;\n", name, name, path, name);
	/* a piece from each layer that gives one, in the layers' order */
	for (i = 0; conf != NULL && i < conf->sn_obj.ob_n; i++) {
		for (l = conf->sn_obj.ob_v[i].pr_val->sn_str; *l != '\0';
		    l = *nl == '\0' ? nl : nl + 1) {
			nl = l + strcspn(l, "\n");
			if (nl > l)
				(void) fprintf(f, "\t%.*s\n", (int)(nl - l), l);
		}
	}
	(void) fputs("}\n", f);
	bad = ferror(f);
	if (fclose(f) != 0 || bad) {
		sh_errf(e, "%s: %s", file, strerror(errno));
		return (-1);
	}
	return (0);
}

void
sh_jail_start(struct sh_job *j, const char *file, const char *name)
{
	const char *argv[] = { "jail", "-f", file, "-cm", name, NULL };

	sh_job_child(j, "jail", argv);
}

void
sh_jail_stop(struct sh_job *j, const char *file, const char *name)
{
	/* jail -r refuses one not running: jls -j asks the kernel first */
	static const char script[] = "jls -j \"$1\" >/dev/null 2>&1 || "
	    "exit 0; exec jail -f \"$2\" -r \"$1\"";
	const char *argv[] = { "sh", "-c", script, "sh", name, file, NULL };

	sh_job_child(j, "jail", argv);
}
