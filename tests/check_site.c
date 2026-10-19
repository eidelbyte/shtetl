/*
 * shtetl.conf (docs/configuration.md 3): tests/MATRIX.md family CS,
 * every cell (CS1 - CS15, CS3 and CS6 retired).
 */

#include <string.h>

#include "check.h"
#include "core/parse.h"
#include "core/site.h"
#include "core/tree.h"

static struct cap c;

/* TEXT parsed and checked as shtetl.conf: the count. */
static unsigned
K(const char *text)
{
	struct sh_node *n;
	unsigned bad;

	cap_reset(&c);
	n = sh_parse_buf(text, strlen(text), "shtetl.conf", &c.c_err);
	if (n == NULL) {
		printf("did not parse: %s\n%s", text, cap_text(&c));
		CHECK(n != NULL);
	}
	bad = sh_site_check(n, "shtetl.conf", &c.c_err);
	sh_node_free(n);
	return (bad);
}

static void
ok(const char *text)
{
	if (K(text) != 0)
		printf("ok(\"%s\"):\n%s", text, cap_text(&c));
	CHECK(c.c_err.se_count == 0);
}

static void
bad(const char *text, const char *want)
{
	unsigned n;

	n = K(text);
	if (n != 1 || !cap_has(&c, want))
		printf("bad(\"%s\"): wanted one message with \"%s\", got %u:\n"
		    "%s", text, want, n, cap_text(&c));
	CHECK(n == 1);
	CHECK(cap_has(&c, want));
}

/* CS1: every section, as docs/configuration.md 3 shows them. */
static void
test_example(void)
{
	ok("properties {\n"
	    "    default { compression = \"zstd\"; }\n"
	    "    lake    { compression = \"zstd-9\"; }\n"
	    "}\n"
	    "build {\n"
	    "    make_jobs = 0;                  # 0 = hw.ncpu\n"
	    "}\n"
	    "roll {\n"
	    "    depth = \"30d\";\n"
	    "}\n"
	    "run {\n"
	    "    keep = 2;\n"
	    "}\n"
	    "lake {\n"
	    "    keep = 5;\n"
	    "}\n");
}

/* CS2, CS10: sections and keys; a removed one is an invalid key. */
static void
test_keys(void)
{
	bad("frob { }", "shtetl.conf: frob: invalid key");
	bad("build { jobs = 1; }", "build.jobs: invalid key");
	bad("build = 4;", "build: must be an object");
	bad("run { depth = 1; }", "run.depth: invalid key");
	/* CS10 */
	bad("update { suffix = \"staging\"; }", "shtetl.conf: update: "
	    "invalid key");
	bad("retention { run = 5; }", "shtetl.conf: retention: invalid key");
}

/* CS4: properties. */
static void
test_properties(void)
{
	ok("properties { lake { atime = \"on\"; } build { exec = false; } "
	    "config { \"com.example:x\" = \"y\"; } run { atime = \"off\"; } "
	    "}");
	bad("properties { bogus { } }", "properties.bogus: invalid key");
	bad("properties { default { recordsize = 1M; } }",
	    "properties.default.recordsize: must be a quoted string");
	bad("properties { run { mountpoint = \"/x\"; } }",
	    "properties.run.mountpoint: invalid property: reserved");
	bad("properties { run { canmount = \"on\"; } }", "invalid property: "
	    "reserved");
	bad("properties { lake = \"zstd\"; }", "properties.lake: must be an "
	    "object");
}

/* CS5, CS7, CS12: a count, roll.depth, the keeps. */
static void
test_values(void)
{
	static const char *const good[] = { "0", "10", "\"7d\"", "\"6m\"",
	    "\"1y\"", "\"12H\"", "\"30M\"", "\"2w\"", "\"90S\"" };
	static const char *const poor[] = { "\"7x\"", "\"d7\"", "\"\"",
	    "\"0d\"", "\"7dd\"", "\"-1d\"" };
	static const char *const keeps[] = { "run { keep = %s; }",
	    "lake { keep = %s; }" };
	char text[128];
	size_t i, j;

	/* CS5 */
	ok("build { make_jobs = 8; }");
	bad("build { make_jobs = -1; }", "build.make_jobs: must be 0 or more");
	bad("build { make_jobs = \"4\"; }", "build.make_jobs: must be an "
	    "integer");
	bad("build { make_jobs = 1.5; }", "build.make_jobs: must be an "
	    "integer");
	/* CS7 */
	for (j = 0; j < sizeof (good) / sizeof (good[0]); j++) {
		(void) snprintf(text, sizeof (text), "roll { depth = %s; }",
		    good[j]);
		ok(text);
	}
	for (j = 0; j < sizeof (poor) / sizeof (poor[0]); j++) {
		(void) snprintf(text, sizeof (text), "roll { depth = %s; }",
		    poor[j]);
		bad(text, "invalid interval: must be a count above 0 and one "
		    "of y, m, w, d, H, M, S");
	}
	bad("roll { depth = -1; }", "roll.depth: must be 0 or more");
	bad("roll { depth = 1.5; }", "must be an integer or a quoted "
	    "interval");
	bad("roll { depth = 30d; }", "must be an integer or a quoted "
	    "interval");
	bad("roll { depth = true; }", "must be an integer or a quoted "
	    "interval");
	/* CS12: the active version counts, so 1 is the least */
	for (i = 0; i < sizeof (keeps) / sizeof (keeps[0]); i++) {
		(void) snprintf(text, sizeof (text), keeps[i], "1");
		ok(text);
		(void) snprintf(text, sizeof (text), keeps[i], "12");
		ok(text);
		(void) snprintf(text, sizeof (text), keeps[i], "0");
		bad(text, "keep: must be 1 or more");
		(void) snprintf(text, sizeof (text), keeps[i], "-2");
		bad(text, "keep: must be 1 or more");
		(void) snprintf(text, sizeof (text), keeps[i], "\"30d\"");
		bad(text, "keep: must be an integer");
		(void) snprintf(text, sizeof (text), keeps[i], "\"2\"");
		bad(text, "keep: must be an integer");
	}
}

/* CS13, CS14: the remotes; the lake's is out of scope (plan 8.1). */
static void
test_remotes(void)
{
	ok("remotes { src = \"https://git.example.org/src.git\"; }");
	ok("remotes { src = \"github.com/freebsd/freebsd-src\"; }");
	ok("remotes { recipes { shtetl-recipes = "
	    "\"https://github.com/eidelbyte/shtetl-recipes.git\"; "
	    "site = \"ssh://git@git.example.org/recipes.git\"; } }");
	bad("remotes { src = \"recipes/local\"; }", "remotes.src: invalid "
	    "remote: a URL, or a host and a path");
	bad("remotes { src = \"git@github.com:x/y\"; }", "remotes.src: "
	    "invalid remote: scp-style address: use ssh://");
	bad("remotes { src = \"\"; }", "remotes.src: invalid remote: empty");
	bad("remotes { src = 5; }", "remotes.src: must be a string");
	/* CS14 */
	bad("remotes { recipes { \"a.b\" = \"github.com/x/y\"; } }",
	    "remotes.recipes.\"a.b\": invalid name: only letters, digits, - "
	    "and _ allowed");
	bad("remotes { recipes { site = \"site\"; } }", "remotes.recipes.site: "
	    "invalid remote: a URL, or a host and a path");
	bad("remotes { recipes = [ \"github.com/x/y\" ]; }",
	    "remotes.recipes: must be an object");
	bad("remotes { lake = \"ssh://lake\"; }", "remotes.lake: invalid key");
}

/* KEY's value in OBJ, a string, set by the layer FROM. */
static void
is_str(const struct sh_node *obj, const char *key, const char *want,
    const char *from)
{
	const struct sh_node *v;

	v = sh_obj_get(obj, key);
	CHECK(v != NULL && v->sn_type == SH_STR);
	CHECK(strcmp(v->sn_str, want) == 0);
	CHECK(strcmp(v->sn_from, from) == 0);
}

/* KEY's value in OBJ, an integer, set by the layer FROM. */
static void
is_int(const struct sh_node *obj, const char *key, int64_t want,
    const char *from)
{
	const struct sh_node *v;

	v = sh_obj_get(obj, key);
	CHECK(v != NULL && v->sn_type == SH_INT && v->sn_int == want);
	CHECK(strcmp(v->sn_from, from) == 0);
}

/* CS8, CS9, CS11: the built-in settings, and shtetl.conf over them. */
static void
test_load(void)
{
	struct sh_site s;
	const struct sh_node *props, *dflt, *v;
	const char **repos;
	char root[1100];

	/* no shtetl.conf at all */
	(void) snprintf(root, sizeof (root), "%s", scratch_path("empty"));
	CHECK(mkdir(root, 0755) == 0);
	cap_reset(&c);
	CHECK(sh_site_load(root, &s, &c.c_err) == 0);
	CHECK(c.c_err.se_count == 0);
	CHECK(!s.ss_has_conf);
	dflt = sh_obj_get(sh_obj_get(s.ss_merged, "properties"), "default");
	CHECK(dflt != NULL && dflt->sn_obj.ob_n == 3);
	is_str(dflt, "atime", "off", "(built-in)");
	is_str(dflt, "compression", "zstd", "(built-in)");
	is_str(dflt, "aclmode", "passthrough", "(built-in)");
	is_int(sh_obj_get(s.ss_merged, "build"), "make_jobs", 0, "(built-in)");
	is_int(sh_obj_get(s.ss_merged, "roll"), "depth", 0, "(built-in)");
	is_int(sh_obj_get(s.ss_merged, "run"), "keep", 2, "(built-in)");
	is_int(sh_obj_get(s.ss_merged, "lake"), "keep", 5, "(built-in)");
	CHECK(strcmp(s.ss_merged->sn_obj.ob_v[0].pr_key, "properties") == 0);
	/* CS15: the src remote built in, no recipe repository */
	is_str(sh_obj_get(s.ss_merged, "remotes"), "src",
	    "https://git.freebsd.org/src.git", "(built-in)");
	repos = sh_site_repos(&s);
	CHECK(repos[0] == NULL);
	free(repos);
	sh_site_free(&s);

	/* shtetl.conf sets one property, adds a section, sets a keep */
	(void) scratch_write("one/shtetl.conf",
	    "properties {\n"
	    "    default { compression = \"zstd-9\"; }\n"
	    "    lake { atime = \"on\"; }\n"
	    "}\n"
	    "run { keep = 3; }\n"
	    "roll { depth = \"30d\"; }\n"
	    "remotes { recipes { web = \"github.com/x/web\"; "
	    "site = \"github.com/x/site\"; } }\n");
	(void) snprintf(root, sizeof (root), "%s", scratch_path("one"));
	cap_reset(&c);
	CHECK(sh_site_load(root, &s, &c.c_err) == 0);
	CHECK(s.ss_has_conf);
	props = sh_obj_get(s.ss_merged, "properties");
	dflt = sh_obj_get(props, "default");
	CHECK(dflt->sn_obj.ob_n == 3);
	/* the changed key keeps its place */
	CHECK(strcmp(dflt->sn_obj.ob_v[1].pr_key, "compression") == 0);
	CHECK(strcmp(dflt->sn_obj.ob_v[1].pr_from, "(built-in)") == 0);
	is_str(dflt, "compression", "zstd-9", "shtetl.conf");
	is_str(dflt, "atime", "off", "(built-in)");
	is_str(sh_obj_get(props, "lake"), "atime", "on", "shtetl.conf");
	CHECK(strcmp(sh_obj_find(props, "lake")->pr_from, "shtetl.conf") ==
	    0);
	is_int(sh_obj_get(s.ss_merged, "run"), "keep", 3, "shtetl.conf");
	is_int(sh_obj_get(s.ss_merged, "lake"), "keep", 5, "(built-in)");
	is_str(sh_obj_get(s.ss_merged, "roll"), "depth", "30d",
	    "shtetl.conf");
	/* CS15: the repositories in the order shtetl.conf gives them */
	repos = sh_site_repos(&s);
	CHECK(repos[0] != NULL && strcmp(repos[0], "web") == 0);
	CHECK(repos[1] != NULL && strcmp(repos[1], "site") == 0);
	CHECK(repos[2] == NULL);
	free(repos);
	is_str(sh_obj_get(s.ss_merged, "remotes"), "src",
	    "https://git.freebsd.org/src.git", "(built-in)");
	sh_site_free(&s);

	/* a refused shtetl.conf refuses the load */
	(void) scratch_write("two/shtetl.conf", "lake { keep = 0; }\n");
	(void) snprintf(root, sizeof (root), "%s", scratch_path("two"));
	cap_reset(&c);
	CHECK(sh_site_load(root, &s, &c.c_err) == -1);
	CHECK(s.ss_merged == NULL);
	CHECK(cap_has(&c, "shtetl.conf: lake.keep: must be 1 or more"));
	/* CS11: the default recipe beside the settings, out of their view */
	(void) scratch_write("four/shtetl.conf",
	    "build { make_jobs = 2; }\nver = \">=15.1\";\n"
	    "kernconf = \"GENERIC\";\npkg = [ \"sudo\" ];\n");
	(void) snprintf(root, sizeof (root), "%s", scratch_path("four"));
	cap_reset(&c);
	CHECK(sh_site_load(root, &s, &c.c_err) == 0);
	CHECK(sh_obj_get(s.ss_merged, "ver") == NULL);
	CHECK(sh_obj_get(s.ss_merged, "pkg") == NULL);
	v = sh_obj_get(sh_obj_get(s.ss_merged, "build"), "make_jobs");
	CHECK(v != NULL && v->sn_int == 2);
	sh_site_free(&s);
	(void) scratch_write("five/shtetl.conf", "ver = 15;\n");
	(void) snprintf(root, sizeof (root), "%s", scratch_path("five"));
	cap_reset(&c);
	CHECK(sh_site_load(root, &s, &c.c_err) == -1);
	CHECK(cap_has(&c, "shtetl.conf: ver: must be a quoted string"));
	(void) scratch_write("three/shtetl.conf", ".include \"x\"\n");
	(void) snprintf(root, sizeof (root), "%s", scratch_path("three"));
	cap_reset(&c);
	CHECK(sh_site_load(root, &s, &c.c_err) == -1);
	CHECK(cap_has(&c, "shtetl.conf:1: macros are illegal"));
}

int
main(void)
{
	cap_open(&c);
	test_example();
	test_keys();
	test_properties();
	test_values();
	test_remotes();
	test_load();
	cap_close(&c);
	scratch_clean();
	printf("check_site: %d checks\n", checks);
	return (0);
}
