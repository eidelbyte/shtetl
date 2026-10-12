/*
 * The recipe cascade (planning.md 5.4): tests/MATRIX.md family CC,
 * every cell (CC1 - CC26). Each test writes the configuration root it
 * needs into the scratch directory.
 */

#include <string.h>

#include "check.h"
#include "core/cascade.h"
#include "core/tree.h"

static struct cap c;
static struct sh_recipe r;
static char root[1100];

/* A configuration root DIR holding FILES, pairs of path and text. */
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

/* TARGET resolved in ROOT into r, after the last one is freed. */
static int
R(const char *rt, const char *target)
{
	sh_recipe_free(&r);
	cap_reset(&c);
	return (sh_recipe_resolve(rt, target, &r, &c.c_err));
}

static void
resolves(const char *rt, const char *target)
{
	if (R(rt, target) != 0)
		printf("%s did not resolve:\n%s", target, cap_text(&c));
	CHECK(c.c_err.se_count == 0);
	CHECK(r.sr_merged != NULL);
}

/* Refused with exactly one message, holding WANT. */
static void
refused(const char *rt, const char *target, const char *want)
{
	int rc;

	rc = R(rt, target);
	if (rc == 0 || c.c_err.se_count != 1 || !cap_has(&c, want))
		printf("%s: wanted one message with \"%s\", got:\n%s", target,
		    want, cap_text(&c));
	CHECK(rc == -1);
	CHECK(c.c_err.se_count == 1);
	CHECK(cap_has(&c, want));
}

/* The keys of OBJ, in order, space-separated. */
static const char *
keys(const struct sh_node *obj)
{
	static char buf[1024];
	size_t i, n;

	buf[0] = '\0';
	for (i = 0; obj != NULL && i < obj->sn_obj.ob_n; i++) {
		n = strlen(buf);
		(void) snprintf(buf + n, sizeof (buf) - n, "%s%s",
		    i > 0 ? " " : "", obj->sn_obj.ob_v[i].pr_key);
	}
	return (buf);
}

/* The layers of r, in merge order. */
static const char *
layers(void)
{
	static char buf[1024];
	size_t i, n;

	buf[0] = '\0';
	for (i = 0; i < r.sr_nlayers; i++) {
		n = strlen(buf);
		(void) snprintf(buf + n, sizeof (buf) - n, "%s%s",
		    i > 0 ? " " : "", r.sr_layers[i].sl_file);
	}
	return (buf);
}

static const struct sh_node *
M(const char *key)
{
	return (sh_obj_get(r.sr_merged, key));
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

/* The 5.4 example, as the plan writes it. */
static const char *const example[] = {
	"default.conf",
	"pkg = [ \"sudo\", \"tmux\" ];\n"
	"datasets {\n"
	"    tmp {}\n"
	"    var {}\n"
	"    \"usr/local\" {}\n"
	"}\n",
	"common/web.conf",
	"pkg = [ \"apache24\", \"py311-certbot\" ];\n"
	"commands {\n"
	"    certbot = <<EOD\n"
	"sysrc certbot_enable=YES\n"
	"EOD\n"
	"}\n",
	"containers/nginx.conf",
	"import = [ \"common/web.conf\" ];\n"
	"pkg { nginx = true; }\n"
	"datasets {\n"
	"    www { readonly = true; }\n"
	"}\n"
	"commands {\n"
	"    nginx = <<EOD\n"
	"sysrc nginx_enable=YES\n"
	"EOD\n"
	"}\n",
	NULL
};

/* CC1, CC13, CC17, CC24: the worked example, and its layers. */
static void
test_example(void)
{
	const struct sh_node *pkg, *www;
	const struct sh_pair *pr;

	resolves(mkroot("ex", example), "containers/nginx");
	CHECK(r.sr_kind == SH_CONTAINER);
	CHECK(is(r.sr_name, "nginx"));
	CHECK(is(r.sr_file, "containers/nginx.conf"));
	CHECK(is(layers(), "default.conf common/web.conf "
	    "containers/nginx.conf"));
	CHECK(is(keys(M("pkg")), "sudo tmux apache24 py311-certbot nginx"));
	CHECK(is(keys(M("datasets")), "tmp var usr/local www"));
	CHECK(is(keys(M("commands")), "certbot nginx"));
	CHECK(is(sh_obj_get(M("commands"), "certbot")->sn_str,
	    "sysrc certbot_enable=YES"));
	/* CC24: who added each, who set each */
	pkg = M("pkg");
	pr = sh_obj_find(pkg, "sudo");
	CHECK(sh_recipe_layer(&r, pr->pr_from) == &r.sr_layers[0]);
	CHECK(pr->pr_val->sn_from == r.sr_layers[0].sl_file);
	pr = sh_obj_find(pkg, "py311-certbot");
	CHECK(pr->pr_from == r.sr_layers[1].sl_file);
	pr = sh_obj_find(pkg, "nginx");
	CHECK(pr->pr_from == r.sr_layers[2].sl_file);
	www = sh_obj_get(M("datasets"), "www");
	CHECK(www->sn_type == SH_OBJECT);
	CHECK(sh_obj_get(www, "readonly")->sn_bool);
	CHECK(sh_recipe_layer(&r, "containers/nginx.conf") == NULL);
	/* import and reset are the cascade's, not the recipe's */
	CHECK(M("import") == NULL);
}

/* CC2, CC3, CC4: default.conf absent; imports nested and repeated. */
static void
test_layers(void)
{
	static const char *const bare[] = {
		"containers/x.conf", "pkg = [ \"a\" ];\n",
		NULL
	};
	static const char *const nest[] = {
		"default.conf", "import = \"common/base.conf\";\n",
		"common/base.conf", "pkg = [ \"zsh\" ];\n",
		"common/c.conf", "pkg = [ \"c\" ];\n",
		"common/a.conf", "import = [ \"common/c.conf\" ];\n",
		"common/b.conf",
		"import = [ \"common/c.conf\", \"common/base.conf\" ];\n",
		"containers/x.conf",
		"import = [ \"common/a.conf\", \"common/b.conf\", "
		"\"default.conf\" ];\n",
		NULL
	};

	resolves(mkroot("bare", bare), "containers/x");
	CHECK(is(layers(), "containers/x.conf"));
	CHECK(is(keys(M("pkg")), "a"));

	resolves(mkroot("nest", nest), "containers/x");
	CHECK(is(layers(), "common/base.conf default.conf common/c.conf "
	    "common/a.conf common/b.conf containers/x.conf"));
	CHECK(is(keys(M("pkg")), "zsh c"));
}

/* CC5, CC6: a cycle, and an import of nothing. */
static void
test_import_errors(void)
{
	static const char *const cyc[] = {
		"common/a.conf", "import = \"common/b.conf\";\n",
		"common/b.conf", "import = [ \"common/a.conf\" ];\n",
		"containers/x.conf", "import = \"common/a.conf\";\n",
		"containers/self.conf",
		"import = [ \"containers/self.conf\" ];\n",
		"containers/gone.conf",
		"import = [ \"common/a.conf\", \"common/nope.conf\" ];\n",
		NULL
	};

	refused(mkroot("cyc", cyc), "containers/x", "common/b.conf: "
	    "import[0]: an import cycle: common/a.conf, common/b.conf, "
	    "common/a.conf");
	CHECK(r.sr_merged == NULL);
	refused(root, "containers/self", "containers/self.conf: import[0]: "
	    "an import cycle: containers/self.conf, containers/self.conf");
	(void) R(root, "containers/gone");
	CHECK(cap_has(&c, "containers/gone.conf: import[1]: common/nope.conf: "
	    "no such recipe in the configuration root"));
}

/*
 * CC7, CC8: what stacks. Every file's ver is kept, to hold together;
 * buildopts and jailconf append, lower layers first, a _file's file in
 * its place; kernconf, one value, is the nearer file's until Q-AC is
 * ruled (planning.md 15).
 */
static void
test_stacked(void)
{
	static const char *const st[] = {
		"default.conf", "ver = \">=14\";\npkg = [ \"a\" ];\n",
		"common/w.conf", "ver = \">=15\";\njailconf = \"j1;\";\n",
		"containers/x.conf", "import = \"common/w.conf\";\n"
		"pkg = [ \"b\" ];\nver = \"<16\";\n"
		"jailconf_file = \"jails/x\";\njailconf = \"j2;\";\n",
		"jails/x", "allow.raw_sockets;\n",
		"common/b.conf", "buildopts = \"A=1\";\n"
		"buildopts_file = \"build/b.conf\";\n",
		"build/b.conf", "WITHOUT_X=yes\n",
		"common/k1.conf", "kernconf = \"GENERIC\";\n",
		"common/k2.conf", "kernconf_file = \"kernels/G\";\n",
		"kernels/G", "ident G\n",
		"foundations/f1.conf", "import = [ \"common/k1.conf\", "
		"\"common/b.conf\" ];\n"
		"src_branch = \"main\";\nkernconf_file = \"kernels/G\";\n"
		"buildopts = \"B=2\";\n",
		"foundations/f2.conf", "import = \"common/k2.conf\";\n"
		"src_branch = \"main\";\nkernconf = \"MINE\";\n",
		NULL
	};
	const struct sh_node *v, *el;

	resolves(mkroot("st", st), "containers/x");
	CHECK(is(keys(r.sr_merged), "ver pkg jailconf"));
	v = M("ver");
	CHECK(v->sn_type == SH_ARRAY && v->sn_arr.ar_n == 3);
	CHECK(is(v->sn_arr.ar_v[0]->sn_str, ">=14"));
	CHECK(is(v->sn_arr.ar_v[0]->sn_from, "default.conf"));
	CHECK(is(v->sn_arr.ar_v[1]->sn_str, ">=15"));
	CHECK(is(v->sn_arr.ar_v[1]->sn_from, "common/w.conf"));
	CHECK(is(v->sn_arr.ar_v[2]->sn_str, "<16"));
	CHECK(is(v->sn_arr.ar_v[2]->sn_from, "containers/x.conf"));
	v = M("jailconf");
	CHECK(v->sn_type == SH_ARRAY && v->sn_arr.ar_n == 3);
	CHECK(is(v->sn_arr.ar_v[0]->sn_str, "j1;"));
	el = v->sn_arr.ar_v[1];
	CHECK(el->sn_type == SH_OBJECT);
	CHECK(is(sh_obj_get(el, "file")->sn_str, "jails/x"));
	CHECK(is(el->sn_from, "containers/x.conf"));
	CHECK(is(v->sn_arr.ar_v[2]->sn_str, "j2;"));

	resolves(root, "foundations/f1");
	v = M("buildopts");
	CHECK(v->sn_type == SH_ARRAY && v->sn_arr.ar_n == 3);
	CHECK(is(v->sn_arr.ar_v[0]->sn_str, "A=1"));
	CHECK(is(sh_obj_get(v->sn_arr.ar_v[1], "file")->sn_str,
	    "build/b.conf"));
	CHECK(is(v->sn_arr.ar_v[2]->sn_str, "B=2"));
	CHECK(is(v->sn_arr.ar_v[2]->sn_from, "foundations/f1.conf"));
	/* kernconf: one value; the file's stands for it */
	CHECK(M("kernconf_file") == NULL);
	CHECK(is(sh_obj_get(M("kernconf"), "file")->sn_str, "kernels/G"));
	resolves(root, "foundations/f2");
	CHECK(is(M("kernconf")->sn_str, "MINE"));
}

/*
 * CC9, CC10, CC16: entries added and stacked; order. A field or a
 * commands block given twice is the nearer file's until Q-AC is ruled
 * (planning.md 15).
 */
static void
test_collections(void)
{
	static const char *const co[] = {
		"default.conf",
		"datasets { var {} }\n"
		"commands { a = \"1\"; b = \"2\"; }\n",
		"common/f.conf",
		"datasets { www { readonly = true; compression = \"lz4\"; } "
		"cache {} }\n"
		"commands { c = \"3\"; a = \"1b\"; }\n",
		"containers/x.conf",
		"import = \"common/f.conf\";\n"
		"datasets { www { compression = \"zstd\"; } db {} }\n"
		"commands { b = \"2b\"; d = \"4\"; }\n",
		"containers/y.conf",
		"import = \"common/f.conf\";\n"
		"datasets = [ \"www\", \"x\" ];\n",
		NULL
	};
	const struct sh_node *www, *cmd;
	const struct sh_pair *pr;

	resolves(mkroot("co", co), "containers/x");
	CHECK(is(keys(M("datasets")), "var www cache db"));
	www = sh_obj_get(M("datasets"), "www");
	CHECK(is(keys(www), "readonly compression"));
	CHECK(sh_obj_get(www, "readonly")->sn_bool);
	pr = sh_obj_find(www, "compression");
	CHECK(is(pr->pr_val->sn_str, "zstd"));
	CHECK(is(pr->pr_from, "common/f.conf"));
	CHECK(is(pr->pr_val->sn_from, "containers/x.conf"));
	CHECK(is(sh_obj_find(M("datasets"), "www")->pr_from,
	    "common/f.conf"));
	/* CC16 */
	cmd = M("commands");
	CHECK(is(keys(cmd), "a b c d"));
	CHECK(is(sh_obj_get(cmd, "a")->sn_str, "1b"));
	CHECK(is(sh_obj_get(cmd, "a")->sn_from, "common/f.conf"));
	CHECK(is(sh_obj_get(cmd, "b")->sn_str, "2b"));

	/* CC10: a list of names over an entry with fields */
	resolves(root, "containers/y");
	CHECK(is(keys(M("datasets")), "var www cache x"));
	www = sh_obj_get(M("datasets"), "www");
	CHECK(www->sn_type == SH_OBJECT);
	CHECK(sh_obj_get(www, "readonly")->sn_bool);
	CHECK(sh_obj_get(M("datasets"), "x")->sn_type == SH_BOOL);
}

/*
 * CC11, CC12, CC14, CC15: nothing is dropped and nothing reset (ruled
 * 2026-10-12), in any kind of recipe; so what default.conf and its
 * imports give, every recipe keeps, commands included.
 */
static void
test_nodrops(void)
{
	static const char *const nd[] = {
		"default.conf",
		"import = \"common/base.conf\";\n"
		"pkg = [ \"sudo\", \"tmux\" ];\n"
		"datasets { tmp {} }\n"
		"commands { motd = \"echo\"; }\n",
		"common/base.conf", "pkg = [ \"zsh\" ];\n",
		"containers/tmux.conf", "pkg { tmux = false; }\n",
		"containers/tmp.conf", "datasets { tmp = false; }\n",
		"systems/motd.conf", "commands { motd = false; }\n",
		"foundations/f.conf",
		"src_branch = \"main\";\npkg { tmux = false; }\n",
		"foundations/g.conf",
		"src_branch = \"main\";\nreset = [ \"pkg\" ];\n",
		"containers/keep.conf", "pkg = [ \"mine\" ];\n",
		NULL
	};

	refused(mkroot("nd", nd), "containers/tmux", "containers/tmux.conf: "
	    "pkg.tmux: false drops nothing: the layers stack, and what one "
	    "gives stays (ruled 2026-10-12)");
	refused(root, "containers/tmp", "datasets.tmp: false drops nothing");
	refused(root, "systems/motd", "commands.motd: false drops nothing");
	refused(root, "foundations/f", "foundations/f.conf: pkg.tmux: false "
	    "drops nothing");
	refused(root, "foundations/g", "foundations/g.conf: reset: nothing is "
	    "reset: the layers stack");
	resolves(root, "containers/keep");
	CHECK(is(keys(M("pkg")), "zsh sudo tmux mine"));
	CHECK(is(keys(M("datasets")), "tmp"));
	CHECK(is(keys(M("commands")), "motd"));
}

/* CC18 - CC21: what only the merged recipe can answer. */
static void
test_merged(void)
{
	static const char *const mg[] = {
		"foundations/nob.conf", "pkg = [ \"a\" ];\n",
		"common/d.conf",
		"derivations { \"etc/x\" { source = \"etc/y\"; } }\n",
		"containers/half.conf", "import = \"common/d.conf\";\n",
		"containers/whole.conf",
		"import = \"common/d.conf\";\n"
		"derivations { \"etc/x\" { command = \"mk etc/y\"; } }\n",
		"foundations/files.conf",
		"src_branch = \"main\";\nkernconf_file = \"kernels/NOPE\";\n"
		"buildopts_file = \"build/src.conf\";\n"
		"patches { \"patches/gen\" { dir = \"15.1\"; } }\n",
		"build/src.conf", "WITHOUT_X=yes\n",
		"patches/gen/14.3/0-a.patch", "\n",
		"foundations/pat.conf",
		"src_branch = \"main\";\n"
		"patches = [ \"patches/gone\", \"patches/gen\", "
		"\"github.com/a/b\" ];\n",
		"containers/jail.conf", "jailconf_file = \"jails/x\";\n",
		"containers/db.conf", "\n",
		"containers/req.conf",
		"requires = [ \"db\", \"req\", \"nope\" ];\n",
		NULL
	};

	refused(mkroot("mg", mg), "foundations/nob", "foundations/nob.conf: "
	    "no layer gives src_branch: a foundation is built from a FreeBSD "
	    "branch");
	refused(root, "containers/half", "common/d.conf: "
	    "derivations.\"etc/x\": no layer gives its command");
	resolves(root, "containers/whole");
	CHECK(is(keys(sh_obj_get(M("derivations"), "etc/x")),
	    "source command"));
	/* CC20: buildopts_file is there; the others are not */
	CHECK(R(root, "foundations/files") == -1);
	CHECK(c.c_err.se_count == 2);
	CHECK(cap_has(&c, "foundations/files.conf: kernconf: "
	    "kernels/NOPE: no such file in the configuration root"));
	CHECK(cap_has(&c, "foundations/files.conf: patches.\"patches/gen\": "
	    "patches/gen/15.1: no such directory in the configuration root"));
	refused(root, "foundations/pat", "foundations/pat.conf: "
	    "patches.\"patches/gone\": patches/gone: no such directory");
	refused(root, "containers/jail", "containers/jail.conf: jailconf: "
	    "jails/x: no such file");
	/* CC21 */
	CHECK(R(root, "containers/req") == -1);
	CHECK(c.c_err.se_count == 2);
	CHECK(cap_has(&c, "containers/req.conf: requires.req: a container "
	    "cannot require itself"));
	CHECK(cap_has(&c, "containers/req.conf: requires.nope: no such "
	    "container recipe: containers/nope.conf"));
}

/* CC22: naming the target. */
static void
test_targets(void)
{
	static const char *const tg[] = {
		"containers/x.conf", "pkg = [ \"a\" ];\n",
		NULL
	};

	resolves(mkroot("tg", tg), "containers/x");
	resolves(root, "containers/x.conf");
	CHECK(is(r.sr_name, "x"));
	refused(root, "x", "x: not a recipe: name one by its path, as "
	    "foundations/F, containers/X or systems/S");
	refused(root, "jails/x", "jails/x: not a recipe:");
	refused(root, "containersx/x", "not a recipe:");
	refused(root, "containers/../x", "containers/../x: not a recipe's "
	    "name: it must begin with a letter or a digit");
	refused(root, "containers/a.b", "not a recipe's name: it takes only");
	refused(root, "containers/web/nginx", "not a recipe's name");
	refused(root, "containers/", "containers/: not a recipe's name: "
	    "empty");
	refused(root, "containers/nope", "containers/nope.conf: no such "
	    "recipe in the configuration root");
	refused(root, "foundations/x", "foundations/x.conf: no such recipe");
}

/* CC23, CC26: every layer's problems, against the target's kind. */
static void
test_layer_errors(void)
{
	static const char *const le[] = {
		"default.conf", "frob = 1;\n",
		"common/k.conf", "kernconf = \"G\";\npkg = [ \"-x\" ];\n",
		"containers/x.conf",
		"import = \"common/k.conf\";\njailconf = 5;\n",
		NULL
	};

	CHECK(R(mkroot("le", le), "containers/x") == -1);
	CHECK(c.c_err.se_count == 4);
	CHECK(cap_has(&c, "default.conf: frob: not a key a recipe takes"));
	CHECK(cap_has(&c, "common/k.conf: kernconf: not a key of a "
	    "container's recipe: only a foundation's takes it"));
	CHECK(cap_has(&c, "common/k.conf: pkg[0]: not a package's name"));
	CHECK(cap_has(&c, "containers/x.conf: jailconf: expected a string, "
	    "not an integer: quote it"));
	CHECK(r.sr_merged == NULL);
}

/* CC25: patch sources across layers keep their first place. */
static void
test_patches(void)
{
	static const char *const pt[] = {
		"default.conf", "patches = [ \"github.com/a/one\" ];\n",
		"common/p.conf", "patches = [ \"github.com/b/two\" ];\n",
		"patches/local/0-x.patch", "\n",
		"foundations/f.conf",
		"import = \"common/p.conf\";\nsrc_branch = \"main\";\n"
		"patches { \"patches/local\" {} \"github.com/a/one\" { "
		"ref = \"r1\"; } }\n",
		NULL
	};
	const struct sh_node *one;

	resolves(mkroot("pt", pt), "foundations/f");
	CHECK(is(keys(M("patches")), "github.com/a/one github.com/b/two "
	    "patches/local"));
	one = sh_obj_get(M("patches"), "github.com/a/one");
	CHECK(one->sn_type == SH_OBJECT);
	CHECK(is(sh_obj_get(one, "ref")->sn_str, "r1"));
}

int
main(void)
{
	cap_open(&c);
	test_example();
	test_layers();
	test_import_errors();
	test_stacked();
	test_collections();
	test_nodrops();
	test_merged();
	test_targets();
	test_layer_errors();
	test_patches();
	sh_recipe_free(&r);
	cap_close(&c);
	scratch_clean();
	printf("check_cascade: %d checks\n", checks);
	return (0);
}
