/*
 * shtetl.conf (planning.md 5.2, 6.3): tests/MATRIX.md family CS, every
 * cell (CS1 - CS10).
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

/* CS1: 5.2's example, as the plan writes it. */
static void
test_example(void)
{
	ok("spaces {\n"
	    "    lake { default = \"tank/shtetl\"; }   # several lake roots\n"
	    "    run  { default = \"fast/shtetl/run\"; }\n"
	    "}\n"
	    "properties {\n"
	    "    default { compression = \"zstd\"; }\n"
	    "    lake    { compression = \"zstd-9\"; }\n"
	    "}\n"
	    "build {\n"
	    "    make_jobs = 0;                  # 0 = hw.ncpu\n"
	    "}\n"
	    "update {\n"
	    "    suffix = \"staging\";             # X-staging\n"
	    "    depth = 0;\n"
	    "}\n"
	    "retention {                         # 6.3; 0 = keep everything\n"
	    "    run     = 10;\n"
	    "    archive = \"1y\";\n"
	    "}\n");
}

/* CS2, CS3, CS10: sections and keys, spaces. */
static void
test_keys(void)
{
	bad("frob { }", "shtetl.conf: frob: not a key of shtetl.conf: it "
	    "takes spaces, properties, build, update, retention");
	bad("build { jobs = 1; }", "build.jobs: not a key of build: it takes "
	    "make_jobs");
	bad("build = 4;", "build: expected an object, not an integer");
	/* CS3 */
	bad("spaces { config { default = \"tank/c\"; } }", "spaces.config: "
	    "not a key of spaces: it takes lake, run");
	bad("spaces { build { default = \"tank/b\"; } }", "not a key of "
	    "spaces");
	bad("spaces { lake { other = 1; } }", "spaces.lake.other: not a key "
	    "of spaces.lake: it takes default");
	bad("spaces { lake { default = \"/tank\"; } }", "spaces.lake.default: "
	    "not a dataset's name: it begins with '/'");
	bad("spaces { run { default = \"1tank/x\"; } }", "a pool's name "
	    "begins with a letter");
	bad("spaces { run { default = 5; } }", "expected a dataset's name, "
	    "not an integer");
	ok("spaces { lake { default = \"tank\"; } }");
	/* CS10 */
	bad("retention { lake = 5; }", "retention.lake: not a key of "
	    "retention: it takes run, archive");
}

/* CS4: properties. */
static void
test_properties(void)
{
	ok("properties { lake { atime = \"on\"; } build { exec = false; } "
	    "config { \"com.example:x\" = \"y\"; } run { atime = \"off\"; } "
	    "}");
	bad("properties { bogus { } }", "properties.bogus: not a key of "
	    "properties: it takes default, lake, run, config, build");
	bad("properties { default { recordsize = 1M; } }",
	    "properties.default.recordsize: expected a string, or true or "
	    "false, not an integer: quote it");
	bad("properties { run { mountpoint = \"/x\"; } }", "not a property "
	    "the configuration may set: shtetl sets it itself");
	bad("properties { run { canmount = \"on\"; } }", "shtetl sets it "
	    "itself");
	bad("properties { lake = \"zstd\"; }", "properties.lake: expected an "
	    "object of properties, not a string");
}

/* CS5, CS6, CS7: counts, the suffix, the policies of 6.3. */
static void
test_values(void)
{
	static const char *const keys[] = { "retention { run = %s; }",
	    "retention { archive = %s; }", "update { depth = %s; }" };
	static const char *const good[] = { "0", "10", "\"7d\"", "\"6m\"",
	    "\"1y\"", "\"12H\"", "\"30M\"", "\"2w\"", "\"90S\"" };
	static const char *const poor[] = { "\"7x\"", "\"d7\"", "\"\"",
	    "\"0d\"", "\"7dd\"", "\"-1d\"" };
	char text[128];
	size_t i, j;

	ok("build { make_jobs = 8; }");
	bad("build { make_jobs = -1; }", "build.make_jobs: a count is 0 or "
	    "more, not -1");
	bad("build { make_jobs = \"4\"; }", "expected a count, 0 or more, not "
	    "a string");
	bad("build { make_jobs = 1.5; }", "expected a count, 0 or more, not "
	    "a number");
	/* CS6 */
	ok("update { suffix = \"soak\"; }");
	bad("update { suffix = \"\"; }", "update.suffix: not a name: empty");
	bad("update { suffix = \"a.b\"; }", "not a name: it takes only "
	    "letters, digits, '-' and '_'");
	bad("update { suffix = true; }", "expected a name, not true");
	/* CS7 */
	for (i = 0; i < sizeof (keys) / sizeof (keys[0]); i++) {
		for (j = 0; j < sizeof (good) / sizeof (good[0]); j++) {
			(void) snprintf(text, sizeof (text), keys[i], good[j]);
			ok(text);
		}
		for (j = 0; j < sizeof (poor) / sizeof (poor[0]); j++) {
			(void) snprintf(text, sizeof (text), keys[i], poor[j]);
			bad(text, "not an interval: an interval is a count "
			    "above 0");
		}
		(void) snprintf(text, sizeof (text), keys[i], "-1");
		bad(text, "a count is 0 or more, not -1");
		(void) snprintf(text, sizeof (text), keys[i], "1.5");
		bad(text, "expected a count or a quoted interval, not a "
		    "number");
		(void) snprintf(text, sizeof (text), keys[i], "30d");
		bad(text, "unquoted, UCL reads 30d as seconds; quote it, "
		    "\"30d\"");
		(void) snprintf(text, sizeof (text), keys[i], "true");
		bad(text, "expected a count, 0 or more, or a quoted "
		    "interval, not true");
	}
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

/* CS8, CS9: the built-in settings, and shtetl.conf over them. */
static void
test_load(void)
{
	struct sh_site s;
	const struct sh_node *props, *dflt, *v;
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
	is_str(sh_obj_get(s.ss_merged, "update"), "suffix", "staging",
	    "(built-in)");
	v = sh_obj_get(sh_obj_get(s.ss_merged, "retention"), "run");
	CHECK(v != NULL && v->sn_type == SH_INT && v->sn_int == 0);
	v = sh_obj_get(sh_obj_get(s.ss_merged, "build"), "make_jobs");
	CHECK(v != NULL && v->sn_type == SH_INT && v->sn_int == 0);
	v = sh_obj_get(s.ss_merged, "spaces");
	CHECK(v != NULL && v->sn_type == SH_OBJECT && v->sn_obj.ob_n == 0);
	CHECK(strcmp(s.ss_merged->sn_obj.ob_v[0].pr_key, "spaces") == 0);
	sh_site_free(&s);

	/* shtetl.conf sets one property and adds a section */
	(void) scratch_write("one/shtetl.conf",
	    "properties {\n"
	    "    default { compression = \"zstd-9\"; }\n"
	    "    lake { atime = \"on\"; }\n"
	    "}\n"
	    "retention { archive = \"1y\"; }\n");
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
	is_str(sh_obj_get(s.ss_merged, "retention"), "archive", "1y",
	    "shtetl.conf");
	v = sh_obj_get(sh_obj_get(s.ss_merged, "retention"), "run");
	CHECK(v->sn_type == SH_INT && v->sn_int == 0);
	CHECK(strcmp(v->sn_from, "(built-in)") == 0);
	sh_site_free(&s);

	/* a refused shtetl.conf refuses the load */
	(void) scratch_write("two/shtetl.conf", "retention { lake = 1; }\n");
	(void) snprintf(root, sizeof (root), "%s", scratch_path("two"));
	cap_reset(&c);
	CHECK(sh_site_load(root, &s, &c.c_err) == -1);
	CHECK(s.ss_merged == NULL);
	CHECK(cap_has(&c, "shtetl.conf: retention.lake: not a key"));
	(void) scratch_write("three/shtetl.conf", ".include \"x\"\n");
	(void) snprintf(root, sizeof (root), "%s", scratch_path("three"));
	cap_reset(&c);
	CHECK(sh_site_load(root, &s, &c.c_err) == -1);
	CHECK(cap_has(&c, "shtetl.conf:1: a UCL macro"));
}

int
main(void)
{
	cap_open(&c);
	test_example();
	test_keys();
	test_properties();
	test_values();
	test_load();
	cap_close(&c);
	scratch_clean();
	printf("check_site: %d checks\n", checks);
	return (0);
}
