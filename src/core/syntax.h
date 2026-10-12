/*
 * The spellings shtetl takes for the names and paths in its
 * configuration. Each check returns NULL when S will do, or why not,
 * as a phrase a message puts after what S should have been ("not a
 * dataset path: an empty component").
 */

#ifndef	SH_SYNTAX_H
#define	SH_SYNTAX_H

/*
 * A recipe's name (the F of foundations/F.conf, a container, a
 * system), a name in requires, update.suffix: one dataset component
 * that is also a jail name and a file name. Letters, digits, '-' and
 * '_', beginning with a letter or a digit.
 */
const char *sh_syn_name(const char *s);

/*
 * A dataset path in a recipe's datasets, below the recipe's dataset:
 * usr/local. Components of the characters ZFS names take
 * (zfs_namecheck.c, valid_char), none empty, none "." or "..".
 */
const char *sh_syn_dataset(const char *s);

/* A dataset's whole name, its pool first: tank/shtetl. */
const char *sh_syn_dsname(const char *s);

/* A package: a name or an origin (www/nginx), pkg's characters. */
const char *sh_syn_pkg(const char *s);

/* The label of a commands block. */
const char *sh_syn_label(const char *s);

/*
 * A path in the configuration root: an import, a _file, a derived
 * file, a patch directory. Relative, no empty, "." or ".." component.
 */
const char *sh_syn_relpath(const char *s);

/*
 * A ZFS property the configuration may set: a native name
 * (userquota@x included) or a user property (it has a ':'). Not
 * mountpoint, canmount or shtetl:*, which are shtetl's own. Whether
 * a native name is one this ZFS knows is libzfs's to say, on the box.
 */
const char *sh_syn_prop(const char *s);

/* A FreeBSD branch, as src_branch names it. */
enum sh_btype {
	SH_BR_MAIN,		/* main */
	SH_BR_STABLE,		/* stable/N */
	SH_BR_RELENG		/* releng/N.M */
};

struct sh_branch {
	enum sh_btype	br_type;
	long		br_major;	/* stable, releng; main: -1 */
	long		br_minor;	/* releng; -1 otherwise */
};

/* S read as a branch into B: main, stable/N or releng/N.M. */
const char *sh_syn_branch(const char *s, struct sh_branch *b);

/* A version constraint: >=15, =15.1, <16, <=15.1, >14. */
const char *sh_syn_ver(const char *s);

/*
 * The versions constraint S (which sh_syn_ver took) allows, as a range
 * of major * SH_VER_MINORS + minor, both ends in it. A version written
 * N is every N.x: =15 is 15.0 to the last 15.x, <=15 takes 15.x, >15
 * begins at 16.0, >=15 at 15.0, and <15 ends before 15.0. Every
 * constraint of a recipe must hold together, so their ranges must
 * meet.
 */
#define	SH_VER_MINORS	100000L
#define	SH_VER_LOW	(-1L)
#define	SH_VER_HIGH	(1000L * SH_VER_MINORS)

void sh_ver_range(const char *s, long *lo, long *hi);

/*
 * A date(1) -v interval (planning.md 6.3): a count above 0 and one
 * of y, m, w, d, H, M, S.
 */
const char *sh_syn_interval(const char *s);

/*
 * Where patches come from (planning.md 7.1.1): a URL with a scheme;
 * a host and path, which is https (its first component has a '.');
 * or a directory in the configuration root.
 */
enum sh_loc {
	SH_LOC_URL,
	SH_LOC_HOST,
	SH_LOC_DIR
};

const char *sh_syn_location(const char *s, enum sh_loc *kind);

#endif	/* SH_SYNTAX_H */
