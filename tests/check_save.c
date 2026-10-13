/*
 * shtetl save X (planning.md 7.5, 6.2, 7.10): tests/MATRIX.md family
 * S, over the fake ZFS, a lake root and a run root in two pools; and
 * the message from an editor (src/core/edit.c).
 */

#include <sys/stat.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "check.h"
#include "core/beam.h"
#include "core/edit.h"
#include "core/save.h"
#include "core/spaces.h"
#include "core/zfs.h"
#include "zfake.h"

#define	LF	"tank/shtetl/foundations/generic"
#define	RF	"fast/run/foundations/generic"
#define	RX	"fast/run/containers/x"
#define	LX	"tank/shtetl/containers/x"
#define	CFG	"tank/zconfig@create-x-20261008-120000"
#define	S1	"save-2026-10-08-120000"
/* 2026-10-08 12:00:00 UTC */
#define	NOON	((time_t)1791460800)

static struct cap c, wc;
static struct sh_zfs *z;
static struct sh_spaces s;
static struct sh_save p;
static char *ran, snapname[256];

static int
is(const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("got \"%s\", wanted \"%s\"\n", got, want);
		return (0);
	}
	return (1);
}

static const char *
prop(const char *name, const char *pr, enum sh_zsrc *src)
{
	static char val[4][512];
	static int n;

	n = (n + 1) % 4;
	if (sh_zfs_get(z, name, pr, val[n], sizeof (val[n]), src,
	    &wc.c_err) != 0)
		return ("(refused)");
	return (val[n]);
}

/*
 * The roots, the foundation generic at v1 in zlake and on zrun, and
 * the container x cloned from it as create clones one, with its own
 * www; with RUN 0, no run root and nothing on it.
 */
static void
world(int run)
{
	static const char *const v1[] = { LF "@v1", LF "/usr@v1",
	    LF "/usr/local@v1" };
	static const struct sh_zprop kind = { "shtetl:kind", "version" };
	static const struct sh_zprop off = { "canmount", "off" };
	static const struct sh_zprop cfg = { "shtetl:kind", "config" };
	static const char *const conf[] = { CFG };
	struct sh_beam b;
	FILE *f;

	zfake_reset();
	zfake_pool("tank");
	zfake_fs("tank/shtetl");
	zfake_set("tank/shtetl", "shtetl:space", "lake");
	zfake_set("tank/shtetl", "mountpoint", "none");
	zfake_fs("tank/shtetl/foundations");
	zfake_fs("tank/shtetl/containers");
	zfake_fs("tank/zconfig");
	zfake_set("tank/zconfig", "shtetl:space", "config");
	zfake_fs(LF);
	zfake_fs(LF "/usr");
	zfake_set(LF "/usr", "canmount", "off");
	zfake_fs(LF "/usr/local");
	cap_reset(&wc);
	CHECK(sh_zfs_snapshot(z, v1, 3, &kind, 1, &wc.c_err) == 0);
	CHECK(sh_zfs_snapshot(z, conf, 1, &cfg, 1, &wc.c_err) == 0);
	zfake_pool("fast");
	if (run) {
		zfake_fs("fast/run");
		zfake_set("fast/run", "shtetl:space", "run");
		zfake_set("fast/run", "mountpoint", "none");
		zfake_fs("fast/run/foundations");
		zfake_set("fast/run/foundations", "mountpoint",
		    "/shtetl/foundations");
		zfake_set("fast/run/foundations", "canmount", "off");
		zfake_fs("fast/run/containers");
		zfake_set("fast/run/containers", "mountpoint", "/containers");
	}
	sh_spaces_free(&s);
	cap_reset(&c);
	CHECK(sh_spaces_discover(z, &s, &c.c_err) == 0);
	if (!run)
		return;
	CHECK(sh_beam_plan(z, &s, "generic", "v1", &b, &c.c_err) == 0);
	CHECK((f = fopen("/dev/null", "w")) != NULL);
	CHECK(sh_beam_run(z, &b, f, &c.c_err) == 0);
	(void) fclose(f);
	CHECK(sh_zfs_clone(z, RF "@v1", RX, NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_clone(z, RF "/usr@v1", RX "/usr", &off, 1, &c.c_err) ==
	    0);
	CHECK(sh_zfs_clone(z, RF "/usr/local@v1", RX "/usr/local", NULL, 0,
	    &c.c_err) == 0);
	CHECK(sh_zfs_create(z, RX "/www", NULL, 0, &c.c_err) == 0);
}

/* NAME snapshotted, no property. */
static void
snap1(const char *name)
{
	cap_reset(&wc);
	CHECK(sh_zfs_snapshot(z, &name, 1, NULL, 0, &wc.c_err) == 0);
}

static int
plan(const char *target, const char *config)
{
	cap_reset(&c);
	return (sh_save_plan(z, &s, target, config, &p, &c.c_err));
}

static void
refused(const char *target, const char *config, const char *want)
{
	int rc;

	rc = plan(target, config);
	if (rc == 0 || !cap_has(&c, want))
		printf("%s: wanted \"%s\", got:\n%s", target, want,
		    cap_text(&c));
	CHECK(rc == -1 && cap_has(&c, want));
}

/* x saved with MSG, CONFIG given or not: its return. */
static int
save(const char *config, const char *msg)
{
	size_t len;
	FILE *f;
	int rc;

	if (plan("x", config) != 0) {
		printf("plan refused:\n%s", cap_text(&c));
		CHECK(0);
	}
	free(ran);
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	cap_reset(&c);
	rc = sh_save_run(z, &p, msg, NOON, snapname, sizeof (snapname), f,
	    &c.c_err);
	CHECK(fclose(f) == 0);
	return (rc);
}

/* S1: what is refused. */
static void
test_refused(void)
{
	world(0);
	refused("x", NULL, "no run root");
	world(1);
	refused("X.y", NULL, "X.y: invalid container name");
	refused("foundations/x", NULL, "foundations/x: invalid container");
	refused("nope", NULL, "no container nope");
	refused("x", NULL, "container x has no save to take shtetl:config "
	    "from");
	refused("x", "tank/zconfig@nope", "tank/zconfig@nope: no such config "
	    "snapshot");
	CHECK(plan("containers/x", CFG) == 0 && p.sv_n == 4);
	CHECK(is(p.sv_rel[1], "usr") && is(p.sv_origin[1], RF "/usr@v1"));
	CHECK(is(p.sv_rel[3], "www") && is(p.sv_origin[3], ""));
	CHECK(save(CFG, "") == -1 && cap_has(&c, "empty message"));
	/* a clone of something not a foundation's */
	snap1(RX "/www@s0");
	CHECK(sh_zfs_clone(z, RX "/www@s0", RX "/odd", NULL, 0,
	    &wc.c_err) == 0);
	refused("x", CFG, RX "/odd: a clone of " RX "/www@s0, not of a "
	    "foundation");
}

/* S2 - S5: the snapshot, the mirror, the copy of shtetl:*. */
static void
test_saves(void)
{
	enum sh_zsrc src;

	world(1);
	/* S2 */
	CHECK(save(CFG, "first") == 0);
	CHECK(is(snapname, S1));
	CHECK(strstr(ran, "saved " RX "@" S1 "\n") != NULL);
	CHECK(zfake_snapshot(RX "/usr/local@" S1) &&
	    zfake_snapshot(RX "/www@" S1));
	CHECK(is(prop(RX "/www@" S1, "shtetl:kind", &src), "save") &&
	    src == SH_ZSRC_LOCAL);
	CHECK(is(prop(RX "@" S1, "shtetl:commit", NULL), "first"));
	CHECK(is(prop(RX "@" S1, "shtetl:config", NULL), CFG));
	CHECK(is(prop(RX "@" S1, "shtetl:config-guid", NULL),
	    prop(CFG, "guid", NULL)));
	/* S3: the mirror */
	CHECK(strstr(ran, "mirrored " LX "@" S1 ", a clone of zlake's "
	    "foundation\n") != NULL);
	CHECK(strstr(ran, "mirrored " LX "/www@" S1 ", whole\n") != NULL);
	CHECK(is(prop(LX, "origin", NULL), LF "@v1"));
	CHECK(is(prop(LX "/usr", "origin", NULL), LF "/usr@v1"));
	CHECK(is(prop(LX "/usr/local", "origin", NULL), LF "/usr/local@v1"));
	CHECK(is(prop(LX "/www", "origin", NULL), "-"));
	/* S5 */
	CHECK(is(prop(LX "/www@" S1, "shtetl:kind", &src), "save") &&
	    src == SH_ZSRC_LOCAL);
	CHECK(is(prop(LX "@" S1, "shtetl:config", NULL), CFG));
	CHECK(is(prop(LX "/usr", "canmount", NULL), "on"));
	/* S2, S4: a later one, in the same second, its config carried */
	CHECK(save(NULL, "second") == 0);
	CHECK(is(snapname, S1 "-2"));
	CHECK(is(prop(RX "@" S1 "-2", "shtetl:config", NULL), CFG));
	CHECK(strstr(ran, "mirrored " LX "/usr/local@" S1 "-2, from its "
	    "last\n") != NULL);
	CHECK(zfake_snapshot(LX "/www@" S1) &&
	    zfake_snapshot(LX "/www@" S1 "-2"));
	CHECK(is(prop(LX "/www@" S1 "-2", "shtetl:commit", NULL), "second"));
}

/* S6: a mirror failing part way, brought up to date by the next. */
static void
test_mirror_failing(void)
{
	world(1);
	CHECK(save(CFG, "first") == 0);
	zfake_fail_nth("send", 3);
	CHECK(save(NULL, "second") == -1);
	CHECK(cap_has(&c, "x@" S1 "-2 is saved; its mirror into zlake "
	    "failed, and the next save brings it up to date"));
	CHECK(zfake_snapshot(RX "/www@" S1 "-2"));
	CHECK(zfake_snapshot(LX "/usr@" S1 "-2"));
	CHECK(!zfake_snapshot(LX "/usr/local@" S1 "-2"));
	CHECK(save(NULL, "third") == 0);
	CHECK(zfake_snapshot(LX "/usr/local@" S1 "-3"));
	CHECK(zfake_snapshot(LX "/www@" S1 "-3"));
	CHECK(!zfake_snapshot(LX "/www@" S1 "-2"));
	/* the first save's mirror failing: the next makes it */
	world(1);
	zfake_fail_nth("send", 4);
	CHECK(save(CFG, "first") == -1);
	CHECK(!zfake_exists(LX "/www"));
	CHECK(save(NULL, "again") == 0);
	CHECK(strstr(ran, "mirrored " LX "/www@" S1 "-2, whole\n") != NULL);
}

/* What the file PATH holds, or "". */
static const char *
slurp(const char *path)
{
	static char buf[4096];
	FILE *f;
	size_t n;

	buf[0] = '\0';
	if ((f = fopen(path, "r")) == NULL)
		return (buf);
	n = fread(buf, 1, sizeof (buf) - 1, f);
	buf[n] = '\0';
	(void) fclose(f);
	return (buf);
}

/* An editor in scratch: sh, writing TEXT to its $1 after a copy. */
static void
editor(const char *text, int fails)
{
	char body[1024], path[1200], seen[1200];

	(void) snprintf(seen, sizeof (seen), "%s", scratch_path("seen"));
	(void) snprintf(body, sizeof (body), "#!/bin/sh\ncp \"$1\" %s\n"
	    "printf '%s' > \"$1\"\nexit %d\n", seen, text, fails);
	(void) scratch_write("editor", body);
	(void) snprintf(path, sizeof (path), "%s", scratch_path("editor"));
	CHECK(chmod(path, 0755) == 0);
	CHECK(setenv("VISUAL", path, 1) == 0);
}

/* S7: the message from an editor. */
static void
test_editor(void)
{
	char *msg, *t;

	editor("fixed the www root\\n\\n# a comment\\n\\n", 0);
	cap_reset(&c);
	CHECK(sh_edit("What changed.\nWhy.", &msg, &c.c_err) == 0);
	CHECK(is(msg, "fixed the www root"));
	free(msg);
	CHECK(is(slurp(scratch_path("seen")), "\n# What changed.\n# Why.\n"));
	editor("# nothing but this\\n\\n", 0);
	CHECK(sh_edit("hint", &msg, &c.c_err) == -1 && msg == NULL);
	CHECK(cap_has(&c, "empty message"));
	editor("anything", 1);
	cap_reset(&c);
	CHECK(sh_edit("hint", &msg, &c.c_err) == -1);
	CHECK(cap_has(&c, "the message's editor failed"));
	/* $EDITOR where $VISUAL is not, and what it may carry */
	CHECK(unsetenv("VISUAL") == 0);
	editor("from EDITOR", 0);
	CHECK(setenv("EDITOR", getenv("VISUAL"), 1) == 0);
	CHECK(unsetenv("VISUAL") == 0);
	CHECK(sh_edit("hint", &msg, &c.c_err) == 0 && is(msg, "from EDITOR"));
	free(msg);
	(void) unsetenv("EDITOR");
	t = sh_edit_strip("\n\n  \nfirst\n# gone\n\nsecond line\n  \n\n");
	CHECK(t != NULL && is(t, "first\n\nsecond line"));
	free(t);
}

int
main(void)
{
	cap_open(&c);
	cap_open(&wc);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	test_refused();
	test_saves();
	test_mirror_failing();
	test_editor();
	free(ran);
	sh_spaces_free(&s);
	sh_zfs_close(z);
	cap_close(&wc);
	cap_close(&c);
	scratch_clean();
	printf("check_save: %d checks\n", checks);
	return (0);
}
