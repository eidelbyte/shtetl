/*
 * shtetl's command line: one table of commands, each a pair of words
 * ("config show") and the function that takes the words after them.
 * Exit status: 0 done; 1 refused, with every reason on standard
 * error; 2 a usage error.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static const struct sh_cmd sh_cmds[] = {
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

int
main(int argc, char **argv)
{
	struct sh_ctx ctx;
	const struct sh_cmd *c;
	char opt[3];
	int ch;

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
