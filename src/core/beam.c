/*
 * shtetl beam F (planning.md 7.3): a foundation's version, sent from
 * zlake to the run root's foundations/F, whole the first time and
 * from zrun's newest after, never with -p: the version's shtetl:* are
 * copied onto each received snapshot (6.2).
 *
 * Decided here, beyond the plan (PROPOSAL): the command, shown and
 * confirmed as a build is; with no version given, F's newest snapshot
 * that is a version or a -pkg of one; zrun takes versions forward
 * only, one on zrun already or older than zrun's newest refused; a
 * failure destroys what the beam received, so zrun is as it was.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/beam.h"
#include "core/build.h"
#include "core/err.h"
#include "core/spaces.h"
#include "core/zfs.h"

/* A list of names, NULL-ended, grown by one at a time. */
static int
bm_add(void *arg, const char *name)
{
	char ***list = arg;
	char **nl;
	size_t n;

	for (n = 0; (*list)[n] != NULL; n++)
		;
	if ((nl = realloc(*list, (n + 2) * sizeof (*nl))) == NULL)
		return (-1);
	*list = nl;
	if ((nl[n] = strdup(name)) == NULL)
		return (-1);
	nl[n + 1] = NULL;
	return (0);
}

static void
bm_free(char **list)
{
	size_t i;

	for (i = 0; list != NULL && list[i] != NULL; i++)
		free(list[i]);
	free(list);
}

static char **
bm_new(struct sh_err *e)
{
	char **list;

	if ((list = calloc(1, sizeof (*list))) == NULL)
		sh_err_nomem(e);
	return (list);
}

/* DS's snapshots, the names after the @, oldest first; NULL: refused. */
static char **
bm_snaps(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char **list;

	if ((list = bm_new(e)) == NULL)
		return (NULL);
	if (sh_zfs_snapshots(z, ds, bm_add, &list, e) != 0) {
		bm_free(list);
		return (NULL);
	}
	return (list);
}

/* Every filesystem below DS, parents first, by full name. */
static int
bm_walk(struct sh_zfs *z, const char *ds, char ***all, struct sh_err *e)
{
	char **kids;
	size_t i;
	int r;

	if ((kids = bm_new(e)) == NULL)
		return (-1);
	r = sh_zfs_children(z, ds, bm_add, &kids, e);
	for (i = 0; r == 0 && kids[i] != NULL; i++) {
		if (bm_add(all, kids[i]) != 0) {
			sh_err_nomem(e);
			r = -1;
		} else {
			r = bm_walk(z, kids[i], all, e);
		}
	}
	bm_free(kids);
	return (r);
}

/* The index of NAME in LIST, or -1. */
static long
bm_index(char *const *list, const char *name)
{
	long i;

	for (i = 0; list[i] != NULL; i++) {
		if (strcmp(list[i], name) == 0)
			return (i);
	}
	return (-1);
}

/* DS@SNAP's shtetl:kind into KIND; "-" when it has none. */
static int
bm_kind(struct sh_zfs *z, const char *ds, const char *snap, char *kind,
    size_t len, struct sh_err *e)
{
	char name[SH_BEAM_NAME * 2];

	(void) snprintf(name, sizeof (name), "%s@%s", ds, snap);
	return (sh_zfs_get(z, name, "shtetl:kind", kind, len, NULL, e));
}

int
sh_beam_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const char *target, const char *version, struct sh_beam *p,
    struct sh_err *e)
{
	char val[SH_ZFS_VALMAX], **lake, **vers, **zrun, **all, **rall;
	unsigned before;
	size_t i, n;
	long iv, ifrom;
	int r;

	(void) memset(p, 0, sizeof (*p));
	before = e->se_count;
	if (sh_build_target(target, p->bm_name, sizeof (p->bm_name), e) != 0)
		return (-1);
	if (s->ss_root[SH_SP_LAKE] == NULL)
		sh_errf(e, "no lake root");
	if (s->ss_root[SH_SP_RUN] == NULL)
		sh_errf(e, "no run root");
	if (e->se_count != before)
		return (-1);
	(void) snprintf(p->bm_lake, sizeof (p->bm_lake), "%s/foundations/%s",
	    s->ss_root[SH_SP_LAKE], p->bm_name);
	(void) snprintf(p->bm_run, sizeof (p->bm_run), "%s/foundations/%s",
	    s->ss_root[SH_SP_RUN], p->bm_name);
	if ((r = sh_zfs_get(z, p->bm_lake, "type", val, sizeof (val), NULL,
	    e)) != 0) {
		if (r == 1)
			sh_errf(e, "no foundation %s", p->bm_name);
		return (-1);
	}
	lake = vers = zrun = all = rall = NULL;
	if ((lake = bm_snaps(z, p->bm_lake, e)) == NULL ||
	    (vers = bm_new(e)) == NULL)
		goto out;
	/* its versions, and -pkg of them */
	for (i = 0; lake[i] != NULL; i++) {
		if (bm_kind(z, p->bm_lake, lake[i], val, sizeof (val), e) != 0)
			goto out;
		if ((strcmp(val, "version") == 0 || strcmp(val, "pkg") == 0) &&
		    bm_add(&vers, lake[i]) != 0) {
			sh_err_nomem(e);
			goto out;
		}
	}
	for (n = 0; vers[n] != NULL; n++)
		;
	if (n == 0) {
		sh_errf(e, "foundation %s exists without a version",
		    p->bm_name);
		goto out;
	}
	if (version != NULL && bm_index(vers, version) < 0) {
		sh_errf(e, "foundation %s has no version %s", p->bm_name,
		    version);
		goto out;
	}
	(void) snprintf(p->bm_version, sizeof (p->bm_version), "%s",
	    version != NULL ? version : vers[n - 1]);

	/* its datasets, parents first; a placeholder by its canmount */
	if ((all = bm_new(e)) == NULL || bm_add(&all, p->bm_lake) != 0 ||
	    bm_walk(z, p->bm_lake, &all, e) != 0)
		goto out;
	for (i = 0; all[i] != NULL; i++) {
		if (i == SH_BEAM_DS) {
			sh_errf(e, "foundation %s: more than %d datasets",
			    p->bm_name, SH_BEAM_DS);
			goto out;
		}
		(void) snprintf(p->bm_rel[i], SH_BEAM_NAME, "%s", i == 0 ? "" :
		    all[i] + strlen(p->bm_lake) + 1);
		if (sh_zfs_get(z, all[i], "canmount", val, sizeof (val), NULL,
		    e) != 0)
			goto out;
		p->bm_placeholder[i] = strcmp(val, "off") == 0;
	}
	p->bm_n = i;

	/* zrun's: forward only, the same datasets */
	if ((r = sh_zfs_get(z, p->bm_run, "type", val, sizeof (val), NULL,
	    e)) < 0)
		goto out;
	if (r == 1)
		goto out;
	if ((zrun = bm_snaps(z, p->bm_run, e)) == NULL)
		goto out;
	for (n = 0; zrun[n] != NULL; n++)
		;
	if (n == 0) {
		sh_errf(e, "%s exists without a version", p->bm_run);
		goto out;
	}
	(void) snprintf(p->bm_from, sizeof (p->bm_from), "%s", zrun[n - 1]);
	iv = bm_index(lake, p->bm_version);
	ifrom = bm_index(lake, p->bm_from);
	if (bm_index(zrun, p->bm_version) >= 0)
		sh_errf(e, "%s@%s is on zrun already", p->bm_name,
		    p->bm_version);
	else if (ifrom < 0)
		sh_errf(e, "zrun's newest, %s@%s, is not in zlake",
		    p->bm_name, p->bm_from);
	else if (ifrom > iv)
		sh_errf(e, "zrun has %s@%s, newer than %s", p->bm_name,
		    p->bm_from, p->bm_version);
	if ((rall = bm_new(e)) == NULL || bm_add(&rall, p->bm_run) != 0 ||
	    bm_walk(z, p->bm_run, &rall, e) != 0)
		goto out;
	for (n = 0; rall[n] != NULL; n++)
		;
	for (i = 1; i < p->bm_n && n == p->bm_n; i++) {
		(void) snprintf(val, sizeof (val), "%s/%s", p->bm_run,
		    p->bm_rel[i]);
		if (bm_index(rall, val) < 0)
			break;
	}
	if (n != p->bm_n || i != p->bm_n)
		sh_errf(e, "foundation %s: its datasets on zrun differ",
		    p->bm_name);
out:
	bm_free(lake);
	bm_free(vers);
	bm_free(zrun);
	bm_free(all);
	bm_free(rall);
	return (e->se_count == before ? 0 : -1);
}

void
sh_beam_show(FILE *out, const struct sh_beam *p)
{
	size_t i;

	(void) fprintf(out, "foundation %s, @%s to zrun, ", p->bm_name,
	    p->bm_version);
	if (p->bm_from[0] != '\0')
		(void) fprintf(out, "from @%s\n", p->bm_from);
	else
		(void) fprintf(out, "whole\n");
	for (i = 0; i < p->bm_n; i++)
		(void) fprintf(out, "  %s%s%s -> %s%s%s%s\n", p->bm_lake,
		    i > 0 ? "/" : "", p->bm_rel[i], p->bm_run,
		    i > 0 ? "/" : "", p->bm_rel[i], p->bm_placeholder[i] ?
		    "  (placeholder)" : "");
	if (p->bm_from[0] == '\0')
		(void) fprintf(out, "  readonly=on, canmount=noauto; a "
		    "placeholder's canmount=off\n");
}

/* The full name of P's dataset I on SIDE (bm_lake or bm_run). */
static void
bm_ds(const struct sh_beam *p, const char *side, size_t i, char *buf,
    size_t len)
{
	(void) snprintf(buf, len, "%s%s%s", side, i > 0 ? "/" : "",
	    p->bm_rel[i]);
}

/* A copy of shtetl:* onto a received snapshot. */
struct bm_copy {
	struct sh_zfs	*bc_z;
	const char	*bc_to;
	struct sh_err	*bc_err;
};

static int
bm_copy1(void *arg, const char *prop, const char *value)
{
	struct bm_copy *c = arg;

	if (strncmp(prop, "shtetl:", 7) != 0)
		return (0);
	return (sh_zfs_set(c->bc_z, c->bc_to, prop, value, c->bc_err) != 0 ?
	    -1 : 0);
}

int
sh_shtetl_copy(struct sh_zfs *z, const char *from, const char *to,
    struct sh_err *e)
{
	struct bm_copy c;

	c.bc_z = z;
	c.bc_to = to;
	c.bc_err = e;
	return (sh_zfs_userprops(z, from, bm_copy1, &c, e) != 0 ? -1 : 0);
}

/*
 * A zrun snapshot of dataset I with no shtetl:kind, a copy cut short:
 * its zlake counterpart's copied onto it again.
 */
static int
bm_mend(struct sh_zfs *z, const struct sh_beam *p, size_t i, FILE *out,
    struct sh_err *e)
{
	char lds[SH_BEAM_NAME * 2], rds[SH_BEAM_NAME * 2];
	char from[SH_BEAM_NAME * 3], to[SH_BEAM_NAME * 3];
	char kind[SH_ZFS_VALMAX], **snaps;
	size_t j;
	int r;

	bm_ds(p, p->bm_lake, i, lds, sizeof (lds));
	bm_ds(p, p->bm_run, i, rds, sizeof (rds));
	if ((snaps = bm_snaps(z, rds, e)) == NULL)
		return (-1);
	r = 0;
	for (j = 0; r == 0 && snaps[j] != NULL; j++) {
		if ((r = bm_kind(z, rds, snaps[j], kind, sizeof (kind),
		    e)) != 0 || strcmp(kind, "-") != 0)
			continue;
		(void) snprintf(from, sizeof (from), "%s@%s", lds, snaps[j]);
		(void) snprintf(to, sizeof (to), "%s@%s", rds, snaps[j]);
		if (sh_zfs_get(z, from, "type", kind, sizeof (kind), NULL,
		    e) != 0)
			continue;
		if ((r = sh_shtetl_copy(z, from, to, e)) == 0)
			(void) fprintf(out, "copied shtetl:* onto %s again\n",
			    to);
	}
	bm_free(snaps);
	return (r);
}

/* DS, its snapshots and everything below it destroyed, deepest first. */
static int
bm_destroyf(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char **kids, **snaps, name[SH_BEAM_NAME * 3];
	size_t i;
	int r;

	if ((kids = bm_new(e)) == NULL)
		return (-1);
	r = sh_zfs_children(z, ds, bm_add, &kids, e);
	for (i = 0; r == 0 && kids[i] != NULL; i++)
		r = bm_destroyf(z, kids[i], e);
	bm_free(kids);
	if (r != 0 || (snaps = bm_snaps(z, ds, e)) == NULL)
		return (-1);
	for (i = 0; r == 0 && snaps[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", ds, snaps[i]);
		r = sh_zfs_destroy(z, name, e);
	}
	bm_free(snaps);
	return (r != 0 ? -1 : sh_zfs_destroy(z, ds, e));
}

int
sh_beam_run(struct sh_zfs *z, const struct sh_beam *p, FILE *out,
    struct sh_err *e)
{
	char lds[SH_BEAM_NAME * 2], rds[SH_BEAM_NAME * 2];
	char snap[SH_BEAM_NAME * 3], from[SH_BEAM_NAME * 3];
	char to[SH_BEAM_NAME * 3];
	size_t i, done;
	int first;

	first = p->bm_from[0] == '\0';
	for (i = 0; !first && i < p->bm_n; i++) {
		if (bm_mend(z, p, i, out, e) != 0)
			return (-1);
	}
	done = 0;
	for (i = 0; i < p->bm_n; i++) {
		bm_ds(p, p->bm_lake, i, lds, sizeof (lds));
		bm_ds(p, p->bm_run, i, rds, sizeof (rds));
		(void) snprintf(snap, sizeof (snap), "%s@%s", lds,
		    p->bm_version);
		(void) snprintf(from, sizeof (from), "%s@%s", lds, p->bm_from);
		(void) snprintf(to, sizeof (to), "%s@%s", rds, p->bm_version);
		if (sh_zfs_send(z, snap, first ? NULL : from, to, NULL,
		    e) != 0)
			goto fail;
		done = i + 1;
		if (first && ((i == 0 && sh_zfs_set(z, rds, "readonly", "on",
		    e) != 0) || sh_zfs_set(z, rds, "canmount",
		    p->bm_placeholder[i] ? "off" : "noauto", e) != 0))
			goto fail;
		if (sh_shtetl_copy(z, snap, to, e) != 0)
			goto fail;
		(void) fprintf(out, "received %s, %s\n", to, first ? "whole" :
		    "from its last");
	}
	(void) fprintf(out, "beamed %s@%s to %s\n", p->bm_name, p->bm_version,
	    p->bm_run);
	return (0);
fail:
	/* what this beam received gone: zrun as it was */
	if (first && done > 0) {
		if (bm_destroyf(z, p->bm_run, e) == 0)
			(void) fprintf(out, "destroyed %s\n", p->bm_run);
	} else {
		while (done > 0) {
			bm_ds(p, p->bm_run, --done, rds, sizeof (rds));
			(void) snprintf(to, sizeof (to), "%s@%s", rds,
			    p->bm_version);
			if (sh_zfs_destroy(z, to, e) == 0)
				(void) fprintf(out, "destroyed %s\n", to);
		}
	}
	sh_errf(e, "beam of %s@%s failed", p->bm_name, p->bm_version);
	return (-1);
}
