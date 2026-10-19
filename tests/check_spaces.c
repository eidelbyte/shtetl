/*
 * The spaces and their skeleton, src/core/spaces.c: family S of
 * tests/MATRIX.md, over the fake.
 */

#include "check.h"
#include "core/spaces.h"
#include "core/zfs.h"
#include "zfake.h"

static struct sh_zfs *z;
static struct cap c;

/* DS made, with shtetl:space VALUE when VALUE is not NULL. */
static void
mk(const char *ds, const char *value)
{
	struct sh_zprop p = { "shtetl:space", value };

	CHECK(sh_zfs_create(z, ds, &p, value != NULL ? 1 : 0, &c.c_err) == 0);
}

/* The fake as a fresh box: two pools, nothing marked. */
static void
fresh(void)
{
	zfake_reset();
	zfake_pool("tank");
	zfake_pool("fast");
	cap_reset(&c);
}

static int
find(struct sh_spaces *sp)
{
	return (sh_spaces_find(z, sp, &c.c_err));
}

int
main(void)
{
	struct sh_spaces sp;

	cap_open(&c);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);

	/* S1: nothing marked: every space absent, nothing said */
	fresh();
	CHECK(find(&sp) == 0);
	CHECK(sp.ss_root[SH_SP_LAKE] == NULL && sp.ss_root[SH_SP_RUN] == NULL);
	CHECK(strcmp(cap_text(&c), "") == 0);

	/* S2, S3: one of each, marked where they are; below them inherited */
	fresh();
	mk("tank/shtetl", NULL);
	mk("tank/shtetl/lake", "lake");
	mk("tank/shtetl/lake/x", NULL);
	mk("tank/shtetl/config", "config");
	mk("tank/build", "build");
	mk("fast/run", "run");
	CHECK(find(&sp) == 0);
	CHECK(strcmp(sp.ss_root[SH_SP_LAKE], "tank/shtetl/lake") == 0);
	CHECK(strcmp(sp.ss_root[SH_SP_CONFIG], "tank/shtetl/config") == 0);
	CHECK(strcmp(sp.ss_root[SH_SP_BUILD], "tank/build") == 0);
	CHECK(strcmp(sp.ss_root[SH_SP_RUN], "fast/run") == 0);
	sh_spaces_free(&sp);

	/* S4: two roots of one space, in two pools: both named, sorted */
	fresh();
	mk("tank/lake", "lake");
	mk("fast/lake", "lake");
	CHECK(find(&sp) == -1);
	CHECK(cap_has(&c, "two lake roots: fast/lake and tank/lake"));
	CHECK(sp.ss_root[SH_SP_LAKE] == NULL);

	/* S5: a value that is no space's */
	fresh();
	mk("tank/x", "attic");
	mk("tank/run", "run");
	CHECK(find(&sp) == -1);
	CHECK(cap_has(&c, "tank/x: invalid shtetl:space \"attic\""));
	CHECK(sp.ss_root[SH_SP_RUN] == NULL);

	/* S6: a root inside another's */
	fresh();
	mk("tank/lake", "lake");
	mk("tank/lake/foundations", NULL);
	mk("tank/lake/foundations/run", "run");
	CHECK(find(&sp) == -1);
	CHECK(cap_has(&c, "tank/lake/foundations/run: run root inside lake "
	    "root tank/lake"));

	/* S7: the walk refused: its words, and nothing found */
	fresh();
	mk("tank/lake", "lake");
	zfake_fail("local");
	CHECK(find(&sp) == -1);
	CHECK(cap_has(&c, "local failed as asked"));
	CHECK(sp.ss_root[SH_SP_LAKE] == NULL);

	/* S8: the skeleton made where it is missing, canmount off */
	fresh();
	mk("tank/lake", "lake");
	mk("fast/run", "run");
	CHECK(find(&sp) == 0);
	CHECK(sh_spaces_skeleton(z, &sp, &c.c_err) == 0);
	CHECK(strcmp(zfake_local("tank/lake/foundations", "canmount"),
	    "off") == 0);
	CHECK(strcmp(zfake_local("tank/lake/foundation-builds", "canmount"),
	    "off") == 0);

	/* S9: there already: left as it is, nothing said */
	CHECK(sh_spaces_skeleton(z, &sp, &c.c_err) == 0);
	CHECK(strcmp(cap_text(&c), "") == 0);
	sh_spaces_free(&sp);

	/* S10: no lake: nothing made for it */
	fresh();
	mk("fast/run", "run");
	CHECK(find(&sp) == 0);
	CHECK(sh_spaces_skeleton(z, &sp, &c.c_err) == 0);
	CHECK(zfake_local("tank/foundations", "canmount") == NULL);
	sh_spaces_free(&sp);

	/* S11: making it refused: its words */
	fresh();
	mk("tank/lake", "lake");
	CHECK(find(&sp) == 0);
	zfake_fail("create");
	CHECK(sh_spaces_skeleton(z, &sp, &c.c_err) == -1);
	CHECK(cap_has(&c, "create failed as asked"));
	sh_spaces_free(&sp);

	sh_zfs_close(z);
	cap_close(&c);
	return (0);
}
