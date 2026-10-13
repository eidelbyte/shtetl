/*
 * shtetl save X (planning.md 7.5, 6.2, 7.10).
 *
 * Decided here, beyond the plan (PROPOSAL): a save's shtetl:config is
 * the config snapshot the container was made with, carried from its
 * newest save, or given by create for the first; a save that is taken
 * stays when its mirror fails, and the next save's mirror brings each
 * dataset up to it from that dataset's own newest mirrored save.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/beam.h"
#include "core/err.h"
#include "core/save.h"
#include "core/spaces.h"
#include "core/syntax.h"
#include "core/zfs.h"
#include "core/zwalk.h"

int
sh_save_target(const char *target, char *name, size_t len,
    struct sh_err *e)
{
	const char *n, *why;

	n = target;
	if (strchr(target, '/') != NULL) {
		if (strncmp(target, "containers/", 11) != 0) {
			sh_errf(e, "%s: invalid container: use X or "
			    "containers/X", target);
			return (-1);
		}
		n = target + 11;
	}
	(void) snprintf(name, len, "%s", n);
	if ((why = sh_syn_name(name)) != NULL) {
		sh_errf(e, "%s: invalid container name: %s", target, why);
		return (-1);
	}
	return (0);
}

/* X's newest save's shtetl:config and its guid into P; 1: none. */
static int
sv_lastconfig(struct sh_zfs *z, struct sh_save *p, struct sh_err *e)
{
	char name[SH_SAVE_NAME * 2], kind[SH_ZFS_VALMAX], **snaps;
	size_t n;
	int r;

	if ((snaps = sh_zw_snaps(z, p->sv_run, e)) == NULL)
		return (-1);
	for (n = 0; snaps[n] != NULL; n++)
		;
	r = 1;
	while (r == 1 && n > 0) {
		(void) snprintf(name, sizeof (name), "%s@%s", p->sv_run,
		    snaps[--n]);
		if (sh_zfs_get(z, name, "shtetl:kind", kind, sizeof (kind),
		    NULL, e) != 0) {
			r = -1;
			break;
		}
		if (strcmp(kind, "save") != 0)
			continue;
		r = sh_zfs_get(z, name, "shtetl:config", p->sv_config,
		    sizeof (p->sv_config), NULL, e) != 0 ||
		    sh_zfs_get(z, name, "shtetl:config-guid", p->sv_guid,
		    sizeof (p->sv_guid), NULL, e) != 0 ? -1 : 0;
	}
	sh_zw_free(snaps);
	return (r);
}

int
sh_save_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const char *target, const char *config, struct sh_save *p,
    struct sh_err *e)
{
	char val[SH_ZFS_VALMAX], prefix[SH_SAVE_NAME + 2], **all;
	unsigned before;
	size_t i;
	int r;

	(void) memset(p, 0, sizeof (*p));
	before = e->se_count;
	if (sh_save_target(target, p->sv_name, sizeof (p->sv_name), e) != 0)
		return (-1);
	if (s->ss_root[SH_SP_LAKE] == NULL)
		sh_errf(e, "no lake root");
	if (s->ss_root[SH_SP_RUN] == NULL)
		sh_errf(e, "no run root");
	if (e->se_count != before)
		return (-1);
	(void) snprintf(p->sv_run, sizeof (p->sv_run), "%s/containers/%s",
	    s->ss_root[SH_SP_RUN], p->sv_name);
	(void) snprintf(p->sv_lake, sizeof (p->sv_lake), "%s/containers/%s",
	    s->ss_root[SH_SP_LAKE], p->sv_name);
	(void) snprintf(p->sv_runf, sizeof (p->sv_runf), "%s/foundations",
	    s->ss_root[SH_SP_RUN]);
	(void) snprintf(p->sv_lakef, sizeof (p->sv_lakef), "%s/foundations",
	    s->ss_root[SH_SP_LAKE]);
	if ((r = sh_zfs_get(z, p->sv_run, "type", val, sizeof (val), NULL,
	    e)) != 0) {
		if (r == 1)
			sh_errf(e, "no container %s", p->sv_name);
		return (-1);
	}
	if ((all = calloc(1, sizeof (*all))) == NULL || sh_zw_add(&all,
	    p->sv_run) != 0 || sh_zw_walk(z, p->sv_run, &all, e) != 0) {
		if (all == NULL)
			sh_err_nomem(e);
		sh_zw_free(all);
		return (-1);
	}
	(void) snprintf(prefix, sizeof (prefix), "%s/", p->sv_runf);
	for (i = 0; all[i] != NULL; i++) {
		if (i == SH_SAVE_DS) {
			sh_errf(e, "container %s: more than %d datasets",
			    p->sv_name, SH_SAVE_DS);
			break;
		}
		(void) snprintf(p->sv_rel[i], SH_SAVE_NAME, "%s", i == 0 ? "" :
		    all[i] + strlen(p->sv_run) + 1);
		if (sh_zfs_get(z, all[i], "origin", val, sizeof (val), NULL,
		    e) != 0)
			break;
		/* a foundation's on zrun, or none: X's own */
		if (strcmp(val, "-") != 0 && strncmp(val, prefix,
		    strlen(prefix)) != 0)
			sh_errf(e, "%s: a clone of %s, not of a foundation",
			    all[i], val);
		else if (strcmp(val, "-") != 0)
			(void) snprintf(p->sv_origin[i],
			    sizeof (p->sv_origin[i]), "%s", val);
	}
	p->sv_n = i;
	sh_zw_free(all);
	if (e->se_count != before)
		return (-1);
	if (config != NULL) {
		(void) snprintf(p->sv_config, sizeof (p->sv_config), "%s",
		    config);
		if (sh_zfs_get(z, config, "guid", p->sv_guid,
		    sizeof (p->sv_guid), NULL, e) != 0) {
			sh_errf(e, "%s: no such config snapshot", config);
			return (-1);
		}
	} else if ((r = sv_lastconfig(z, p, e)) != 0) {
		if (r == 1)
			sh_errf(e, "container %s has no save to take "
			    "shtetl:config from", p->sv_name);
		return (-1);
	}
	return (e->se_count == before ? 0 : -1);
}

/* The full name of P's dataset I below BASE (sv_run or sv_lake). */
static void
sv_ds(const struct sh_save *p, const char *base, size_t i, char *buf,
    size_t len)
{
	(void) snprintf(buf, len, "%s%s%s", base, i > 0 ? "/" : "",
	    p->sv_rel[i]);
}

/*
 * Dataset I's mirror brought up to SNAP: a first, a clone stream as a
 * clone of zlake's counterpart of its origin, or whole; a later, from
 * its own newest mirrored save.
 */
static int
sv_mirror1(struct sh_zfs *z, const struct sh_save *p, size_t i,
    const char *snap, FILE *out, struct sh_err *e)
{
	char rds[SH_SAVE_NAME * 2], lds[SH_SAVE_NAME * 2];
	char from[SH_SAVE_NAME * 3], to[SH_SAVE_NAME * 3];
	char lorigin[SH_SAVE_NAME * 3], val[SH_ZFS_VALMAX], **have;
	const char *how;
	size_t n;
	int r;

	sv_ds(p, p->sv_run, i, rds, sizeof (rds));
	sv_ds(p, p->sv_lake, i, lds, sizeof (lds));
	(void) snprintf(from, sizeof (from), "%s@%s", rds, snap);
	(void) snprintf(to, sizeof (to), "%s@%s", lds, snap);
	if ((r = sh_zfs_get(z, lds, "type", val, sizeof (val), NULL, e)) < 0)
		return (-1);
	if (r == 1 && p->sv_origin[i][0] != '\0') {
		(void) snprintf(lorigin, sizeof (lorigin), "%s%s", p->sv_lakef,
		    p->sv_origin[i] + strlen(p->sv_runf));
		r = sh_zfs_send(z, from, p->sv_origin[i], to, lorigin, e);
		how = "a clone of zlake's foundation";
	} else if (r == 1) {
		r = sh_zfs_send(z, from, NULL, to, NULL, e);
		how = "whole";
	} else {
		if ((have = sh_zw_snaps(z, lds, e)) == NULL)
			return (-1);
		for (n = 0; have[n] != NULL; n++)
			;
		if (n > 0 && strcmp(have[n - 1], snap) == 0) {
			sh_zw_free(have);
			return (0);
		}
		(void) snprintf(val, sizeof (val), "%s@%s", rds, n > 0 ?
		    have[n - 1] : "");
		sh_zw_free(have);
		r = sh_zfs_send(z, from, val, to, NULL, e);
		how = "from its last";
	}
	if (r != 0 || sh_shtetl_copy(z, from, to, e) != 0)
		return (-1);
	(void) fprintf(out, "mirrored %s, %s\n", to, how);
	return (0);
}

int
sh_save_run(struct sh_zfs *z, const struct sh_save *p, const char *msg,
    time_t now, char *snap, size_t len, FILE *out, struct sh_err *e)
{
	char names[SH_SAVE_DS][SH_SAVE_NAME * 3], val[SH_ZFS_VALMAX];
	char stamp[64];
	const char *np[SH_SAVE_DS];
	struct sh_zprop zp[4];
	struct tm tm;
	size_t i;
	int n;

	if (msg == NULL || *msg == '\0') {
		sh_errf(e, "empty message");
		return (-1);
	}
	(void) gmtime_r(&now, &tm);
	(void) strftime(stamp, sizeof (stamp), "save-%Y-%m-%d-%H%M%S", &tm);
	(void) snprintf(snap, len, "%s", stamp);
	/* another save in the same second: -2, -3, ... */
	for (n = 2; n < 100; n++) {
		(void) snprintf(names[0], sizeof (names[0]), "%s@%s", p->sv_run,
		    snap);
		if (sh_zfs_get(z, names[0], "type", val, sizeof (val), NULL,
		    e) != 0)
			break;
		(void) snprintf(snap, len, "%s-%d", stamp, n);
	}
	for (i = 0; i < p->sv_n; i++) {
		sv_ds(p, p->sv_run, i, val, sizeof (val));
		(void) snprintf(names[i], sizeof (names[i]), "%s@%s", val,
		    snap);
		np[i] = names[i];
	}
	zp[0].zp_name = "shtetl:kind";
	zp[0].zp_value = "save";
	zp[1].zp_name = "shtetl:commit";
	zp[1].zp_value = msg;
	zp[2].zp_name = "shtetl:config";
	zp[2].zp_value = p->sv_config;
	zp[3].zp_name = "shtetl:config-guid";
	zp[3].zp_value = p->sv_guid;
	if (sh_zfs_snapshot(z, np, p->sv_n, zp, 4, e) != 0)
		return (-1);
	(void) fprintf(out, "saved %s@%s\n", p->sv_run, snap);
	for (i = 0; i < p->sv_n; i++) {
		if (sv_mirror1(z, p, i, snap, out, e) != 0) {
			sh_errf(e, "%s@%s is saved; its mirror into zlake "
			    "failed, and the next save brings it up to date",
			    p->sv_name, snap);
			return (-1);
		}
	}
	return (0);
}
