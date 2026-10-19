/*
 * What show prints: tests/MATRIX.md family CW, every cell
 * (CW1 - CW7). Every case is a round trip: the text printed must
 * parse back, under shtetl's own parse, to the tree it came from.
 */

#include <string.h>

#include "check.h"
#include "core/cascade.h"
#include "core/parse.h"
#include "core/show.h"
#include "core/site.h"
#include "core/tree.h"

static struct cap c;
static struct sh_recipe r;
static char root[1100];
static char *out;
static size_t outlen;

static const char *
mkroot(const char *dir, const char *const *files)
{
	char path[1200];
	size_t i;

	for (i = 0; files[i] != NULL; i += 2) {
		(void) snprintf(path, sizeof (path), "%s/%s", dir, files[i]);
		(void) scratch_write(path, files[i + 1]);
	}
	(void) snprintf(root, sizeof (root), "%s", scratch_path(dir));
	return (root);
}

/* The text printed must parse back to WANT, keys and values alike. */
static void
round_trip(const struct sh_node *want)
{
	struct sh_node *back;

	cap_reset(&c);
	back = sh_parse_buf(out, outlen, "shown", &c.c_err);
	if (back == NULL || !sh_node_equal(back, want))
		printf("round trip failed:\n%s\n%s", out, cap_text(&c));
	CHECK(back != NULL);
	CHECK(sh_node_equal(back, want));
	sh_node_free(back);
}

/* TARGET in ROOT resolved, shown into out, and round-tripped. */
static void
show(const char *rt, const char *target)
{
	FILE *f;

	sh_recipe_free(&r);
	cap_reset(&c);
	if (sh_recipe_resolve(rt, target, &r, &c.c_err, NULL) != 0)
		printf("%s did not resolve:\n%s", target, cap_text(&c));
	CHECK(c.c_err.se_count == 0);
	free(out);
	out = NULL;
	f = open_memstream(&out, &outlen);
	CHECK(f != NULL);
	sh_show_recipe(f, &r);
	CHECK(fclose(f) == 0);
	round_trip(r.sr_merged);
}

static int
has(const char *s)
{
	if (strstr(out, s) == NULL) {
		printf("not in the output: \"%s\"\n%s", s, out);
		return (0);
	}
	return (1);
}

/* CW1, CW2, CW5: the worked example and a changed field. */
static void
test_recipes(void)
{
	static const char *const ex[] = {
		"shtetl.conf",
		"pkg = [ \"sudo\", \"tmux\" ];\n"
		"datasets { tmp {} var {} \"usr/local\" {} }\n",
		"common/web.conf",
		"ver = \">=15\";\n"
		"pkg = [ \"apache24\", \"py311-certbot\" ];\n"
		"datasets { www { readonly = true; compression = \"lz4\"; } }\n"
		"commands { certbot = \"sysrc certbot_enable=YES\"; }\n",
		"containers/nginx.conf",
		"import = [ \"common/web.conf\" ];\n"
		"ver = \"<16\";\n"
		"pkg { nginx = true; }\n"
		"datasets { www { compression = \"zstd\"; } }\n"
		"commands { \"c.d\" = \"x\"; }\n",
		"foundations/f.conf",
		"src_branch = \"main\";\n"
		"patches { \"github.com/a/b\" { ref = \"r\"; dir = \"15\"; } "
		"\"https://x.org/p.git\" = true; }\n"
		"buildopts = \"A=1\\nB=2\";\n",
		NULL
	};
	static const char *const em[] = {
		"systems/e.conf", "pkg {}\ndatasets {}\n",
		NULL
	};

	show(mkroot("ex", ex), "containers/nginx");
	CHECK(has("# containers/nginx.conf: a container's recipe, its layers "
	    "merged\n"));
	CHECK(has("# layers, nearest last: shtetl.conf, common/web.conf, "
	    "containers/nginx.conf\n"));
	/* CW2 */
	CHECK(has("\n    sudo = true;  # shtetl.conf\n"));
	CHECK(has("\n    nginx = true;  # containers/nginx.conf\n"));
	CHECK(has("\n    www {  # common/web.conf\n"
	    "        readonly = true;  # common/web.conf\n"
	    "        compression = \"zstd\";  # containers/nginx.conf, over "
	    "common/web.conf\n    }\n"));
	CHECK(has("\n    apache24 = true;  # common/web.conf\n"));
	/* CW8: a list that stacks, each value with its layer */
	CHECK(has("\nver = [\n    \">=15\",  # common/web.conf\n"
	    "    \"<16\"  # containers/nginx.conf\n]\n"));
	/* CW5 */
	CHECK(has("\n    \"usr/local\" {}  # shtetl.conf\n"));
	CHECK(has("\n    py311-certbot = true;"));
	CHECK(has("\n    \"c.d\" {  # containers/nginx.conf\n"
	    "        \"containers/nginx.conf\" = \"x\";\n    }\n"));

	/* CW1 on a foundation; CW5's URLs; CW8, text a piece per file */
	show(root, "foundations/f");
	CHECK(has("\n    \"github.com/a/b\" {  # foundations/f.conf\n"));
	CHECK(has("\n    \"https://x.org/p.git\" = true;"));
	CHECK(has("\nbuildopts {\n    \"foundations/f.conf\" = <<EOD\n"
	    "A=1\nB=2\nEOD\n}\n"));

	/* CW6: a collection written empty */
	show(mkroot("em", em), "systems/e");
	CHECK(has("\npkg {}\n"));
	CHECK(has("\ndatasets {}\n"));
}

/* CW3, CW4: escapes and heredocs come back as they went. */
static void
test_strings(void)
{
	static const char *const st[] = {
		"containers/s.conf",
		"commands {\n"
		"    q = \"say \\\"hi\\\" \\\\ back\\ttab\\u0001 \\r end\";\n"
		"    two = <<EOD\nline one\n  line two\nEOD\n"
		"    nl = \"ends in a newline\\n\";\n"
		"    only = \"\\n\";\n"
		"    clash = \"a\\nEOD\\nb\";\n"
		"    clash2 = \"a\\nEOD;\\nEODX\\nEODXX,\\nb\";\n"
		"    term = \"EOD\\nfirst\";\n"
		"    high = \"caf\\u00e9\";\n"
		"}\n",
		NULL
	};
	const struct sh_node *cmd;

	show(mkroot("st", st), "containers/s");
	/* each block's piece, keyed by its file */
	cmd = sh_obj_get(r.sr_merged, "commands");
	CHECK(strcmp(sh_obj_get(sh_obj_get(cmd, "q"), "containers/s.conf")->
	    sn_str, "say \"hi\" \\ back\ttab\001 \r end") == 0);
	CHECK(has("\"containers/s.conf\" = \"say \\\"hi\\\" \\\\ "
	    "back\\ttab\\u0001 \\r end\";"));
	CHECK(has("    two {  # containers/s.conf\n"
	    "        \"containers/s.conf\" = <<EOD\nline one\n"
	    "  line two\nEOD\n    }\n"));
	CHECK(has("\"containers/s.conf\" = <<EOD\nends in a newline\n\nEOD\n"));
	CHECK(has("\"containers/s.conf\" = <<EOD\n\n\nEOD\n"));
	CHECK(has("\"containers/s.conf\" = <<EODX\na\nEOD\nb\nEODX\n"));
	CHECK(has("\"containers/s.conf\" = <<EODXXX\n"));
	CHECK(has("\"containers/s.conf\" = <<EODX\nEOD\nfirst\nEODX\n"));
	CHECK(strcmp(sh_obj_get(sh_obj_get(cmd, "high"), "containers/s.conf")->
	    sn_str, "caf\303\251") == 0);
}

/* CW7: the site's settings. */
static void
test_site(void)
{
	struct sh_site s;
	FILE *f;

	(void) scratch_write("site/shtetl.conf",
	    "properties { default { compression = \"zstd-9\"; } }\n"
	    "roll { depth = \"30d\"; }\n");
	cap_reset(&c);
	CHECK(sh_site_load(scratch_path("site"), &s, &c.c_err) == 0);
	free(out);
	out = NULL;
	f = open_memstream(&out, &outlen);
	CHECK(f != NULL);
	sh_show_site(f, &s);
	CHECK(fclose(f) == 0);
	round_trip(s.ss_merged);
	CHECK(has("# the site's settings: shtetl.conf over the built-in "
	    "ones\nproperties {\n    default {\n"));
	CHECK(has("\n    depth = \"30d\";  # shtetl.conf, over "
	    "(built-in)\n"));
	CHECK(has("\n        atime = \"off\";  # (built-in)\n"));
	CHECK(has("\n        compression = \"zstd-9\";  # shtetl.conf, over "
	    "(built-in)\n"));
	sh_site_free(&s);
}

int
main(void)
{
	cap_open(&c);
	test_recipes();
	test_strings();
	test_site();
	sh_recipe_free(&r);
	free(out);
	cap_close(&c);
	scratch_clean();
	printf("check_show: %d checks\n", checks);
	return (0);
}
