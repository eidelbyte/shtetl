/*
 * The parse of one file under planning.md 5.3: tests/MATRIX.md family
 * CP, every cell (CP1 - CP25), against whichever libucl is linked.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "core/parse.h"
#include "core/tree.h"

static struct cap c;

/* TEXT parsed as t.conf. */
static struct sh_node *
P(const char *text)
{
	return (sh_parse_buf(text, strlen(text), "t.conf", &c.c_err));
}

/* TEXT refused, with one message holding WANT. */
static void
refused(const char *text, const char *want)
{
	struct sh_node *n;

	cap_reset(&c);
	n = P(text);
	if (n != NULL || c.c_err.se_count != 1 || !cap_has(&c, want)) {
		printf("refused(\"%s\"): wanted one message with \"%s\", "
		    "got %s:\n%s", text, want, n != NULL ? "a tree" : "none",
		    cap_text(&c));
	}
	CHECK(n == NULL);
	CHECK(c.c_err.se_count == 1);
	CHECK(cap_has(&c, want));
}

static const struct sh_node *
get(const struct sh_node *o, const char *key, enum sh_type type)
{
	const struct sh_node *v;

	v = sh_obj_get(o, key);
	CHECK(v != NULL);
	CHECK(v->sn_type == type);
	return (v);
}

/* CP23: the linked libucl keeps the three rules shtetl leans on. */
static void
test_selfcheck(void)
{
	cap_reset(&c);
	CHECK(sh_parse_selfcheck(&c.c_err) == 0);
	CHECK(c.c_err.se_count == 0);
}

/* CP1, CP2: an empty file is no keys; the top must be keys. */
static void
test_shape(void)
{
	struct sh_node *n;

	cap_reset(&c);
	n = P("");
	CHECK(n != NULL && n->sn_type == SH_OBJECT && n->sn_obj.ob_n == 0);
	sh_node_free(n);
	n = P("# a comment and nothing else\n");
	CHECK(n != NULL && n->sn_type == SH_OBJECT && n->sn_obj.ob_n == 0);
	sh_node_free(n);
	CHECK(c.c_err.se_count == 0);

	refused("[1, 2]", "t.conf: the file must hold keys, not a list");
}

/* CP3, CP4: every scalar kind, typed as UCL types it. */
static void
test_scalars(void)
{
	struct sh_node *n;

	cap_reset(&c);
	n = P("b1 = true; b2 = false; b3 = yes; b4 = off;\n"
	    "i1 = 10; i2 = 0x10; i3 = 1k; f = 15.10;\n"
	    "s1 = \"a string\"; s2 = bare; z = null;\n"
	    "t1 = 30d; t2 = 10min;\n");
	CHECK(n != NULL);
	CHECK(get(n, "b1", SH_BOOL)->sn_bool);
	CHECK(!get(n, "b2", SH_BOOL)->sn_bool);
	CHECK(get(n, "b3", SH_BOOL)->sn_bool);
	CHECK(!get(n, "b4", SH_BOOL)->sn_bool);
	CHECK(get(n, "i1", SH_INT)->sn_int == 10);
	CHECK(get(n, "i2", SH_INT)->sn_int == 16);
	CHECK(get(n, "i3", SH_INT)->sn_int == 1000);
	CHECK(get(n, "f", SH_NUM)->sn_num == 15.1);
	CHECK(strcmp(get(n, "s1", SH_STR)->sn_str, "a string") == 0);
	CHECK(strcmp(get(n, "s2", SH_STR)->sn_str, "bare") == 0);
	(void) get(n, "z", SH_NULL);
	/* CP4: never the string "30d" */
	CHECK(get(n, "t1", SH_NUM)->sn_num == 30.0 * 86400);
	CHECK(get(n, "t2", SH_NUM)->sn_num == 600.0);
	sh_node_free(n);
}

/* CP5, CP16: a heredoc is verbatim, and $ is literal everywhere. */
static void
test_verbatim(void)
{
	struct sh_node *n;

	cap_reset(&c);
	n = P("h = <<EOD\n"
	    "line \\\"one\\\" \\n\n"
	    ".include \"x\"\n"
	    "$FOO ${CURDIR}\n"
	    "EOD\n"
	    "a = \"$FOO ${CURDIR} $FILENAME\";\n");
	CHECK(n != NULL);
	CHECK(strcmp(get(n, "h", SH_STR)->sn_str,
	    "line \\\"one\\\" \\n\n.include \"x\"\n$FOO ${CURDIR}") == 0);
	CHECK(strcmp(get(n, "a", SH_STR)->sn_str,
	    "$FOO ${CURDIR} $FILENAME") == 0);
	sh_node_free(n);
}

/* CP6, CP10: nesting, and keys in the order written, case and all. */
static void
test_nesting(void)
{
	struct sh_node *n;
	const struct sh_node *a, *y;

	cap_reset(&c);
	n = P("z = 1;\na { y = [1, { k = \"v\" }]; x = 2; }\nm = 3;\n"
	    "K = 1; k = 2;\n");
	CHECK(n != NULL && n->sn_obj.ob_n == 5);
	CHECK(strcmp(n->sn_obj.ob_v[0].pr_key, "z") == 0);
	CHECK(strcmp(n->sn_obj.ob_v[1].pr_key, "a") == 0);
	CHECK(strcmp(n->sn_obj.ob_v[2].pr_key, "m") == 0);
	a = get(n, "a", SH_OBJECT);
	CHECK(a->sn_obj.ob_n == 2);
	CHECK(strcmp(a->sn_obj.ob_v[0].pr_key, "y") == 0);
	CHECK(strcmp(a->sn_obj.ob_v[1].pr_key, "x") == 0);
	y = get(a, "y", SH_ARRAY);
	CHECK(y->sn_arr.ar_n == 2);
	CHECK(y->sn_arr.ar_v[0]->sn_type == SH_INT);
	CHECK(y->sn_arr.ar_v[1]->sn_type == SH_OBJECT);
	CHECK(strcmp(get(y->sn_arr.ar_v[1], "k", SH_STR)->sn_str, "v") == 0);
	/* CP10 */
	CHECK(get(n, "K", SH_INT)->sn_int == 1);
	CHECK(get(n, "k", SH_INT)->sn_int == 2);
	sh_node_free(n);
}

/* CP7, CP8, CP9: a key written twice, at any depth, is refused. */
static void
test_duplicates(void)
{
	refused("a = 1;\nb = 2;\na = 3;\n", "line: 3");
	CHECK(cap_has(&c, "t.conf"));
	CHECK(cap_has(&c, "duplicate"));
	refused("a { x = 1; }\nb = 2;\na { y = 3; }\n", "line: 3");
	CHECK(cap_has(&c, "duplicate"));
	refused("o {\n a = 1;\n a = 2;\n}\n", "line: 3");
	CHECK(cap_has(&c, "duplicate"));
}

/*
 * CP11 - CP14: a macro, of any name and at any depth, is refused at
 * its line. The included file exists, so a parse that went on would
 * have read it.
 */
static void
test_macros(void)
{
	char text[4096];
	const char *inc;

	inc = scratch_write("inc.conf", "included = true;\n");
	(void) snprintf(text, sizeof (text), "x = 1;\n.include \"%s\"\n", inc);
	refused(text, "t.conf:2: a UCL macro");
	(void) snprintf(text, sizeof (text), "o {\n  x = 1;\n  .priority 5\n"
	    "}\n");
	refused(text, "t.conf:3: a UCL macro");
	(void) snprintf(text, sizeof (text), ".try_include \"%s\"\n", inc);
	refused(text, "t.conf:1: a UCL macro");
	(void) snprintf(text, sizeof (text), "a = 1;\n.includes \"%s\"\n",
	    inc);
	refused(text, "t.conf:2: a UCL macro");
	(void) snprintf(text, sizeof (text), ".load(key=\"k\") \"%s\"\n", inc);
	refused(text, "t.conf:1: a UCL macro");
	refused("o { a = 1; }\np {\n  .inherit \"o\"\n}\n",
	    "t.conf:3: a UCL macro");
	refused("x = 1;\n.foo bar\n", "t.conf:2: a UCL macro");
}

/*
 * CP15: where no macro can stand, .include is text. An unquoted array
 * element runs to the end of its line, so the first is one string.
 */
static void
test_not_macros(void)
{
	struct sh_node *n;
	const struct sh_node *a;

	cap_reset(&c);
	n = P("a [\n .include \"x\"\n]\nb = .include;\n"
	    "c = [ \".include\", .include ];\n");
	CHECK(n != NULL);
	a = get(n, "a", SH_ARRAY);
	CHECK(a->sn_arr.ar_n == 1);
	CHECK(a->sn_arr.ar_v[0]->sn_type == SH_STR);
	CHECK(strcmp(a->sn_arr.ar_v[0]->sn_str, ".include \"x\"") == 0);
	CHECK(strcmp(get(n, "b", SH_STR)->sn_str, ".include") == 0);
	a = get(n, "c", SH_ARRAY);
	CHECK(a->sn_arr.ar_n == 2);
	CHECK(strcmp(a->sn_arr.ar_v[0]->sn_str, ".include") == 0);
	CHECK(strcmp(a->sn_arr.ar_v[1]->sn_str, ".include") == 0);
	sh_node_free(n);
}

/* CP17, CP18, CP25: syntax, NUL bytes, nesting too deep. */
static void
test_bad_input(void)
{
	static const char nul[] = "a = 1;\nb = \"x\0y\";\n";
	struct sh_node *n;
	char deep[512];
	size_t i;

	refused("a = {\n b = 1;\n", "t.conf");

	cap_reset(&c);
	n = sh_parse_buf(nul, sizeof (nul) - 1, "t.conf", &c.c_err);
	CHECK(n == NULL);
	CHECK(c.c_err.se_count == 1);
	CHECK(cap_has(&c, "t.conf:2: a NUL byte"));
	/* the same byte by escape, which libucl may unescape */
	cap_reset(&c);
	n = P("a = \"x\\u0000y\";\n");
	CHECK(n == NULL);
	CHECK(c.c_err.se_count == 1);
	CHECK(cap_has(&c, "t.conf"));

	/* CP25: 65 objects deep, past SH_PARSE_DEPTH */
	deep[0] = '\0';
	for (i = 0; i < SH_PARSE_DEPTH; i++)
		(void) strcat(deep, "a { ");
	(void) strcat(deep, "b = 1;");
	for (i = 0; i < SH_PARSE_DEPTH; i++)
		(void) strcat(deep, "}");
	refused(deep, "nested more than 64 deep");
	CHECK(cap_has(&c, "t.conf: a.a.a."));
}

/* CP19 - CP22: files: missing, a directory, too large, and a good one. */
static void
test_files(void)
{
	static const char *name = "containers/x.conf";
	struct sh_node *n;
	const struct sh_node *o;
	size_t i;
	int fd;

	cap_reset(&c);
	CHECK(sh_parse_file(scratch_path("nope.conf"), "nope.conf",
	    &c.c_err) == NULL);
	CHECK(c.c_err.se_count == 1);
	CHECK(cap_has(&c, "nope.conf: No such file or directory"));

	cap_reset(&c);
	CHECK(mkdir(scratch_path("d.conf"), 0755) == 0);
	CHECK(sh_parse_file(scratch_path("d.conf"), "d.conf",
	    &c.c_err) == NULL);
	CHECK(cap_has(&c, "d.conf: not a regular file"));

	cap_reset(&c);
	fd = open(scratch_path("big.conf"), O_WRONLY | O_CREAT, 0644);
	CHECK(fd != -1);
	CHECK(ftruncate(fd, SH_PARSE_MAX + 1) == 0);
	(void) close(fd);
	CHECK(sh_parse_file(scratch_path("big.conf"), "big.conf",
	    &c.c_err) == NULL);
	CHECK(cap_has(&c, "big.conf: larger than 4 MiB"));
	/* exactly the limit is read (and is a NUL byte at line 1) */
	cap_reset(&c);
	CHECK(truncate(scratch_path("big.conf"), SH_PARSE_MAX) == 0);
	CHECK(sh_parse_file(scratch_path("big.conf"), "big.conf",
	    &c.c_err) == NULL);
	CHECK(cap_has(&c, "big.conf:1: a NUL byte"));

	/* CP22: every node and key of a parsed file is from that file */
	cap_reset(&c);
	n = sh_parse_file(scratch_write("x.conf",
	    "pkg = [ \"a\", \"b\" ];\ndatasets { www { readonly = true; } }\n"),
	    name, &c.c_err);
	CHECK(n != NULL);
	CHECK(c.c_err.se_count == 0);
	CHECK(n->sn_from == name);
	for (i = 0; i < n->sn_obj.ob_n; i++) {
		CHECK(n->sn_obj.ob_v[i].pr_from == name);
		CHECK(n->sn_obj.ob_v[i].pr_val->sn_from == name);
	}
	o = get(n, "pkg", SH_ARRAY);
	for (i = 0; i < o->sn_arr.ar_n; i++)
		CHECK(o->sn_arr.ar_v[i]->sn_from == name);
	o = get(get(n, "datasets", SH_OBJECT), "www", SH_OBJECT);
	CHECK(o->sn_from == name);
	CHECK(o->sn_obj.ob_v[0].pr_from == name);
	CHECK(o->sn_obj.ob_v[0].pr_val->sn_from == name);
	sh_node_free(n);
}

int
main(void)
{
	cap_open(&c);
	test_selfcheck();
	test_shape();
	test_scalars();
	test_verbatim();
	test_nesting();
	test_duplicates();
	test_macros();
	test_not_macros();
	test_bad_input();
	test_files();
	cap_close(&c);
	scratch_clean();
	printf("check_parse: %d checks\n", checks);
	return (0);
}
