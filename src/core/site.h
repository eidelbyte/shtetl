/*
 * shtetl.conf (docs/configuration.md 3): what is about this machine, never
 * about a recipe. Its own schema, and two layers: the built-in
 * settings, then the file, merged key by key.
 */

#ifndef	SH_SITE_H
#define	SH_SITE_H

#include "core/err.h"
#include "core/tree.h"

/* The layers' names, as show prints them. */
extern const char sh_site_builtin[];	/* "(built-in)" */
extern const char sh_site_conf[];	/* "shtetl.conf" */

struct sh_site {
	struct sh_node	*ss_merged;	/* the settings in force */
	int		ss_has_conf;	/* shtetl.conf was there */
};

/*
 * Checks SITE, the tree of FILE, against shtetl.conf's keys: every
 * problem reported to E, and their number returned.
 */
unsigned sh_site_check(const struct sh_node *site, const char *file,
    struct sh_err *e);

/*
 * The settings of the configuration root ROOT: the built-in ones,
 * and shtetl.conf over them when it is there. 0, or -1 with the
 * reasons reported to E.
 */
int sh_site_load(const char *root, struct sh_site *s, struct sh_err *e);
void sh_site_free(struct sh_site *s);

/*
 * The recipe repositories remotes.recipes names, each a directory of
 * zconfig: their names, NULL-ended, in the order given. The array is
 * the caller's to free; the names are the site's.
 */
const char **sh_site_repos(const struct sh_site *s);

/* The keep of SECTION, "run" or "lake", in S's settings (plan 8.2). */
int64_t sh_site_keep(const struct sh_site *s, const char *section);

/*
 * roll.depth, the saves a house's roll replays (plan 8.2): a count in
 * *COUNT, the newest that many, 0 every one, with *SINCE 0; or, given
 * as an interval, *COUNT 0 and in *SINCE the moment it reaches back to
 * from NOW, as date(1) -v takes it back on the local calendar, a save
 * taken before it not replayed.
 */
void sh_site_depth(const struct sh_site *s, int64_t now, int64_t *count,
    int64_t *since);

/* The built-in settings alone (the caller frees them), or NULL. */
struct sh_node *sh_site_builtins(struct sh_err *e);

/*
 * Whether KEY, at the top of shtetl.conf, is one of its sections of
 * settings (properties, build, roll, run, lake, remotes) rather than
 * a key of the default recipe (docs/configuration.md 6).
 */
int sh_site_section(const char *key);

#endif	/* SH_SITE_H */
