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
	    "reset = [ \"pkg\" ];\n"
	    "ver = \">=15\";\n"
	    "src_branch = \"releng/15.1\";\n"
	    "kernconf = \"GENERIC\";\n"
	    "buildopts = <<EOD\nWITHOUT_LIB32=yes\nEOD\n"
	    "patches = [ \"github.com/eidelbyte/freebsd-patches\" ];\n"
	    "datasets { tmp {} var {} \"usr/local\" { compression = "
	    "\"zstd-9\"; } }\n"
	    "pkg = [ \"sudo\", \"tmux\" ];\n"
	    "commands { motd = \"echo hi > /etc/motd\"; }\n"
	    "derivations { \"etc/pwd.db\" { source = \"etc/master.passwd\"; "
	    "command = \"pwd_mkdb -d etc etc/master.passwd\"; } }\n", F);
	ok("kernconf_file = \"kernels/GAMING\";\n"
	    "buildopts_file = \"build/src.conf\";\n", F);
	ok("import = \"common/web.conf\";\n"
	    "ver = \"=15.1\";\n"
	    "pkg { nginx = true; apache24 = false; }\n"
	    "datasets { www { readonly = true; } }\n"
	    "commands { nginx = <<EOD\nsysrc nginx_enable=YES\nEOD\n}\n"
	    "derivations { x { command = \"true\"; } }\n"
	    "requires = [ \"db\" ];\n"
	    "jailconf = \"allow.raw_sockets;\";\n", C);
	ok("jailconf_file = \"jails/nginx.jail\";\n", C);
	ok("import = [ \"common/base.conf\" ];\n"
	    "reset = [ \"datasets\" ];\n"
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
	    "kernconf = \"G\";", "kernconf_file = \"k\";",
	    "buildopts = \"x\";", "buildopts_file = \"b\";",
	    "patches = [ \"p/q\" ];" };
	char want[128];
	size_t i;

	bad("frobnicate = 1;\n", F, "t.conf: frobnicate: not a key a recipe "
	    "takes");
	bad("Pkg = [ \"a\" ];\n", C, "t.conf: Pkg: not a key a recipe takes");
	for (i = 0; i < sizeof (fonly) / sizeof (fonly[0]); i++) {
		(void) snprintf(want, sizeof (want), "t.conf: %.*s: not a key "
		    "of a container's recipe: only a foundation's takes it",
		    (int)strcspn(fonly[i], " "), fonly[i]);
		bad(fonly[i], C, want);
		bad(fonly[i], S, "only a foundation's takes it");
		CHECK(cap_has(&c, "of a system's recipe"));
	}
	bad("requires = [ \"db\" ];", F, "t.conf: requires: not a key of a "
	    "foundation's recipe: only a container's takes it");
	bad("requires = [ \"db\" ];", S, "only a container's takes it");
	bad("jailconf = \"x\";", S, "jailconf: not a key of a system's");
	bad("jailconf_file = \"j\";", F, "only a container's takes it");
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

	bad("src_branch = 15;", F, "src_branch: expected a string, not an "
	    "integer: quote it");
	bad("kernconf = true;", F, "expected a string, not true: quote it");
	bad("ver = 15.1;", C, "ver: expected a version constraint, not a "
	    "number: quote it");
	bad("jailconf = [ \"a\" ];", C, "expected a string, not a list");
	CHECK(!cap_has(&c, "quote it"));
	bad("src_branch = \"\";", F, "src_branch: empty");
	for (i = 0; i < sizeof (good) / sizeof (good[0]); i++) {
		(void) snprintf(text, sizeof (text), "ver = \"%s\";", good[i]);
		ok(text, S);
	}
	for (i = 0; i < sizeof (poor) / sizeof (poor[0]); i++) {
		(void) snprintf(text, sizeof (text), "ver = \"%s\";", poor[i]);
		bad(text, S, poor[i][0] == '\0' ? "ver: empty" :
		    "ver: not a version constraint: ");
	}
}

/* CK7, CK8: the twins, and paths in the configuration root. */
static void
test_files(void)
{
	bad("kernconf = \"G\"; kernconf_file = \"k\";", F,
	    "kernconf: and kernconf_file both: a layer gives one or the "
	    "other");
	bad("buildopts_file = \"b\"; buildopts = \"x\";", F,
	    "buildopts: and buildopts_file both");
	bad("jailconf = \"x\"; jailconf_file = \"j\";", C,
	    "jailconf: and jailconf_file both");
	ok("kernconf_file = \"kernels/GAMING\";", F);
	bad("kernconf_file = \"/etc/GAMING\";", F, "kernconf_file: not a "
	    "file's path: it is absolute");
	bad("kernconf_file = \"a/../b\";", F, "a '.' or '..' component");
	bad("kernconf_file = \"./b\";", F, "a '.' or '..' component");
	bad("kernconf_file = \"a//b\";", F, "an empty component");
	bad("kernconf_file = \"a/\";", F, "it ends with '/'");
	bad("jailconf_file = 5;", C, "expected a file's path, not an "
	    "integer: quote it");
}

/* CK9, CK10: import and reset. */
static void
test_import_reset(void)
{
	ok("import = \"x.conf\";", C);
	ok("import = [ \"a.conf\", \"b/c.conf\" ];", C);
	bad("import = [ \"a.conf\", 1 ];", C, "import[1]: expected a "
	    "recipe's path, not an integer");
	bad("import = [ \"/x.conf\" ];", C, "import[0]: not a recipe's path: "
	    "it is absolute");
	bad("import { a = 1; }", C, "import: expected a list of recipes' "
	    "paths, not an object");

	ok("reset = [ \"pkg\", \"datasets\" ];", C);
	ok("reset = \"commands\";", S);
	ok("reset = [ \"patches\" ];", F);
	bad("reset = [ \"ver\" ];", C, "reset[0]: not a collection a "
	    "container's recipe has: \"ver\"");
	bad("reset = [ \"bogus\" ];", C, "not a collection");
	bad("reset = [ \"patches\" ];", C, "not a collection a container's "
	    "recipe has: \"patches\"");
	bad("reset = [ \"requires\" ];", S, "not a collection a system's");
	bad("reset = { pkg = true; }", C, "reset: expected a list of "
	    "collections' names, not an object");
}

/* CK11, CK18: pkg, and collections given as scalars. */
static void
test_pkg(void)
{
	ok("pkg = [ \"a\", \"py311-certbot\", \"www/nginx\", \"gtk+3\" ];",
	    C);
	ok("pkg { a = true; b = false; }", C);
	bad("pkg { a { x = 1; } }", C, "pkg.a: expected true, or false to "
	    "drop it, not an object");
	bad("pkg { a = \"yes\"; }", C, "pkg.a: expected true, or false to "
	    "drop it, not a string");
	bad("pkg = [ \"\" ];", C, "pkg[0]: not a package's name: empty");
	bad("pkg = [ \"-x\" ];", C, "pkg[0]: not a package's name: it "
	    "begins with '-'");
	bad("pkg = [ \"a b\" ];", C, "a character a package's name does not "
	    "take");
	bad("pkg = [ \"a\", \"b\", \"a\" ];", C, "pkg[2]: \"a\" is listed "
	    "twice");
	bad("pkg = [ 5 ];", C, "pkg[0]: expected a package's name, not an "
	    "integer: quote it");
	/* CK18 */
	bad("pkg = \"a\";", C, "pkg: expected an object keyed by a "
	    "package's name, or a list of them, not a string");
	bad("datasets = 5;", F, "datasets: expected an object keyed by a "
	    "dataset path, or a list of them, not an integer");
	bad("commands = \"x\";", F, "commands: expected an object keyed by "
	    "a label, not a string");
}

/* CK12, CK13: dataset paths, and their properties. */
static void
test_datasets(void)
{
	ok("datasets = [ \"tmp\", \"usr/local\", \"var/db:x\", "
	    "\"a b\" ];", F);
	bad("datasets = [ \"/usr\" ];", F, "datasets[0]: not a dataset path: "
	    "it begins with '/'");
	bad("datasets = [ \"usr/\" ];", F, "it ends with '/'");
	bad("datasets = [ \"a//b\" ];", F, "an empty component");
	bad("datasets = [ \".\" ];", F, "a '.' or '..' component");
	bad("datasets = [ \"a/../b\" ];", F, "a '.' or '..' component");
	bad("datasets { \"a@b\" {} }", F, "datasets.\"a@b\": not a dataset "
	    "path: a character ZFS names do not take");
	bad("datasets = [ \"a#b\" ];", F, "a character ZFS names do not take");

	ok("datasets { www { readonly = true; compression = \"lz4\"; "
	    "recordsize = \"1M\"; \"com.example:x\" = \"y\"; "
	    "\"userquota@www\" = \"1G\"; } }", C);
	bad("datasets { www { recordsize = 1M; } }", C, "datasets.www."
	    "recordsize: expected a string, or true or false, not an "
	    "integer: quote it");
	bad("datasets { www { mountpoint = \"/x\"; } }", C, "datasets.www."
	    "mountpoint: not a property the configuration may set: shtetl "
	    "sets it itself");
	bad("datasets { www { canmount = \"on\"; } }", C, "shtetl sets it "
	    "itself");
	bad("datasets { www { \"shtetl:kind\" = \"x\"; } }", C, "shtetl sets "
	    "it itself");
	bad("datasets { www { Readonly = \"on\"; } }", C, "not a ZFS "
	    "property's name");
	bad("datasets { www { \"Com.example:x\" = \"y\"; } }", C, "a user "
	    "property takes only lowercase");
	bad("datasets { www { readonly = \"\"; } }", C, "datasets.www."
	    "readonly: empty");
	bad("datasets { www = \"on\"; }", C, "datasets.www: expected true, "
	    "its properties, or false to drop it, not a string");
}

/* CK14, CK15: commands and derivations. */
static void
test_commands(void)
{
	ok("commands { a = \"x\"; b = false; \"c.d\" = \"y\"; }", S);
	bad("commands { a = true; }", S, "commands.a: expected its commands "
	    "as a string or heredoc, or false to drop it, not true");
	bad("commands { a { x = 1; } }", S, "not an object");
	bad("commands { a = 5; }", S, "not an integer: quote it");
	bad("commands = [ \"a\" ];", S, "commands: expected an object keyed "
	    "by a label, not a list");
	bad("commands { a = \"\"; }", S, "commands.a: empty");
	bad("commands { \"-a\" = \"x\"; }", S, "commands.\"-a\": not a label: "
	    "it must begin with a letter or a digit");

	ok("derivations { \"etc/pwd.db\" { source = \"etc/master.passwd\"; "
	    "command = \"pwd_mkdb\"; } x = false; }", C);
	ok("derivations { x { source = \"s\"; } }", C);
	bad("derivations { x { bogus = \"1\"; } }", C, "derivations.x.bogus: "
	    "not a field of a derivation: it takes source and command");
	bad("derivations { x { command = 5; } }", C, "derivations.x.command: "
	    "expected a command, not an integer: quote it");
	bad("derivations { x { source = \"/etc/s\"; } }", C, "not a source "
	    "file's path: it is absolute");
	bad("derivations { \"/etc/x\" { command = \"c\"; } }", C,
	    "derivations.\"/etc/x\": not a derived file's path: it is "
	    "absolute");
	bad("derivations = [ \"x\" ];", C, "derivations: expected an object "
	    "keyed by a derived file's path, not a list");
	bad("derivations { x = true; }", C, "expected { source; command; }, "
	    "or false to drop it, not true");
}

/* CK16, CK17: requires, and patch sources. */
static void
test_requires_patches(void)
{
	enum sh_loc k;

	ok("requires = [ \"db\", \"cache-1\" ];", C);
	ok("requires { db = true; old = false; }", C);
	bad("requires = [ \"a.b\" ];", C, "requires[0]: not a container's "
	    "name: it takes only letters, digits, '-' and '_'");
	bad("requires { db { x = 1; } }", C, "requires.db: expected true, "
	    "or false to drop it, not an object");

	ok("patches = [ \"github.com/eidelbyte/freebsd-patches\", "
	    "\"https://git.example.org/p.git\", \"patches/generic\" ];", F);
	ok("patches { \"github.com/x/y\" { ref = \"releng-15.1\"; dir = "
	    "\"15.1\"; } \"patches/local\" {} old = false; }", F);
	bad("patches = [ \"/abs/dir\" ];", F, "patches[0]: not a patch "
	    "source's location: it is absolute");
	bad("patches = [ \"patches/../x\" ];", F, "a '.' or '..' component");
	bad("patches = [ \"git@github.com:x/y\" ];", F, "an scp-style "
	    "address: write it as a URL, ssh://...");
	bad("patches = [ \"github.com\" ];", F, "a host with no path");
	bad("patches = [ \"1http://x\" ];", F, "begins with a letter");
	bad("patches { x { bogus = \"1\"; } }", F, "patches.x.bogus: not a "
	    "field of a patch source: it takes ref and dir");
	bad("patches { x { ref = \"a b\"; } }", F, "patches.x.ref: not a git "
	    "ref: a space or a control character");
	bad("patches { x { dir = \"/x\"; } }", F, "patches.x.dir: not a "
	    "directory in the source: it is absolute");

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
	CHECK(L("frob = 1;\npkg = [ \"-x\" ];\nkernconf = \"G\";\n"
	    "datasets { \"usr/local\" { recordsize = 1; } }\n", C) == 4);
	CHECK(cap_has(&c, "t.conf: frob: not a key a recipe takes\n"));
	CHECK(cap_has(&c, "t.conf: pkg[0]: not a package's name"));
	CHECK(cap_has(&c, "t.conf: kernconf: not a key of a container's"));
	CHECK(cap_has(&c, "t.conf: datasets.\"usr/local\".recordsize: "
	    "expected a string"));
	bad("datasets { \"x\\\"y\" {} }", F, "datasets.\"x\\\"y\": not a "
	    "dataset path");
}

int
main(void)
{
	cap_open(&c);
	test_every_key();
	test_kinds();
	test_strings();
	test_files();
	test_import_reset();
	test_pkg();
	test_datasets();
	test_commands();
	test_requires_patches();
	test_reporting();
	cap_close(&c);
	printf("check_schema: %d checks\n", checks);
	return (0);
}
