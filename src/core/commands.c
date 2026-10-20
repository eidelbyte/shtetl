/*
 * A foundation's packages and commands: the children, and the calls
 * that lend the head the host's resolv.conf and take it back.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/cascade.h"
#include "core/commands.h"
#include "core/err.h"
#include "core/job.h"
#include "core/mem.h"
#include "core/root.h"
#include "core/tree.h"

/* A, SEP and B, allocated. */
static char *
cm_join(const char *a, const char *sep, const char *b)
{
	size_t n = strlen(a) + strlen(sep) + strlen(b) + 1;
	char *s = sh_calloc(1, n);

	(void) snprintf(s, n, "%s%s%s", a, sep, b);
	return (s);
}

/* "A: B": a step's name. */
static char *
cm_step(const char *a, const char *b)
{
	return (cm_join(a, ": ", b));
}

/* "NAME=VALUE" with NAME= given: an environment's word. */
static char *
cm_cat(const char *a, const char *b)
{
	return (cm_join(a, "", b));
}

/*
 * Each piece of each label of KEY added to J as a child running it,
 * WORDS before the text: the step "WHAT: LABEL".
 */
static void
cm_pieces(struct sh_job *j, const struct sh_commands *cm, const char *key,
    const char *what, const char *const *words, size_t nw)
{
	const struct sh_node *cmds, *label;
	const char *argv[16];
	char *step;
	size_t i, k;

	cmds = sh_obj_get(cm->cm_recipe->sr_merged, key);
	(void) memcpy(argv, words, nw * sizeof (*argv));
	argv[nw + 1] = NULL;
	for (i = 0; cmds != NULL && i < cmds->sn_obj.ob_n; i++) {
		label = cmds->sn_obj.ob_v[i].pr_val;
		step = cm_step(what, cmds->sn_obj.ob_v[i].pr_key);
		for (k = 0; k < label->sn_obj.ob_n; k++) {
			argv[nw] = label->sn_obj.ob_v[k].pr_val->sn_str;
			sh_job_child(j, step, argv);
		}
		free(step);
	}
}

/* The host's resolv.conf lent to a head that has none, for pkg. */
static int
cm_lend(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_commands *cm = arg;
	char buf[8192], *to;
	ssize_t n;
	int in, out, r = 0;

	(void) j;
	to = sh_root_path(cm->cm_head, "etc/resolv.conf");
	if (access(to, F_OK) == 0 ||
	    (in = open(cm->cm_resolv, O_RDONLY | O_CLOEXEC)) < 0) {
		free(to);
		return (0);
	}
	if ((out = open(to, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
	    0644)) < 0) {
		sh_errf(e, "%s: %s", to, strerror(errno));
		(void) close(in);
		free(to);
		return (-1);
	}
	cm->cm_lent = 1;
	while (r == 0 && (n = read(in, buf, sizeof (buf))) > 0)
		r = write(out, buf, (size_t)n) == n ? 0 : -1;
	if (n < 0 || r != 0 || close(out) != 0) {
		sh_errf(e, "%s: %s", to, strerror(errno));
		r = -1;
	}
	(void) close(in);
	free(to);
	return (r);
}

/* The resolv.conf lent taken back. */
static int
cm_return(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_commands *cm = arg;
	char *to;
	int r = 0;

	(void) j;
	if (!cm->cm_lent)
		return (0);
	to = sh_root_path(cm->cm_head, "etc/resolv.conf");
	if (unlink(to) != 0) {
		sh_errf(e, "%s: %s", to, strerror(errno));
		r = -1;
	}
	cm->cm_lent = 0;
	free(to);
	return (r);
}

void
sh_commands_steps(struct sh_job *j, struct sh_commands *cm)
{
	const struct sh_node *pkg = NULL, *cmds;
	const char **argv, *words[8];
	char *root, *name, *dev;
	size_t i, n = 0;

	if (!cm->cm_upgrade) {
		root = cm_cat("SHTETL_ROOT=", cm->cm_head);
		name = cm_cat("SHTETL_NAME=", cm->cm_name);
		words[0] = "env";
		words[1] = root;
		words[2] = name;
		words[3] = "SHTETL_KIND=foundation";
		words[4] = "sh";
		words[5] = "-e";
		words[6] = "-c";
		cm_pieces(j, cm, "commands-external", "external", words, 7);
		free(root);
		free(name);
		pkg = sh_obj_get(cm->cm_recipe->sr_merged, "pkg");
		cmds = sh_obj_get(cm->cm_recipe->sr_merged, "commands");
		n = pkg != NULL ? pkg->sn_obj.ob_n : 0;
		if (n == 0 && (cmds == NULL || cmds->sn_obj.ob_n == 0))
			return;
	}
	dev = sh_root_path(cm->cm_head, "dev");
	words[0] = "mount";
	words[1] = "-t";
	words[2] = "devfs";
	words[3] = "devfs";
	words[4] = dev;
	words[5] = NULL;
	sh_job_child(j, "devfs", words);
	if (cm->cm_upgrade || n > 0) {
		sh_job_call(j, "resolv.conf", cm_lend, cm);
		argv = sh_calloc(n + 9, sizeof (*argv));
		argv[0] = "env";
		argv[1] = "ASSUME_ALWAYS_YES=yes";
		argv[2] = "pkg";
		argv[3] = "-c";
		argv[4] = cm->cm_head;
		argv[5] = cm->cm_upgrade ? "upgrade" : "install";
		argv[6] = "-y";
		argv[7] = cm->cm_upgrade ? NULL : "--";
		for (i = 0; i < n; i++)
			argv[8 + i] = pkg->sn_obj.ob_v[i].pr_key;
		sh_job_child(j, "pkg", argv);
		free(argv);
	}
	if (!cm->cm_upgrade) {
		words[0] = "chroot";
		words[1] = cm->cm_head;
		words[2] = "/bin/sh";
		words[3] = "-e";
		words[4] = "-c";
		cm_pieces(j, cm, "commands", "commands", words, 5);
	}
	words[0] = "umount";
	words[1] = dev;
	words[2] = NULL;
	sh_job_child(j, "devfs", words);
	sh_job_call(j, "resolv.conf", cm_return, cm);
	free(dev);
}
