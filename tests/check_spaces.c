/*
 * Discovery (planning.md 4.1): tests/MATRIX.md family D, every cell,
 * over the fake ZFS of tests/zfake.c.
 */

#include <string.h>

#include "check.h"
#include "core/spaces.h"
#include "core/zfs.h"
#include "zfake.h"

static struct cap c;
static struct sh_zfs *z;
static struct sh_spaces s;

static int
D(void)
{
	sh_spaces_free(&s);
	cap_reset(&c);
	return (sh_spaces_discover(z, &s, &c.c_err));
}

/* The root of SP, or "" when it has none. */
static const char *
root(enum sh_space sp)
{
	return (s.ss_root[sp] != NULL ? s.ss_root[sp] : "");
}

static int
is(const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("got \"%s\", wanted \"%s\"\n", got, want);
		return (0);
	}
	return (1);
}

/* Two pools, tank and fast, with nothing of shtetl's. */
static void
pools(void)
{
	zfake_reset();
	zfake_pool("tank");
	zfake_fs("tank/home");
	zfake_pool("fast");
}

/* D1 - D3: finding the roots. */
static void
test_found(void)
{
	int sp;

	pools();
	CHECK(D() == 0);
	for (sp = 0; sp < SH_SP_N; sp++)
		CHECK(s.ss_root[sp] == NULL);

	/* D2: a pool's root is a root as well as anything deeper */
	zfake_set("fast", SH_SPACE_PROP, "run");
	zfake_fs("tank/shtetl");
	zfake_set("tank/shtetl", SH_SPACE_PROP, "lake");
	zfake_fs("tank/zconfig");
	zfake_set("tank/zconfig", SH_SPACE_PROP, "config");
	zfake_fs("tank/zbuild");
	zfake_set("tank/zbuild", SH_SPACE_PROP, "build");
	/* D3: what inherits the value is no root */
	zfake_fs("tank/shtetl/foundations");
	zfake_fs("fast/containers");
	CHECK(D() == 0);
	CHECK(is(root(SH_SP_LAKE), "tank/shtetl"));
	CHECK(is(root(SH_SP_CONFIG), "tank/zconfig"));
	CHECK(is(root(SH_SP_BUILD), "tank/zbuild"));
	CHECK(is(root(SH_SP_RUN), "fast"));
	CHECK(strcmp(sh_space_name(SH_SP_RUN), "run") == 0);
}

/* D4, D5, D7, D13: what discovery refuses. */
static void
test_refused(void)
{
	pools();
	zfake_set("tank/home", SH_SPACE_PROP, "camp");
	CHECK(D() == -1);
	CHECK(c.c_err.se_count == 1);
	CHECK(cap_has(&c, "tank/home: invalid shtetl:space \"camp\""));

	pools();
	zfake_fs("tank/c1");
	zfake_fs("tank/c2");
	zfake_set("tank/c2", SH_SPACE_PROP, "config");
	zfake_set("tank/c1", SH_SPACE_PROP, "config");
	zfake_fs("fast/b1");
	zfake_fs("fast/b2");
	zfake_set("fast/b1", SH_SPACE_PROP, "build");
	zfake_set("fast/b2", SH_SPACE_PROP, "build");
	zfake_pool("big");
	zfake_fs("big/lake");
	zfake_set("big/lake", SH_SPACE_PROP, "lake");
	zfake_fs("tank/lake");
	zfake_set("tank/lake", SH_SPACE_PROP, "lake");
	zfake_fs("big/run");
	zfake_set("big/run", SH_SPACE_PROP, "run");
	zfake_fs("tank/run");
	zfake_set("tank/run", SH_SPACE_PROP, "run");
	CHECK(D() == -1);
	CHECK(c.c_err.se_count == 4);
	CHECK(cap_has(&c, "two config roots: tank/c1 and tank/c2\n"));
	CHECK(cap_has(&c, "two build roots: fast/b1 and fast/b2\n"));
	CHECK(cap_has(&c, "two lake roots: big/lake and tank/lake\n"));
	CHECK(cap_has(&c, "two run roots: big/run and tank/run\n"));
	CHECK(s.ss_root[SH_SP_LAKE] == NULL && s.ss_root[SH_SP_RUN] == NULL);

	/* D7: inside another root */
	pools();
	zfake_fs("tank/shtetl");
	zfake_set("tank/shtetl", SH_SPACE_PROP, "lake");
	zfake_fs("tank/shtetl/run");
	zfake_set("tank/shtetl/run", SH_SPACE_PROP, "run");
	zfake_set("fast", SH_SPACE_PROP, "config");
	zfake_fs("fast/deep");
	zfake_fs("fast/deep/build");
	zfake_set("fast/deep/build", SH_SPACE_PROP, "build");
	CHECK(D() == -1);
	CHECK(c.c_err.se_count == 2);
	CHECK(cap_has(&c, "tank/shtetl/run: run root inside lake root "
	    "tank/shtetl"));
	CHECK(cap_has(&c, "fast/deep/build: build root inside config root "
	    "fast"));
	/* a name that only begins like a root's is not inside it */
	pools();
	zfake_fs("tank/sh");
	zfake_set("tank/sh", SH_SPACE_PROP, "lake");
	zfake_fs("tank/shtetl");
	zfake_set("tank/shtetl", SH_SPACE_PROP, "run");
	CHECK(D() == 0);

	/* D13 */
	zfake_fail("local");
	CHECK(D() == -1);
	CHECK(cap_has(&c, "zfake: local failed as asked"));
}

/* D8, D10: a name under its space's root. */
static void
test_lookup(void)
{
	char *out;

	pools();
	zfake_fs("tank/shtetl");
	zfake_set("tank/shtetl", SH_SPACE_PROP, "lake");
	zfake_fs("tank/shtetl/foundations");
	zfake_fs("tank/shtetl/foundations/generic");
	zfake_fs("tank/shtetl/foundations/F2");
	CHECK(D() == 0);
	CHECK(sh_spaces_lookup(z, &s, SH_SP_LAKE, "foundations/generic", &out,
	    &c.c_err) == 0);
	CHECK(is(out, "tank/shtetl/foundations/generic"));
	free(out);
	CHECK(sh_spaces_lookup(z, &s, SH_SP_LAKE, "foundations/gaming", &out,
	    &c.c_err) == 1);
	CHECK(out == NULL);
	CHECK(c.c_err.se_count == 0);
	/* D10 */
	CHECK(sh_spaces_lookup(z, &s, SH_SP_LAKE, "foundations/F", &out,
	    &c.c_err) == 1);
	/* nothing in run: no root, no match */
	CHECK(sh_spaces_lookup(z, &s, SH_SP_RUN, "foundations/generic", &out,
	    &c.c_err) == 1);
	/* ZFS refusing the look */
	cap_reset(&c);
	zfake_fail("get");
	CHECK(sh_spaces_lookup(z, &s, SH_SP_LAKE, "foundations/x", &out,
	    &c.c_err) == -1);
	CHECK(cap_has(&c, "zfake: get failed as asked"));
}

/* D14, D15: the directory the configuration is read from. */
static void
test_config_dir(void)
{
	char *dir;

	pools();
	CHECK(D() == 0);
	CHECK(sh_spaces_config_dir(z, &s, &dir, &c.c_err) == -1);
	CHECK(dir == NULL);
	CHECK(cap_has(&c, "no config root\n"));

	zfake_fs("tank/zconfig");
	zfake_set("tank/zconfig", SH_SPACE_PROP, "config");
	zfake_set("tank/zconfig", "mountpoint", "/zconfig");
	CHECK(D() == 0);
	CHECK(sh_spaces_config_dir(z, &s, &dir, &c.c_err) == -1);
	CHECK(cap_has(&c, "config root tank/zconfig: not mounted"));
	zfake_set("tank/zconfig", "mounted", "yes");
	cap_reset(&c);
	CHECK(sh_spaces_config_dir(z, &s, &dir, &c.c_err) == 0);
	CHECK(is(dir, "/zconfig"));
	free(dir);
	/* inherited from the pool: tank/zconfig at /tank/zconfig */
	zfake_set("tank/zconfig", "mountpoint", "none");
	CHECK(sh_spaces_config_dir(z, &s, &dir, &c.c_err) == -1);
	CHECK(cap_has(&c, "config root tank/zconfig: no mountpoint"));
	zfake_set("tank/zconfig", "mountpoint", "legacy");
	cap_reset(&c);
	CHECK(sh_spaces_config_dir(z, &s, &dir, &c.c_err) == -1);
	CHECK(cap_has(&c, "config root tank/zconfig: no mountpoint"));
}

/* What sh_spaces_show printed; the last one is freed at the end. */
static char *shownbuf;

static const char *
shown(void)
{
	size_t len;
	FILE *f;

	free(shownbuf);
	shownbuf = NULL;
	f = open_memstream(&shownbuf, &len);
	CHECK(f != NULL);
	CHECK(sh_spaces_show(f, z, &s, &c.c_err) == 0);
	CHECK(fclose(f) == 0);
	return (shownbuf);
}

/* D16: shtetl spaces. */
static void
test_show(void)
{
	pools();
	CHECK(D() == 0);
	CHECK(is(shown(),
	    "lake    -  none: shtetl bootstrap makes one\n"
	    "config  -  none: shtetl bootstrap makes one\n"
	    "build   -  none: shtetl bootstrap makes one\n"
	    "run     -  none: shtetl bootstrap makes one\n"));

	zfake_fs("tank/lake");
	zfake_set("tank/lake", SH_SPACE_PROP, "lake");
	zfake_fs("tank/zconfig");
	zfake_set("tank/zconfig", SH_SPACE_PROP, "config");
	zfake_set("tank/zconfig", "mountpoint", "/zconfig");
	zfake_set("tank/zconfig", "mounted", "yes");
	zfake_fs("tank/zbuild");
	zfake_set("tank/zbuild", SH_SPACE_PROP, "build");
	zfake_set("tank/zbuild", "mountpoint", "/usr/src");
	zfake_set("fast", SH_SPACE_PROP, "run");
	CHECK(D() == 0);
	CHECK(is(shown(),
	    "lake    tank/lake\n"
	    "config  tank/zconfig  mounted at /zconfig\n"
	    "build   tank/zbuild  not mounted\n"
	    "run     fast\n"));
}

int
main(void)
{
	cap_open(&c);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	test_found();
	test_refused();
	test_lookup();
	test_config_dir();
	test_show();
	free(shownbuf);
	sh_spaces_free(&s);
	sh_zfs_close(z);
	cap_close(&c);
	printf("check_spaces: %d checks\n", checks);
	return (0);
}
