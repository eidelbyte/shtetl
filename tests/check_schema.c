/*
 * One recipe layer against its kind (planning.md 5.5): tests/MATRIX.md
 * family CK, every cell (CK1 - CK20).
 */

#include <string.h>

#include "check.h"
#include "core/parse.h"
#include "core/schema.h"
#include "core/syntax.h"
#include "core/tree.h"

#define	F	SH_FOUNDATION
#define	C	SH_CONTAINER
#define	S	SH_SYSTEM

static struct cap c;

/* TEXT parsed as t.conf and checked as a KIND layer: the count. */
static unsigned
L(const char *text, enum sh_kind kind)
{
	struct sh_node *n;
	unsigned bad;

	cap_reset(&c);
	n = sh_parse_buf(text, strlen(text), "t.conf", &c.c_err);
	if (n == NULL) {
		printf("did not parse: %s\n%s", text, cap_text(&c));
		CHECK(n != NULL);
	}
	bad = sh_schema_recipe(n, kind, "t.conf", &c.c_err);
	sh_node_free(n);
	return (bad);
}

static void
ok(const char *text, enum sh_kind kind)
{
	if (L(text, kind) != 0)
		printf("ok(\"%s\") as a %s:\n%s", text, sh_kind_name(kind),
		    cap_text(&c));
	CHECK(c.c_err.se_count == 0);
}

/* Refused with exactly one message, holding WANT. */
static void
bad(const char *text, enum sh_kind kind, const char *want)
{
	unsigned n;

	n = L(text, kind);
	if (n != 1 || !cap_has(&c, want))
		printf("bad(\"%s\") as a %s: wanted one message with \"%s\", "
		    "got %u:\n%s", text, sh_kind_name(kind), want, n,
		    cap_text(&c));
	CHECK(n == 1);
	CHECK(c.c_err.se_count == 1);
	CHECK(cap_has(&c, want));
}

/* CK1: every key of 5.5 in each kind that has it. */
static void
test_every_key(void)
{
	ok("import = [ \"common/base.conf\" ];\n"
	    "ver = \">=15\";\n"
	    "src_branch = \"releng/15.1\";\n"
	    "kernconf_file = \"GENERIC\";\n"
	    "kernconf_lines = <<EOD\nnooptions INVARIANTS\nEOD\n"
	    "buildopts = <<EOD\nWITHOUT_LIB32=yes\nEOD\n"
	    "patches = [ \"github.com/eidelbyte/freebsd-patches\" ];\n"
	    "datasets { tmp {} var {} \"usr/local\" { compression = "
	    "\"zstd-9\"; } }\n"
	    "pkg = [ \"sudo\", \"tmux\" ];\n"
	    "commands { motd = \"echo hi > /etc/motd\"; }\n"
	    "derivations { \"etc/pwd.db\" { source = \"etc/master.passwd\"; "
	    "command = \"pwd_mkdb -d etc etc/master.passwd\"; } }\n", F);
	ok("kernconf_file = \"kernels/GAMING\";\n", F);
	ok("import = \"common/web.conf\";\n"
	    "ver = \"=15.1\";\n"
	    "pkg { nginx = true; }\n"
	    "datasets { www { readonly = true; } }\n"
	    "commands { nginx = <<EOD\nsysrc nginx_enable=YES\nEOD\n}\n"
	    "derivations { x { source = \"y\"; command = \"true\"; } }\n"
	    "requires = [ \"db\" ];\n"
	    "jailconf = \"allow.raw_sockets;\";\n", C);
	ok("import = [ \"common/base.conf\" ];\n"
	    "ver = \"<16\";\n"
	    "datasets { home {} }\n"
	    "pkg { zsh = true; }\n"
	    "commands { hello = \"echo\"; }\n"
	    "derivations { \"etc/login.conf.db\" { source = "
	    "\"etc/login.conf\"; command = \"cap_mkdb etc/login.conf\"; } "
	    "}\n", S);
}

/* CK2, CK3, CK4: unknown keys, and keys of another kind. */
static void
test_kinds(void)
{
	static const char *const fonly[] = { "src_branch = \"main\";",
	    "kernconf_lines = \"G\";", "kernconf_file = \"k\";",
	    "buildopts = \"x\";",
	    "patches = [ \"p/q\" ];" };
	char want[128];
	size_t i;

	bad("frobnicate = 1;\n", F, "t.conf: frobnicate: invalid key");
	bad("Pkg = [ \"a\" ];\n", C, "t.conf: Pkg: invalid key");
	for (i = 0; i < sizeof (fonly) / sizeof (fonly[0]); i++) {
		(void) snprintf(want, sizeof (want), "t.conf: %.*s: valid only "
		    "in foundation recipe", (int)strcspn(fonly[i], " "),
		    fonly[i]);
		bad(fonly[i], C, want);
		bad(fonly[i], S, want);
	}
	bad("requires = [ \"db\" ];", F, "t.conf: requires: valid only in "
	    "container recipe");
	bad("requires = [ \"db\" ];", S, "requires: valid only in container "
	    "recipe");
	bad("jailconf = \"x\";", S, "jailconf: valid only in container "
	    "recipe");
}

/* CK5, CK6: strings and versions. */
static void
test_strings(void)
{
	static const char *const good[] = { ">=15", "=15.1", "<16",
	    "<=15.1", ">14", ">=15.10" };
	static const char *const poor[] = { "", "15.1", ">=", "=>15",
	    ">=15.x", ">=15.", ">= 15", ">=15.1.2" };
	char text[64];
	size_t i;

	bad("src_branch = 15;", F, "src_branch: must be a quoted string");
	bad("kernconf_lines = true;", F, "kernconf_lines: must be a quoted "
	    "string");
	bad("ver = 15.1;", C, "ver: must be a quoted string");
	bad("jailconf = [ \"a\" ];", C, "jailconf: must be a string");
	bad("src_branch = \"\";", F, "src_branch: empty");
	for (i = 0; i < sizeof (good) / sizeof (good[0]); i++) {
		(void) snprintf(text, sizeof (text), "ver = \"%s\";", good[i]);
		ok(text, S);
	}
	for (i = 0; i < sizeof (poor) / sizeof (poor[0]); i++) {
		(void) snprintf(text, sizeof (text), "ver = \"%s\";", poor[i]);
		bad(text, S, poor[i][0] == '\0' ? "ver: empty" :
		    "ver: invalid version constraint: ");
	}
}

/* CK7, CK8: kernconf_file and kernconf_lines, and paths. */
static void
test_files(void)
{
	ok("kernconf_file = \"GENERIC\"; kernconf_lines = \"device vmm\";",
	    F);
	ok("kernconf_file = \"kernels/GAMING\";", F);
	bad("kernconf_file = \"/etc/GAMING\";", F, "kernconf_file: invalid "
	    "path: absolute path");
	bad("kernconf_file = \"a/../b\";", F, "invalid path: . or .. "
	    "component");
	bad("kernconf_file = \"./b\";", F, "invalid path: . or .. component");
	bad("kernconf_file = \"a//b\";", F, "invalid path: empty path "
	    "component");
	bad("kernconf_file = \"a/\";", F, "invalid path: trailing /");
	bad("kernconf_file = 5;", F, "kernconf_file: must be a quoted "
	    "string");
}

/* CK9: import. */
static void
test_import(void)
{
	ok("import = \"x.conf\";", C);
	ok("import = [ \"a.conf\", \"b/c.conf\" ];", C);
	bad("import = [ \"a.conf\", 1 ];", C, "import[1]: must be a quoted "
	    "string");
	bad("import = [ \"/x.conf\" ];", C, "import[0]: invalid path: "
	    "absolute path");
	bad("import { a = 1; }", C, "import: must be a path or a list of "
	    "paths");
}

/* CK11, CK18: pkg, and collections given as scalars. */
static void
test_pkg(void)
{
	ok("pkg = [ \"a\", \"py311-certbot\", \"www/nginx\", \"gtk+3\" ];",
	    C);
	ok("pkg { a = true; b = true; }", C);
	bad("pkg { a = true; b = false; }", C, "pkg.b: dropping values is "
	    "illegal");
	bad("pkg { a { x = 1; } }", C, "pkg.a: must be true");
	bad("pkg { a = \"yes\"; }", C, "pkg.a: must be true");
	bad("pkg = [ \"\" ];", C, "pkg[0]: invalid package name: empty");
	bad("pkg = [ \"-x\" ];", C, "pkg[0]: invalid package name: leading -");
	bad("pkg = [ \"a b\" ];", C, "invalid package name: invalid "
	    "character");
	bad("pkg = [ \"a\", \"b\", \"a\" ];", C, "pkg[2]: duplicate entry "
	    "\"a\"");
	bad("pkg = [ 5 ];", C, "pkg[0]: must be a quoted string");
	/* CK18 */
	bad("pkg = \"a\";", C, "pkg: must be an object or a list");
	bad("datasets = 5;", F, "datasets: must be an object or a list");
	bad("commands = \"x\";", F, "commands: must be an object");
}

/* CK12, CK13: dataset paths, and their properties. */
static void
test_datasets(void)
{
	ok("datasets = [ \"tmp\", \"usr/local\", \"var/db:x\", "
	    "\"a b\" ];", F);
	bad("datasets = [ \"/usr\" ];", F, "datasets[0]: invalid dataset "
	    "path: leading /");
	bad("datasets = [ \"usr/\" ];", F, "trailing /");
	bad("datasets = [ \"a//b\" ];", F, "empty path component");
	bad("datasets = [ \".\" ];", F, ". or .. component");
	bad("datasets = [ \"a/../b\" ];", F, ". or .. component");
	bad("datasets { \"a@b\" {} }", F, "datasets.\"a@b\": invalid dataset "
	    "path: invalid character");
	bad("datasets = [ \"a#b\" ];", F, "invalid dataset path: invalid "
	    "character");

	ok("datasets { www { readonly = true; compression = \"lz4\"; "
	    "recordsize = \"1M\"; \"com.example:x\" = \"y\"; "
	    "\"userquota@www\" = \"1G\"; } }", C);
	bad("datasets { www { recordsize = 1M; } }", C, "datasets.www."
	    "recordsize: must be a quoted string");
	bad("datasets { www { mountpoint = \"/x\"; } }", C, "datasets.www."
	    "mountpoint: invalid property: reserved");
	bad("datasets { www { canmount = \"on\"; } }", C, "invalid property: "
	    "reserved");
	bad("datasets { www { \"shtetl:kind\" = \"x\"; } }", C, "invalid "
	    "property: reserved");
	bad("datasets { www { Readonly = \"on\"; } }", C, "invalid property: "
	    "malformed name");
	bad("datasets { www { \"Com.example:x\" = \"y\"; } }", C, "invalid "
	    "property: only lowercase");
	bad("datasets { www { readonly = \"\"; } }", C, "datasets.www."
	    "readonly: empty");
	bad("datasets { www = \"on\"; }", C, "datasets.www: must be true or "
	    "an object");
	bad("datasets { www = false; }", C, "datasets.www: dropping values "
	    "is illegal");
}

/* CK14, CK15: commands and derivations. */
static void
test_commands(void)
{
	ok("commands { a = \"x\"; \"c.d\" = \"y\"; }", S);
	bad("commands { a = true; }", S, "commands.a: must be a quoted "
	    "string");
	bad("commands { a = false; }", S, "commands.a: dropping values is "
	    "illegal");
	bad("commands { a { x = 1; } }", S, "commands.a: must be a string");
	bad("commands { a = 5; }", S, "commands.a: must be a quoted string");
	bad("commands = [ \"a\" ];", S, "commands: must be an object");
	bad("commands { a = \"\"; }", S, "commands.a: empty");
	bad("commands { \"-a\" = \"x\"; }", S, "commands.\"-a\": invalid "
	    "label: must begin with a letter or digit");

	ok("derivations { \"etc/pwd.db\" { source = \"etc/master.passwd\"; "
	    "command = \"pwd_mkdb\"; } }", C);
	bad("derivations { x { source = \"s\"; } }", C, "derivations.x: "
	    "missing command");
	bad("derivations { x { command = \"c\"; } }", C, "derivations.x: "
	    "missing source");
	bad("derivations { x { source = \"s\"; command = \"c\"; "
	    "bogus = \"1\"; } }", C, "derivations.x.bogus: invalid key");
	bad("derivations { x { source = \"s\"; command = 5; } }",
	    C, "derivations.x.command: must be a quoted string");
	bad("derivations { x { source = \"/etc/s\"; command = \"c\"; } "
	    "}", C, "derivations.x.source: invalid path: absolute path");
	bad("derivations { \"/etc/x\" { source = \"s\"; command = \"c\"; } }",
	    C, "derivations.\"/etc/x\": invalid path: absolute path");
	bad("derivations = [ \"x\" ];", C, "derivations: must be an object");
	bad("derivations { x = true; }", C, "derivations.x: must be an "
	    "object");
	bad("derivations { x = false; }", C, "dropping values is illegal");
}

/* CK16, CK17: requires, and patch sources. */
static void
test_requires_patches(void)
{
	enum sh_loc k;

	ok("requires = [ \"db\", \"cache-1\" ];", C);
	ok("requires { db = true; }", C);
	bad("requires { old = false; }", C, "requires.old: dropping values "
	    "is illegal");
	bad("requires = [ \"a.b\" ];", C, "requires[0]: invalid container "
	    "name: only letters, digits, - and _ allowed");
	bad("requires { db { x = 1; } }", C, "requires.db: must be true");

	ok("patches = [ \"github.com/eidelbyte/freebsd-patches\", "
	    "\"https://git.example.org/p.git\", \"patches/generic\" ];", F);
	ok("patches { \"github.com/x/y\" { ref = \"releng-15.1\"; dir = "
	    "\"15.1\"; } \"patches/local\" {} }", F);
	bad("patches { old = false; }", F, "patches.old: dropping values is "
	    "illegal");
	bad("patches = [ \"/abs/dir\" ];", F, "patches[0]: invalid patch "
	    "location: absolute path");
	bad("patches = [ \"patches/../x\" ];", F, ". or .. component");
	bad("patches = [ \"git@github.com:x/y\" ];", F, "invalid patch "
	    "location: scp-style address: use ssh://");
	bad("patches = [ \"github.com\" ];", F, "invalid patch location: "
	    "missing path after host");
	bad("patches = [ \"1http://x\" ];", F, "invalid patch location: "
	    "scheme must begin with a letter");
	bad("patches { x { bogus = \"1\"; } }", F, "patches.x.bogus: invalid "
	    "key");
	bad("patches { x { ref = \"a b\"; } }", F, "patches.x.ref: invalid "
	    "git ref: space or control character");
	bad("patches { x { dir = \"/x\"; } }", F, "patches.x.dir: invalid "
	    "path: absolute path");

	CHECK(sh_syn_location("https://x.org/a", &k) == NULL &&
	    k == SH_LOC_URL);
	CHECK(sh_syn_location("github.com/a/b", &k) == NULL &&
	    k == SH_LOC_HOST);
	CHECK(sh_syn_location("patches/generic", &k) == NULL &&
	    k == SH_LOC_DIR);
	CHECK(sh_syn_location("generic", &k) == NULL && k == SH_LOC_DIR);
}

/* CK19, CK20: every error is reported; paths quote what is not bare. */
static void
test_reporting(void)
{
	CHECK(L("frob = 1;\npkg = [ \"-x\" ];\nkernconf_file = \"G\";\n"
	    "datasets { \"usr/local\" { recordsize = 1; } }\n", C) == 4);
	CHECK(cap_has(&c, "t.conf: frob: invalid key\n"));
	CHECK(cap_has(&c, "t.conf: pkg[0]: invalid package name: leading -"));
	CHECK(cap_has(&c, "t.conf: kernconf_file: valid only in foundation "
	    "recipe"));
	CHECK(cap_has(&c, "t.conf: datasets.\"usr/local\".recordsize: "
	    "must be a quoted string"));
	bad("datasets { \"x\\\"y\" {} }", F, "datasets.\"x\\\"y\": invalid "
	    "dataset path: invalid character");
}

/* TEXT checked as a file of the default layer: the count. */
static unsigned
DF(const char *text, int settings)
{
	struct sh_node *n;
	unsigned bad;

	cap_reset(&c);
	n = sh_parse_buf(text, strlen(text), "shtetl.conf", &c.c_err);
	CHECK(n != NULL);
	bad = sh_schema_default(n, "shtetl.conf", settings, &c.c_err);
	sh_node_free(n);
	return (bad);
}

/*
 * CK22: the default layer takes every kind's keys; shtetl.conf's
 * settings are not recipe keys, and a file it imports has none.
 */
static void
test_default(void)
{
	CHECK(DF("build { make_jobs = 4; }\nver = \">=15.1\";\n"
	    "kernconf_file = \"GENERIC\";\nrequires = [ \"db\" ];\n"
	    "jailconf = \"allow.raw_sockets;\";\npkg = [ \"sudo\" ];\n", 1) ==
	    0);
	CHECK(DF("frob = 1;\n", 1) == 1);
	CHECK(cap_has(&c, "shtetl.conf: frob: invalid key"));
	CHECK(DF("build { make_jobs = 4; }\n", 0) == 1);
	CHECK(cap_has(&c, "shtetl.conf: build: invalid key"));
	CHECK(DF("kernconf_file = 5;\n", 1) == 1);
	CHECK(cap_has(&c, "kernconf_file: must be a quoted string"));
}

int
main(void)
{
	cap_open(&c);
	test_every_key();
	test_kinds();
	test_strings();
	test_files();
	test_import();
	test_pkg();
	test_datasets();
	test_commands();
	test_requires_patches();
	test_reporting();
	test_default();
	cap_close(&c);
	printf("check_schema: %d checks\n", checks);
	return (0);
}
