/*
 * Discovery and multi-rooting (planning.md 4.1): tests/MATRIX.md
 * family D, every cell (D1 - D13), over the fake ZFS of tests/zfake.c.
 */

#include <string.h>

#include "check.h"
#include "core/parse.h"
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

/* The roots of SP, space-separated. */
static const char *
roots(enum sh_space sp)
{
	static char buf[1024];
	size_t i, n;

	buf[0] = '\0';
	for (i = 0; i < s.ss_n[sp]; i++) {
		n = strlen(buf);
		(void) snprintf(buf + n, sizeof (buf) - n, "%s%s",
		    i > 0 ? " " : "", s.ss_roots[sp][i]);
	}
	return (buf);
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

/* D1 - D3, D6: finding the roots. */
static void
test_found(void)
{
	int sp;

	pools();
	CHECK(D() == 0);
	for (sp = 0; sp < SH_SP_N; sp++)
		CHECK(s.ss_n[sp] == 0);

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
	CHECK(is(roots(SH_SP_LAKE), "tank/shtetl"));
	CHECK(is(roots(SH_SP_CONFIG), "tank/zconfig"));
	CHECK(is(roots(SH_SP_BUILD), "tank/zbuild"));
	CHECK(is(roots(SH_SP_RUN), "fast"));

	/* D6: several, across pools, sorted */
	zfake_fs("tank/run2");
	zfake_set("tank/run2", SH_SPACE_PROP, "run");
	zfake_pool("big");
	zfake_fs("big/lake");
	zfake_set("big/lake", SH_SPACE_PROP, "lake");
	zfake_fs("tank/lake0");
	zfake_set("tank/lake0", SH_SPACE_PROP, "lake");
	CHECK(D() == 0);
	CHECK(is(roots(SH_SP_LAKE), "big/lake tank/lake0 tank/shtetl"));
	CHECK(is(roots(SH_SP_RUN), "fast tank/run2"));
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
	CHECK(cap_has(&c, "tank/home: shtetl:space is \"camp\", which names "
	    "no space: lake, config, build or run"));

	pools();
	zfake_fs("tank/c1");
	zfake_fs("tank/c2");
	zfake_set("tank/c2", SH_SPACE_PROP, "config");
	zfake_set("tank/c1", SH_SPACE_PROP, "config");
	zfake_fs("fast/b1");
	zfake_fs("fast/b2");
	zfake_set("fast/b1", SH_SPACE_PROP, "build");
	zfake_set("fast/b2", SH_SPACE_PROP, "build");
	CHECK(D() == -1);
	CHECK(c.c_err.se_count == 2);
	CHECK(cap_has(&c, "two config roots, tank/c1 and tank/c2: the config "
	    "space has one"));
	CHECK(cap_has(&c, "two build roots, fast/b1 and fast/b2"));

	/* D7: inside another root, of the same space or another */
	pools();
	zfake_fs("tank/shtetl");
	zfake_set("tank/shtetl", SH_SPACE_PROP, "lake");
	zfake_fs("tank/shtetl/run");
	zfake_set("tank/shtetl/run", SH_SPACE_PROP, "run");
	zfake_set("fast", SH_SPACE_PROP, "lake");
	zfake_fs("fast/deep");
	zfake_fs("fast/deep/lake");
	zfake_set("fast/deep/lake", SH_SPACE_PROP, "lake");
	CHECK(D() == -1);
	CHECK(c.c_err.se_count == 2);
	CHECK(cap_has(&c, "tank/shtetl/run: a run root inside the lake root "
	    "tank/shtetl"));
	CHECK(cap_has(&c, "fast/deep/lake: a lake root inside the lake root "
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

/* D8 - D10: one name, one place. */
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
	zfake_set("fast", SH_SPACE_PROP, "lake");
	zfake_fs("fast/foundations");
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
	/* D9 */
	zfake_fs("fast/foundations/generic");
	CHECK(sh_spaces_lookup(z, &s, SH_SP_LAKE, "foundations/generic", &out,
	    &c.c_err) == -1);
	CHECK(out == NULL);
	CHECK(cap_has(&c, "foundations/generic is under two lake roots, as "
	    "fast/foundations/generic and tank/shtetl/foundations/generic: "
	    "one name, one place"));
	/* ZFS refusing the look */
	cap_reset(&c);
	zfake_fail("get");
	CHECK(sh_spaces_lookup(z, &s, SH_SP_LAKE, "foundations/x", &out,
	    &c.c_err) == -1);
	CHECK(cap_has(&c, "zfake: get failed as asked"));
}

/* TEXT parsed as shtetl.conf; the caller frees it. */
static struct sh_node *
conf(const char *text)
{
	struct sh_node *n;

	n = sh_parse_buf(text, strlen(text), "shtetl.conf", &c.c_err);
	CHECK(n != NULL);
	return (n);
}

/* D11, D12: where something new goes. */
static void
test_default(void)
{
	struct sh_node *site;
	const char *root;

	pools();
	CHECK(D() == 0);
	CHECK(sh_spaces_default(&s, NULL, SH_SP_LAKE, &root, &c.c_err) == -1);
	CHECK(root == NULL);
	CHECK(cap_has(&c, "no lake root: shtetl bootstrap makes one"));

	zfake_fs("tank/shtetl");
	zfake_set("tank/shtetl", SH_SPACE_PROP, "lake");
	CHECK(D() == 0);
	CHECK(sh_spaces_default(&s, NULL, SH_SP_LAKE, &root, &c.c_err) == 0);
	CHECK(is(root, "tank/shtetl"));
	/* one root, and a default that names another: refused */
	site = conf("spaces { lake { default = \"fast/lake\"; } }");
	CHECK(sh_spaces_default(&s, site, SH_SP_LAKE, &root, &c.c_err) == -1);
	CHECK(cap_has(&c, "shtetl.conf: spaces.lake.default names fast/lake, "
	    "which is no lake root"));

	zfake_fs("fast/lake");
	zfake_set("fast/lake", SH_SPACE_PROP, "lake");
	CHECK(D() == 0);
	CHECK(sh_spaces_default(&s, NULL, SH_SP_LAKE, &root, &c.c_err) == -1);
	CHECK(cap_has(&c, "2 lake roots, and shtetl.conf names none of them "
	    "as spaces.lake.default"));
	CHECK(sh_spaces_default(&s, site, SH_SP_LAKE, &root, &c.c_err) == 0);
	CHECK(is(root, "fast/lake"));
	sh_node_free(site);
	/* a run root named as the lake's default */
	zfake_fs("fast/run");
	zfake_set("fast/run", SH_SPACE_PROP, "run");
	CHECK(D() == 0);
	site = conf("spaces { lake { default = \"fast/run\"; } }");
	cap_reset(&c);
	CHECK(sh_spaces_default(&s, site, SH_SP_LAKE, &root, &c.c_err) == -1);
	CHECK(cap_has(&c, "names fast/run, which is no lake root"));
	sh_node_free(site);
}

int
main(void)
{
	cap_open(&c);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	test_found();
	test_refused();
	test_lookup();
	test_default();
	sh_spaces_free(&s);
	sh_zfs_close(z);
	cap_close(&c);
	printf("check_spaces: %d checks\n", checks);
	return (0);
}
