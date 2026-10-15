/*
 * zfs_rebase as shtetl runs it (planning.md 9, 7.6.1, 7.7): rebases
 * started, continued and aborted through an injected rebaser, the
 * real tool's or a test's, and the resolution document beside each
 * manifest read and answered (zfs-rebase-theory v4-manifest.md 8).
 */

#ifndef	SH_REBASE_H
#define	SH_REBASE_H

#include <stddef.h>

#include "core/err.h"

/* The tool's exits (its README): a start's and a verb's. */
#define	SH_RB_DONE	0	/* done */
#define	SH_RB_GATE	1	/* stopped at the conflicts gate */
#define	SH_RB_REFUSED	2	/* refused, nothing touched */
#define	SH_RB_FAILED	3	/* failed part way, or drift at the end */

/* --continue's flags. */
#define	SH_RB_NOMERGE	0x1	/* --no-merge: hold the gate */
#define	SH_RB_PICK	0x2	/* -i: the picker, on the terminal */
#define	SH_RB_MERGE	0x4	/* -m: clean three-way merges taken */

/* One start. */
struct sh_rbstart {
	const char	*rs_from;	/* ds@snap */
	const char	*rs_onto;	/* ds@snap: clone form; ds: dataset */
	const char	*rs_result;	/* the clone, or the pre-apply's name */
	const char	*rs_base;	/* --allow-unrelated --base, or NULL */
	const char	*rs_manifest;	/* -o: a path that is not there */
	int		rs_merge;	/* -m, --merge-non-conflict */
};

/*
 * A rebaser. Each call is the tool's exit, SH_RB_DONE to SH_RB_FAILED,
 * or -1 when it could not be run at all, said in E; the tool's own
 * output goes to the file LOG (or nowhere: -1), but the picker's, which
 * has the terminal. A verb names its rebase by IDENT: the result
 * dataset, which carries the record in both forms.
 */
struct sh_rebaser {
	int	(*rb_start)(const struct sh_rebaser *self,
		    const struct sh_rbstart *s, int log, struct sh_err *e);
	int	(*rb_continue)(const struct sh_rebaser *self,
		    const char *ident, unsigned flags, int log,
		    struct sh_err *e);
	int	(*rb_abort)(const struct sh_rebaser *self, const char *ident,
		    int log, struct sh_err *e);
	const char	*rb_tool;	/* the command: zfs_rebase */
	void		*rb_data;	/* a test's */
};

/* zfs_rebase, found on SH_RUN_PATH (/usr/local/sbin, where it installs). */
extern const struct sh_rebaser sh_zfs_rebaser;

/* What one exit says, for a message: "stopped at conflicts". */
const char *sh_rb_said(int rc);

/*
 * Where the tool keeps a rebase of RESULT while it is open, its private
 * mount, into BUF: /var/db/zfs_rebase/<RESULT as a path>/mnt.
 */
void sh_rb_privmnt(const char *result, char *buf, size_t len);

/* ---- the resolution ------------------------------------------------ */

/* One line of the tree: a conflict or a drift, answered or not. */
struct sh_resline {
	char		*rl_path;	/* the tree's, decoded: /etc/pwd.db */
	int		rl_conflict;	/* a conflict line; else a drift */
	unsigned	rl_group;	/* its conflict group */
	char		rl_choice[8];	/* -, keep, onto or from */
	size_t		rl_line;	/* its line, in rs_lines */
};

struct sh_resolution {
	char		**rs_text;	/* every line as read, no newline */
	size_t		rs_ntext;
	struct sh_resline *rs_lines;	/* the name lines with a choice */
	size_t		rs_n;
	unsigned	rs_names;	/* the header's #names */
	size_t		rs_unline;	/* the #unanswered line, in rs_text */
};

/*
 * The resolution at PATH read into R: the header's #names and
 * #unanswered, every conflict and drift line with its full path and
 * choice. Refused: no such file, a line out of the grammar, a choice
 * that is none, #unanswered not the lines'. sh_res_free cleans up R
 * either way.
 */
int sh_res_read(const char *path, struct sh_resolution *r,
    struct sh_err *e);

/* The lines of R still unanswered, "-". */
unsigned sh_res_unanswered(const struct sh_resolution *r);

/* The line of R for PATH (/etc/pwd.db), or NULL. */
const struct sh_resline *sh_res_find(const struct sh_resolution *r,
    const char *path);

/*
 * Each line of R whose path is one of PATHS (N of them, as the tree
 * spells them: /etc/pwd.db) and is still "-" answered keep, and
 * #unanswered made the count; how many were answered.
 */
size_t sh_res_keep(struct sh_resolution *r, const char *const *paths,
    size_t n);

/*
 * R written to PATH whole: to PATH.shtetl beside it, then renamed over
 * it, so the tool reads the old document or the new one, never half.
 */
int sh_res_write(const char *path, const struct sh_resolution *r,
    struct sh_err *e);

void sh_res_free(struct sh_resolution *r);

#endif	/* SH_REBASE_H */
