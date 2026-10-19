/*
 * A foundation's start, src/core/foundation.c: family A of
 * tests/MATRIX.md, over the fake. Each cell makes what a crash part
 * way through a roll leaves, and starts the foundations.
 */

#include "check.h"
#include "core/foundation.h"
#include "core/zfs.h"
#include "zfake.h"

#define	F	"tank/lake/foundations"

static struct sh_zfs *z;
static struct cap c;
static struct sh_foundation *f;
static size_t n;

/* REL below the foundations, with a mountpoint when MP is not NULL. */
static void
mk(const char *rel, const char *mp)
{
	struct sh_zprop p = { "mountpoint", mp };
	char ds[256];

	(void) snprintf(ds, sizeof (ds), "%s/%s", F, rel);
	CHECK(sh_zfs_create(z, ds, &p, mp != NULL ? 1 : 0, &c.c_err) == 0);
}

/* REL below the foundations: its snapshot SNAP, then mounted. */
static void
snap(const char *rel, const char *s)
{
	char name[256];
	const char *names[1] = { name };

	(void) snprintf(name, sizeof (name), "%s/%s@%s", F, rel, s);
	CHECK(sh_zfs_snapshot(z, names, 1, NULL, 0, &c.c_err) == 0);
}

static const char *
at(const char *rel)
{
	static char buf[4][256];
	static int i;

	i = (i + 1) % 4;
	(void) snprintf(buf[i], sizeof (buf[i]), "%s/%s", F, rel);
	return (buf[i]);
}

static void
mount(const char *rel)
{
	CHECK(sh_zfs_mount(z, at(rel), &c.c_err) == 0);
}

static int
mounted(const char *rel)
{
	char path[256];

	return (sh_zfs_mounted(z, at(rel), path, sizeof (path), &c.c_err));
}

/* Whether REL is there: a walk of its parent's children. */
static int
there(const char *rel)
{
	char path[256];

	return (sh_zfs_mounted(z, at(rel), path, sizeof (path), &c.c_err) >=
	    0);
}

/* A lake with its foundations and nothing in them. */
static void
fresh(void)
{
	static const struct sh_zprop off = { "canmount", "off" };

	if (f != NULL)
		sh_foundations_free(f, n);
	f = NULL;
	n = 0;
	zfake_reset();
	zfake_pool("tank");
	CHECK(sh_zfs_create(z, "tank/lake", NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, F, &off, 1, &c.c_err) == 0);
	cap_reset(&c);
}

static int
start(void)
{
	return (sh_foundations_start(z, F, &f, &n, &c.c_err));
}

/* F[I]'s status line. */
static const char *
line(size_t i)
{
	static char buf[512];
	FILE *o;

	CHECK((o = fmemopen(buf, sizeof (buf), "w")) != NULL);
	sh_foundation_status(&f[i], o);
	(void) fclose(o);
	return (buf);
}

int
main(void)
{
	static const struct sh_zprop stall = {
		"shtetl:stall", "build: make exited 2"
	};

	cap_open(&c);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);

	/* A1: versions, nothing written since: idle at the newest */
	fresh();
	mk("generic", NULL);
	snap("generic", "v1");
	snap("generic", "v2");
	CHECK(start() == 0 && n == 1);
	CHECK(strcmp(f[0].f_name, "generic") == 0 &&
	    f[0].f_state == SH_F_IDLE && strcmp(f[0].f_newest, "v2") == 0);
	CHECK(strcmp(line(0), "foundation generic idle v2\n") == 0);
	CHECK(strcmp(cap_text(&c), "") == 0);

	/* A2: written since the newest, mounted: undone and unmounted */
	fresh();
	mk("generic", "/g");
	mount("generic");
	CHECK(zfake_put(at("generic"), "a") == 0);
	snap("generic", "v1");
	CHECK(zfake_put(at("generic"), "b") == 0);
	CHECK(start() == 0 && n == 1 && f[0].f_state == SH_F_IDLE);
	CHECK(mounted("generic") == 0);
	mount("generic");
	CHECK(zfake_has(at("generic"), "a") == 1 &&
	    zfake_has(at("generic"), "b") == 0);
	CHECK(strcmp(cap_text(&c), "") == 0);

	/* A3: no version at all, mounted, a child too: undone, gone */
	fresh();
	mk("first", "/first");
	mk("first/usr", NULL);
	mount("first");
	mount("first/usr");
	CHECK(zfake_put(at("first"), "a") == 0);
	CHECK(start() == 0 && n == 0);
	CHECK(!there("first") && !there("first/usr"));
	CHECK(cap_has(&c, "foundation first: a first build undone"));

	/* A4: a version on the head, a child with none: the child gone */
	fresh();
	mk("generic", NULL);
	snap("generic", "v1");
	mk("generic/usr", NULL);
	CHECK(start() == 0 && n == 1 && f[0].f_state == SH_F_IDLE);
	CHECK(there("generic") && !there("generic/usr"));

	/*
	 * A5: the head mounted and a child mounted inside it: the child
	 * unmounted first, which the fake, as umount, insists on
	 */
	fresh();
	mk("generic", "/g");
	mk("generic/usr", NULL);
	snap("generic", "v1");
	snap("generic/usr", "v1");
	mount("generic");
	mount("generic/usr");
	CHECK(zfake_put(at("generic/usr"), "late") == 0);
	CHECK(start() == 0 && n == 1 && f[0].f_state == SH_F_IDLE);
	CHECK(mounted("generic") == 0 && mounted("generic/usr") == 0);
	mount("generic");
	mount("generic/usr");
	CHECK(zfake_has(at("generic/usr"), "late") == 0);

	/* A6: stalled: its text, its version kept; started all the same */
	fresh();
	CHECK(sh_zfs_create(z, at("generic"), &stall, 1, &c.c_err) == 0);
	snap("generic", "v1");
	CHECK(start() == 0 && n == 1);
	CHECK(f[0].f_state == SH_F_STALLED &&
	    strcmp(f[0].f_stall, "build: make exited 2") == 0 &&
	    strcmp(f[0].f_newest, "v1") == 0);
	CHECK(strcmp(line(0),
	    "foundation generic stalled build: make exited 2\n") == 0);

	/*
	 * A7: a step refused: that foundation stalled with the refusal,
	 * said; the others started all the same
	 */
	fresh();
	mk("a", NULL);
	snap("a", "v1");
	mk("b", NULL);
	snap("b", "v1");
	zfake_fail("rollback");
	CHECK(start() == 0 && n == 2);
	CHECK(f[0].f_state == SH_F_STALLED &&
	    strcmp(f[0].f_stall, "zfake: rollback failed as asked") == 0);
	CHECK(f[1].f_state == SH_F_IDLE);
	CHECK(cap_has(&c, "foundation a: zfake: rollback failed as asked"));

	/* A8: several: by name, whatever order ZFS walks them in */
	fresh();
	mk("zz", NULL);
	snap("zz", "v1");
	mk("aa", NULL);
	snap("aa", "v1");
	mk("mm", NULL);
	snap("mm", "v1");
	CHECK(start() == 0 && n == 3);
	CHECK(strcmp(f[0].f_name, "aa") == 0 &&
	    strcmp(f[1].f_name, "mm") == 0 && strcmp(f[2].f_name, "zz") == 0);

	/* A9: the foundations' walk refused: no start */
	fresh();
	mk("generic", NULL);
	snap("generic", "v1");
	zfake_fail("children");
	CHECK(start() == -1 && n == 0 && f == NULL);
	CHECK(cap_has(&c, "children failed as asked"));

	/* A10: none: nothing */
	fresh();
	CHECK(start() == 0 && n == 0);

	sh_foundations_free(f, n);
	sh_zfs_close(z);
	cap_close(&c);
	return (0);
}
