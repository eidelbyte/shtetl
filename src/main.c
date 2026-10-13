/*
 * shtetl's command line: one table of commands, each a pair of words
 * ("config show") and the function that takes the words after them.
 * Exit status: 0 done; 1 refused, with every reason on standard
 * error; 2 a usage error.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "core/boot.h"
#include "core/build.h"
#include "core/cascade.h"
#include "core/err.h"
#include "core/root.h"
#include "core/show.h"
#include "core/site.h"
#include "core/spaces.h"
#include "core/zfs.h"

#define	SH_EXIT_OK	0
#define	SH_EXIT_REFUSED	1
#define	SH_EXIT_USAGE	2
#define	SH_EXIT_PAUSED	3	/* a build, at a patch (planning.md 7.1.1) */

/*
 * What every command is handed. sc_root is the configuration root
 * (zconfig, planning.md 4.4): the directory -C named, or, without
 * -C, the config root's mountpoint, found by shtetl:space (4.1) when
 * a command first needs it. ZFS is opened, and the spaces found, once.
 */
struct sh_ctx {
	const char		*sc_root;
	char			*sc_rootbuf;
	struct sh_zfs		*sc_zfs;
	struct sh_spaces	sc_spaces;
	int			sc_found;
};

struct sh_cmd {
	const char	*cm_word;
	const char	*cm_verb;
	const char	*cm_args;	/* the operands, for the usage */
	const char	*cm_what;	/* one line, for the usage */
	int		(*cm_run)(struct sh_ctx *, int, char **);
};

static int cmd_bootstrap(struct sh_ctx *, int, char **);
static int cmd_build(struct sh_ctx *, int, char **);
static int cmd_config_show(struct sh_ctx *, int, char **);
static int cmd_spaces(struct sh_ctx *, int, char **);

/* A command of one word has no verb. */
static const struct sh_cmd sh_cmds[] = {
	{ "config", "show", "[recipe]", "the recipe (foundations/F, "
	    "containers/X, systems/S)\n    with its layers merged, or the "
	    "site's settings", cmd_config_show },
	{ "spaces", NULL, "", "each space's root, and where config and "
	    "build are mounted", cmd_spaces },
	{ "bootstrap", NULL, "[--lake ds] [--run ds] [--config ds]\n"
	    "           [--build ds] [--recipes url] [-y]", "datasets you "
	    "have made the roots of the spaces, and laid\n    out; shown "
	    "first, done once confirmed (-y confirms)", cmd_bootstrap },
	{ "build", NULL, "[-y] [-m msg] [--continue | --skip | --abort]\n"
	    "           foundation", "a foundation built from its recipe "
	    "into zlake; shown\n    first, done once confirmed (-y "
	    "confirms); a build paused at a\n    patch carried on, the fix "
	    "made or the patch left out, or\n    given up", cmd_build },
	{ NULL, NULL, NULL, NULL, NULL }
};

static void
usage(FILE *f)
{
	const struct sh_cmd *c;

	(void) fprintf(f, "usage: shtetl [-C dir] command ...\n");
	for (c = sh_cmds; c->cm_word != NULL; c++) {
		(void) fprintf(f, "       shtetl [-C dir] %s%s%s%s%s\n",
		    c->cm_word, c->cm_verb != NULL ? " " : "",
		    c->cm_verb != NULL ? c->cm_verb : "",
		    c->cm_args[0] != '\0' ? " " : "", c->cm_args);
	}
	(void) fprintf(f, "\n  -C dir  the configuration root (the "
	    "zshtetl-recipes clone);\n          without it, the config "
	    "root's mountpoint\n");
	if (sh_cmds[0].cm_word != NULL)
		(void) fprintf(f, "\n");
	for (c = sh_cmds; c->cm_word != NULL; c++)
		(void) fprintf(f, "  %s%s%s: %s\n", c->cm_word,
		    c->cm_verb != NULL ? " " : "",
		    c->cm_verb != NULL ? c->cm_verb : "", c->cm_what);
	(void) fprintf(f, "\nexit: 0 done, 1 refused (the reasons are on "
	    "standard error), 2 usage,\n      3 a build paused at a patch\n");
}

/*
 * A usage error: WHY, when there is one, then the whole usage. The
 * option errors are ours rather than getopt's, whose messages name
 * argv[0] on one system and its last component on another.
 */
static int
die_usage(const char *why, const char *what)
{
	if (why != NULL)
		(void) fprintf(stderr, "shtetl: %s%s\n", why, what);
	usage(stderr);
	return (SH_EXIT_USAGE);
}

/*
 * Standard output, flushed and checked: what was printed reached it,
 * or the command fails.
 */
static int
out_done(void)
{
	if (fflush(stdout) != 0 || ferror(stdout)) {
		(void) fprintf(stderr, "shtetl: standard output: %s\n",
		    strerror(errno));
		return (SH_EXIT_REFUSED);
	}
	return (SH_EXIT_OK);
}

/* ZFS opened and the spaces found, once. */
static int
ctx_zfs(struct sh_ctx *ctx, struct sh_err *e)
{
	if (ctx->sc_found)
		return (0);
	if (ctx->sc_zfs == NULL && sh_zfs_open(&ctx->sc_zfs, e) != 0)
		return (-1);
	if (sh_spaces_discover(ctx->sc_zfs, &ctx->sc_spaces, e) != 0)
		return (-1);
	ctx->sc_found = 1;
	return (0);
}

/*
 * The configuration root: -C's directory, or the config root where
 * it is mounted. 0 when there is a directory to read.
 */
static int
ctx_root(struct sh_ctx *ctx, struct sh_err *e)
{
	if (ctx->sc_root == NULL) {
		if (ctx_zfs(ctx, e) != 0 ||
		    sh_spaces_config_dir(ctx->sc_zfs, &ctx->sc_spaces,
		    &ctx->sc_rootbuf, e) != 0)
			return (-1);
		ctx->sc_root = ctx->sc_rootbuf;
	}
	if (sh_root_kind(ctx->sc_root, ".") != 'd') {
		sh_errf(e, "%s: not a directory", ctx->sc_root);
		return (-1);
	}
	return (0);
}

static void
ctx_free(struct sh_ctx *ctx)
{
	sh_spaces_free(&ctx->sc_spaces);
	sh_zfs_close(ctx->sc_zfs);
	free(ctx->sc_rootbuf);
}

/*
 * config show [RECIPE]: the merged recipe, or the site's settings,
 * printed only once all of it has been read and accepted.
 */
static int
cmd_config_show(struct sh_ctx *ctx, int argc, char **argv)
{
	struct sh_recipe r;
	struct sh_site s;
	struct sh_err e, w;
	int rc;

	if (argc > 1)
		return (die_usage("config show takes one recipe at most", ""));
	sh_err_init(&e, stderr, "shtetl: ");
	if (ctx_root(ctx, &e) != 0)
		return (SH_EXIT_REFUSED);
	if (argc == 0) {
		if (sh_site_load(ctx->sc_root, &s, &e) != 0)
			return (SH_EXIT_REFUSED);
		sh_show_site(stdout, &s);
		sh_site_free(&s);
		return (out_done());
	}
	sh_err_init(&w, stderr, "shtetl: warning: ");
	rc = sh_recipe_resolve(ctx->sc_root, argv[0], &r, &e, &w);
	if (rc == 0)
		sh_show_recipe(stdout, &r);
	sh_recipe_free(&r);
	return (rc == 0 ? out_done() : SH_EXIT_REFUSED);
}

/*
 * Descriptors 0, 1 and 2 open, on /dev/null where they were not: a
 * pipe shtetl makes for a child must never be one of them, or the
 * child's redirections would clobber it (src/core/run.c).
 */
static void
std_fds(void)
{
	int fd;

	for (fd = 0; fd <= 2; fd++) {
		if (fcntl(fd, F_GETFD) == -1 &&
		    open("/dev/null", fd == 0 ? O_RDONLY : O_WRONLY) != fd)
			exit(SH_EXIT_REFUSED);
	}
}

/* spaces: the roots. */
static int
cmd_spaces(struct sh_ctx *ctx, int argc, char **argv)
{
	struct sh_err e;

	(void) argv;
	if (argc > 0)
		return (die_usage("spaces takes no operand", ""));
	sh_err_init(&e, stderr, "shtetl: ");
	if (ctx_zfs(ctx, &e) != 0)
		return (SH_EXIT_REFUSED);
	if (sh_spaces_show(stdout, ctx->sc_zfs, &ctx->sc_spaces, &e) != 0)
		return (SH_EXIT_REFUSED);
	return (out_done());
}

/*
 * The settings bootstrap uses: from -C's directory, the config root
 * that is there already, or the config root being named (--config),
 * when it is mounted; otherwise the built-in ones. 1 loaded into
 * SITE, 0 none, -1 refused.
 */
static int
boot_site(struct sh_ctx *ctx, const struct sh_bootreq *req,
    struct sh_site *site, struct sh_err *e)
{
	char dir[SH_ZFS_VALMAX], mounted[SH_ZFS_VALMAX];
	size_t i;

	if (ctx->sc_root != NULL || ctx->sc_spaces.ss_root[SH_SP_CONFIG] !=
	    NULL) {
		if (ctx_root(ctx, e) != 0 ||
		    sh_site_load(ctx->sc_root, site, e) != 0)
			return (-1);
		return (1);
	}
	for (i = 0; i < req->br_n; i++) {
		if (req->br_sp[i] != SH_SP_CONFIG)
			continue;
		if (sh_zfs_get(ctx->sc_zfs, req->br_ds[i], "mountpoint", dir,
		    sizeof (dir), NULL, e) != 0 ||
		    sh_zfs_get(ctx->sc_zfs, req->br_ds[i], "mounted", mounted,
		    sizeof (mounted), NULL, e) != 0)
			return (0);
		if (dir[0] != '/' || strcmp(mounted, "yes") != 0 ||
		    sh_root_kind(dir, ".") != 'd')
			return (0);
		return (sh_site_load(dir, site, e) == 0 ? 1 : -1);
	}
	return (0);
}

/*
 * build F: planned, shown, confirmed, run (planning.md 7.1). The
 * recipe is read where the config root is mounted, the directory the
 * build snapshots and records; -C may name only that directory.
 */
static int
cmd_build(struct sh_ctx *ctx, int argc, char **argv)
{
	struct sh_fplan *plan;
	struct sh_site site;
	struct sh_err e, w;
	char *dir, a[PATH_MAX], b[PATH_MAX];
	static const struct {
		const char	*v_name;
		enum sh_bverb	v_verb;
	} verbs[] = {
		{ "--continue", SH_BV_CONTINUE }, { "--skip", SH_BV_SKIP },
		{ "--abort", SH_BV_ABORT }
	};
	const char *msg, *target;
	enum sh_bverb verb;
	size_t j;
	int i, yes, rc, r;

	yes = 0;
	verb = SH_BV_NEW;
	msg = target = NULL;
	for (i = 0; i < argc; i++) {
		for (j = 0; j < sizeof (verbs) / sizeof (verbs[0]); j++) {
			if (strcmp(argv[i], verbs[j].v_name) == 0)
				break;
		}
		if (j < sizeof (verbs) / sizeof (verbs[0])) {
			if (verb != SH_BV_NEW)
				return (die_usage("build: one of --continue, "
				    "--skip and --abort", ""));
			verb = verbs[j].v_verb;
		} else if (strcmp(argv[i], "-y") == 0) {
			yes = 1;
		} else if (strcmp(argv[i], "-m") == 0) {
			if (i + 1 == argc)
				return (die_usage("-m needs a message", ""));
			msg = argv[++i];
		} else if (argv[i][0] == '-') {
			return (die_usage("build: unknown option ", argv[i]));
		} else if (target != NULL) {
			return (die_usage("build takes one foundation", ""));
		} else {
			target = argv[i];
		}
	}
	if (target == NULL)
		return (die_usage("build: no foundation given", ""));
	if (verb == SH_BV_ABORT && msg != NULL)
		return (die_usage("build: -m is not for --abort", ""));
	sh_err_init(&e, stderr, "shtetl: ");
	sh_err_init(&w, stderr, "shtetl: warning: ");
	if (ctx_zfs(ctx, &e) != 0 ||
	    sh_spaces_config_dir(ctx->sc_zfs, &ctx->sc_spaces, &dir, &e) != 0)
		return (SH_EXIT_REFUSED);
	if (ctx->sc_root != NULL && (realpath(ctx->sc_root, a) == NULL ||
	    realpath(dir, b) == NULL || strcmp(a, b) != 0)) {
		sh_errf(&e, "-C %s: not the config root's mountpoint, %s",
		    ctx->sc_root, dir);
		free(dir);
		return (SH_EXIT_REFUSED);
	}
	if (sh_site_load(dir, &site, &e) != 0) {
		free(dir);
		return (SH_EXIT_REFUSED);
	}
	rc = SH_EXIT_REFUSED;
	if ((plan = calloc(1, sizeof (*plan))) == NULL) {
		sh_err_nomem(&e);
	} else if (sh_build_plan(ctx->sc_zfs, &ctx->sc_spaces, site.ss_merged,
	    dir, target, SH_BUILD_DIR, verb, plan, &e, &w) == 0) {
		/* a paused build was confirmed when it began */
		if (verb == SH_BV_NEW) {
			sh_build_show(stdout, plan);
			(void) fflush(stdout);
		}
		if (verb == SH_BV_NEW && !sh_confirm(stdin, stdout, yes,
		    isatty(STDIN_FILENO), "Build as above?")) {
			sh_errf(&e, "%s", isatty(STDIN_FILENO) ?
			    "not confirmed" : "not confirmed: no terminal, use "
			    "-y");
		} else {
			r = sh_build_run(ctx->sc_zfs, plan, &sh_build_tools,
			    msg, time(NULL), stdout, &e);
			if (r >= 0)
				rc = out_done();
			if (r == 1 && rc == SH_EXIT_OK)
				rc = SH_EXIT_PAUSED;
		}
	}
	if (plan != NULL)
		sh_build_free(plan);
	free(plan);
	sh_site_free(&site);
	free(dir);
	return (rc);
}

/*
 * bootstrap: the roots named, planned, shown, confirmed, applied
 * (planning.md 4.2), one to a space.
 */
static int
cmd_bootstrap(struct sh_ctx *ctx, int argc, char **argv)
{
	static const struct {
		const char	*o_name;
		enum sh_space	o_sp;
	} opts[] = {
		{ "--lake", SH_SP_LAKE }, { "--run", SH_SP_RUN },
		{ "--config", SH_SP_CONFIG }, { "--build", SH_SP_BUILD }
	};
	struct sh_bootreq req;
	struct sh_bplan *plan;
	struct sh_site site;
	struct sh_err e;
	size_t j, k;
	int i, yes, have, tty, rc;

	(void) memset(&req, 0, sizeof (req));
	yes = 0;
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-y") == 0 ||
		    strcmp(argv[i], "--yes") == 0) {
			yes = 1;
			continue;
		}
		if (strcmp(argv[i], "--recipes") == 0) {
			if (i + 1 == argc)
				return (die_usage("--recipes needs a URL", ""));
			req.br_recipes = argv[++i];
			continue;
		}
		for (j = 0; j < sizeof (opts) / sizeof (opts[0]); j++) {
			if (strcmp(argv[i], opts[j].o_name) == 0)
				break;
		}
		if (j == sizeof (opts) / sizeof (opts[0]))
			return (die_usage("bootstrap: unknown option ",
			    argv[i]));
		if (i + 1 == argc)
			return (die_usage(argv[i], " needs a dataset"));
		for (k = 0; k < req.br_n; k++) {
			if (req.br_sp[k] == opts[j].o_sp)
				return (die_usage(argv[i], " given twice"));
		}
		req.br_ds[req.br_n] = argv[++i];
		req.br_sp[req.br_n++] = opts[j].o_sp;
	}
	if (req.br_n == 0)
		return (die_usage("bootstrap: no dataset given", ""));
	sh_err_init(&e, stderr, "shtetl: ");
	if (ctx_zfs(ctx, &e) != 0)
		return (SH_EXIT_REFUSED);
	if ((have = boot_site(ctx, &req, &site, &e)) < 0)
		return (SH_EXIT_REFUSED);
	if ((plan = calloc(1, sizeof (*plan))) == NULL) {
		sh_err_nomem(&e);
		rc = SH_EXIT_REFUSED;
	} else if (sh_boot_plan(ctx->sc_zfs, &ctx->sc_spaces,
	    have ? site.ss_merged : NULL, &req, plan, &e) != 0) {
		rc = SH_EXIT_REFUSED;
	} else {
		sh_boot_show(stdout, plan);
		(void) fflush(stdout);
		tty = isatty(STDIN_FILENO);
		if (!sh_confirm(stdin, stdout, yes, tty,
		    "Bootstrap as above?")) {
			sh_errf(&e, "%s", tty ? "not confirmed" :
			    "not confirmed: no terminal, use -y");
			rc = SH_EXIT_REFUSED;
		} else {
			rc = sh_boot_apply(ctx->sc_zfs, plan, stdout, &e) ==
			    0 ? out_done() : SH_EXIT_REFUSED;
		}
	}
	free(plan);
	if (have)
		sh_site_free(&site);
	return (rc);
}

int
main(int argc, char **argv)
{
	struct sh_ctx ctx;
	const struct sh_cmd *c;
	char opt[3];
	int ch;

	std_fds();
	memset(&ctx, 0, sizeof (ctx));
	opterr = 0;
	while ((ch = getopt(argc, argv, "C:h")) != -1) {
		switch (ch) {
		case 'C':
			ctx.sc_root = optarg;
			break;
		case 'h':
			usage(stdout);
			return (SH_EXIT_OK);
		default:
			opt[0] = '-';
			opt[1] = (char)optopt;
			opt[2] = '\0';
			if (optopt == 'C')
				return (die_usage("-C needs a directory", ""));
			return (die_usage("unknown option ", opt));
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 0)
		return (die_usage(NULL, NULL));
	for (c = sh_cmds; c->cm_word != NULL; c++) {
		if (strcmp(argv[0], c->cm_word) != 0)
			continue;
		if (c->cm_verb == NULL)
			ch = c->cm_run(&ctx, argc - 1, argv + 1);
		else if (argc >= 2 && strcmp(argv[1], c->cm_verb) == 0)
			ch = c->cm_run(&ctx, argc - 2, argv + 2);
		else
			continue;
		ctx_free(&ctx);
		return (ch);
	}
	return (die_usage("unknown command: ", argv[0]));
}
