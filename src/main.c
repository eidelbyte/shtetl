/*
 * shtetl's command line: one table of commands, each a pair of words
 * ("config show") and the function that takes the words after them.
 * Exit status: 0 done; 1 refused, with every reason on standard
 * error; 2 a usage error.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/cascade.h"
#include "core/err.h"
#include "core/root.h"
#include "core/show.h"
#include "core/site.h"

#define	SH_EXIT_OK	0
#define	SH_EXIT_REFUSED	1
#define	SH_EXIT_USAGE	2

/*
 * What every command is handed. sc_root is the configuration root
 * (zconfig, planning.md 4.4) that -C named, or NULL: until discovery
 * by shtetl:space (4.1) is built, -C is the only way to name it.
 */
struct sh_ctx {
	const char	*sc_root;
};

struct sh_cmd {
	const char	*cm_word;
	const char	*cm_verb;
	const char	*cm_args;	/* the operands, for the usage */
	const char	*cm_what;	/* one line, for the usage */
	int		(*cm_run)(struct sh_ctx *, int, char **);
};

static int cmd_config_show(struct sh_ctx *, int, char **);

static const struct sh_cmd sh_cmds[] = {
	{ "config", "show", "[recipe]", "the recipe (foundations/F, "
	    "containers/X, systems/S)\n    with its layers merged, or the "
	    "site's settings", cmd_config_show },
	{ NULL, NULL, NULL, NULL, NULL }
};

static void
usage(FILE *f)
{
	const struct sh_cmd *c;

	(void) fprintf(f, "usage: shtetl [-C dir] command ...\n");
	for (c = sh_cmds; c->cm_word != NULL; c++) {
		(void) fprintf(f, "       shtetl [-C dir] %s %s%s%s\n",
		    c->cm_word, c->cm_verb, c->cm_args[0] != '\0' ? " " : "",
		    c->cm_args);
	}
	(void) fprintf(f, "\n  -C dir  the configuration root: the "
	    "zshtetl-recipes clone\n");
	if (sh_cmds[0].cm_word != NULL)
		(void) fprintf(f, "\n");
	for (c = sh_cmds; c->cm_word != NULL; c++)
		(void) fprintf(f, "  %s %s: %s\n", c->cm_word, c->cm_verb,
		    c->cm_what);
	(void) fprintf(f, "\nexit: 0 done, 1 refused (the reasons are on "
	    "standard error), 2 usage\n");
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

/* The configuration root, or a refusal: 0 if there is one to read. */
static int
need_root(const struct sh_ctx *ctx, struct sh_err *e)
{
	if (sh_root_kind(ctx->sc_root, ".") != 'd') {
		sh_errf(e, "%s: not a directory", ctx->sc_root);
		return (-1);
	}
	return (0);
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
	struct sh_err e;
	int rc;

	if (argc > 1)
		return (die_usage("config show takes one recipe at most", ""));
	if (ctx->sc_root == NULL)
		return (die_usage("no configuration root: give -C dir", ""));
	sh_err_init(&e, stderr, "shtetl: ");
	if (need_root(ctx, &e) != 0)
		return (SH_EXIT_REFUSED);
	if (argc == 0) {
		if (sh_site_load(ctx->sc_root, &s, &e) != 0)
			return (SH_EXIT_REFUSED);
		sh_show_site(stdout, &s);
		sh_site_free(&s);
		return (out_done());
	}
	rc = sh_recipe_resolve(ctx->sc_root, argv[0], &r, &e);
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
		if (strcmp(argv[0], c->cm_word) == 0 && argc >= 2 &&
		    strcmp(argv[1], c->cm_verb) == 0)
			return (c->cm_run(&ctx, argc - 2, argv + 2));
	}
	return (die_usage("unknown command: ", argv[0]));
}
