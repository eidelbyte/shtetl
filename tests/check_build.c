/*
 * shtetl build F (planning.md 7.1): tests/MATRIX.md family F, over
 * the fake ZFS and a configuration root in the scratch directory.
 */

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "core/build.h"
#include "core/spaces.h"
#include "core/zfs.h"
#include "zfake.h"

static struct cap c, wc;
static struct sh_zfs *z;
static struct sh_spaces s;
static struct sh_fplan p;
static char root[1200];

static int
is(const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("got \"%s\", wanted \"%s\"\n", got, want);
		return (0);
	}
	return (1);
}

/* The configuration root DIR: shtetl.conf and FILES (name, text). */
static void
config(const char *dir, const char *const *files)
{
	char path[1200];
	size_t i;

	(void) snprintf(path, sizeof (path), "%s/shtetl.conf", dir);
	(void) scratch_write(path, "ver = \">=15.1\";\n"
	    "kernconf = \"GENERIC\";\n"
	    "pkg = [ \"sudo\" ];\n"
	    "datasets { tmp {} var {} \"usr/local\" {} }\n");
	for (i = 0; files != NULL && files[i] != NULL; i += 2) {
		(void) snprintf(path, sizeof (path), "%s/%s", dir, files[i]);
		(void) scratch_write(path, files[i + 1]);
	}
	(void) snprintf(root, sizeof (root), "%s", scratch_path(dir));
}

/* A bootstrapped box: a lake, a build root mounted, a config root. */
static void
world(void)
{
	zfake_reset();
	zfake_pool("tank");
	zfake_fs("tank/shtetl");
	zfake_set("tank/shtetl", "shtetl:space", "lake");
	zfake_fs("tank/shtetl/foundations");
	zfake_fs("tank/shtetl/foundation-builds");
	zfake_fs("tank/zbuild");
	zfake_set("tank/zbuild", "shtetl:space", "build");
	zfake_set("tank/zbuild", "mountpoint", "/usr/src");
	zfake_set("tank/zbuild", "mounted", "yes");
	zfake_fs("tank/zconfig");
	zfake_set("tank/zconfig", "shtetl:space", "config");
	sh_spaces_free(&s);
	cap_reset(&c);
	CHECK(sh_spaces_discover(z, &s, &c.c_err) == 0);
}

/* The plan for TARGET: 0, or -1 with the reasons in c. */
static int
plan(const char *target)
{
	sh_build_free(&p);
	cap_reset(&c);
	cap_reset(&wc);
	return (sh_build_plan(z, &s, NULL, root, target, "/var/db/x", &p,
	    &c.c_err, &wc.c_err));
}

/* Refused with one message holding WANT. */
static void
refused(const char *target, const char *want)
{
	int rc;

	rc = plan(target);
	if (rc == 0 || c.c_err.se_count != 1 || !cap_has(&c, want))
		printf("%s: wanted one refusal with \"%s\", got:\n%s", target,
		    want, cap_text(&c));
	CHECK(rc == -1 && c.c_err.se_count == 1 && cap_has(&c, want));
}

static const char *const generic[] = {
	"foundations/generic.conf", "src_branch = \"releng/15.1\";\n",
	"containers/x.conf", "pkg = [ \"nginx\" ];\n",
	NULL
};

/* F1 - F7: what is refused. */
static void
test_refused(void)
{
	static const char *const old[] = {
		"foundations/old.conf", "src_branch = \"releng/14.3\";\n",
		"foundations/bad.conf", "src_branch = \"releng/15.1\";\n"
		"frob = 1;\n",
		NULL
	};

	config("a", generic);
	world();
	/* F1 */
	CHECK(plan("generic") == 0 && is(p.fp_name, "generic"));
	CHECK(plan("foundations/generic") == 0 && is(p.fp_name, "generic"));
	CHECK(plan("foundations/generic.conf") == 0);
	CHECK(is(p.fp_ds, "tank/shtetl/foundations/generic"));
	CHECK(is(p.fp_obj, "tank/shtetl/foundation-builds/generic"));
	CHECK(is(p.fp_src, "tank/zbuild") && is(p.fp_srcdir, "/usr/src"));
	CHECK(is(p.fp_config, "tank/zconfig"));
	CHECK(is(p.fp_rundir, "/var/db/x/generic"));
	refused("containers/x", "containers/x: invalid foundation: use F or "
	    "foundations/F");
	refused("Gen.eric", "Gen.eric: invalid foundation name: only "
	    "letters, digits, - and _ allowed");
	refused("nope", "foundations/nope.conf: no such file");
	/* F2 */
	zfake_reset();
	zfake_pool("tank");
	sh_spaces_free(&s);
	CHECK(sh_spaces_discover(z, &s, &c.c_err) == 0);
	CHECK(plan("generic") == -1);
	CHECK(c.c_err.se_count == 3);
	CHECK(cap_has(&c, "no lake root\n") && cap_has(&c, "no config root\n"));
	CHECK(cap_has(&c, "no build root\n"));
	world();
	/* F3, F4 */
	config("b", old);
	refused("bad", "foundations/bad.conf: frob: invalid key");
	refused("old", "foundations/old.conf: ver: 14.3 does not satisfy "
	    ">=15.1 in shtetl.conf");
	/* F5 */
	config("a", generic);
	zfake_set("tank/zbuild", "shtetl:build", "gaming");
	refused("generic", "build of gaming in progress");
	world();
	/* F6 */
	zfake_set("tank/zbuild", "mounted", "no");
	refused("generic", "build root tank/zbuild: not mounted");
	zfake_set("tank/zbuild", "mountpoint", "none");
	refused("generic", "build root tank/zbuild: no mountpoint");
	world();
	/* F7 */
	zfake_fs("tank/shtetl/foundations/generic");
	refused("generic", "foundation generic exists");
}

/* The datasets planned, rel[*] each, space-separated. */
static const char *
dss(void)
{
	static char buf[1024];
	size_t i, n;

	buf[0] = '\0';
	for (i = 0; i < p.fp_nds; i++) {
		n = strlen(buf);
		(void) snprintf(buf + n, sizeof (buf) - n, "%s%s%s",
		    n > 0 ? " " : "", p.fp_dss[i].fd_rel[0] != '\0' ?
		    p.fp_dss[i].fd_rel : "F",
		    p.fp_dss[i].fd_placeholder ? "*" : "");
	}
	return (buf);
}

/* F8, F9: the datasets and the obj cache. */
static void
test_datasets(void)
{
	static const char *const nested[] = {
		"foundations/n.conf", "src_branch = \"main\";\n"
		"datasets { \"a/b/c\" { readonly = true; } a { "
		"compression = \"lz4\"; } }\n",
		NULL
	};
	char many[2048];
	const char *f[3];
	size_t i, n;

	config("c", nested);
	world();
	CHECK(plan("n") == 0);
	CHECK(is(dss(), "F tmp var usr* usr/local a a/b* a/b/c"));
	CHECK(is(p.fp_dss[0].fd_ds, "tank/shtetl/foundations/n"));
	CHECK(is(p.fp_dss[4].fd_ds, "tank/shtetl/foundations/n/usr/local"));
	CHECK(p.fp_dss[5].fd_props != NULL && !p.fp_dss[5].fd_placeholder);
	CHECK(p.fp_dss[7].fd_props != NULL && p.fp_dss[6].fd_props == NULL);
	/* more than fit */
	n = (size_t)snprintf(many, sizeof (many),
	    "src_branch = \"main\";\ndatasets {");
	for (i = 0; i < 40; i++)
		n += (size_t)snprintf(many + n, sizeof (many) - n, " d%zu {}",
		    i);
	(void) snprintf(many + n, sizeof (many) - n, " }\n");
	f[0] = "foundations/many.conf";
	f[1] = many;
	f[2] = NULL;
	config("d", f);
	refused("many", "foundations/many.conf: more than 32 datasets");
	/* F9 */
	config("a", generic);
	CHECK(plan("generic") == 0 && !p.fp_obj_there);
	zfake_fs("tank/shtetl/foundation-builds/generic");
	CHECK(plan("generic") == 0 && p.fp_obj_there);
}

/* F10: the plan shown. */
static void
test_show(void)
{
	static const char *const full[] = {
		"foundations/generic.conf", "src_branch = \"releng/15.1\";\n"
		"kernconf = <<EOD\nnooptions WITNESS\nEOD\n"
		"buildopts = \"WITHOUT_LIB32=yes\";\n"
		"datasets { \"usr/local\" { compression = \"zstd-9\"; "
		"exec = false; } }\n"
		"commands { motd = \"echo hi > /etc/motd\"; }\n",
		NULL
	};
	char *out;
	size_t len;
	FILE *f;

	config("e", full);
	world();
	CHECK(plan("generic") == 0);
	out = NULL;
	f = open_memstream(&out, &len);
	CHECK(f != NULL);
	sh_build_show(f, &p);
	CHECK(fclose(f) == 0);
	CHECK(is(out,
	    "foundation generic, new, from releng/15.1\n"
	    "  version 15.1-releng-<day>-<commit>\n"
	    "  + tank/shtetl/foundations/generic\n"
	    "  + tank/shtetl/foundations/generic/tmp\n"
	    "  + tank/shtetl/foundations/generic/var\n"
	    "  + tank/shtetl/foundations/generic/usr  canmount=off\n"
	    "  + tank/shtetl/foundations/generic/usr/local  "
	    "compression=zstd-9, exec=off\n"
	    "  + tank/shtetl/foundation-builds/generic\n"
	    "  checkout releng/15.1 in tank/zbuild (/usr/src)\n"
	    "  kernel GENERIC, lines from 1 file\n"
	    "  src.conf from 1 file\n"
	    "  make -j<hw.ncpu> buildworld buildkernel\n"
	    "  install world, kernel and distribution (install -C)\n"
	    "  pkg: sudo\n"
	    "  commands: motd\n"
	    "  snapshot -r tank/shtetl/foundations/generic@<version>\n"));
	free(out);
}

int
main(void)
{
	cap_open(&c);
	cap_open(&wc);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	test_refused();
	test_datasets();
	test_show();
	sh_build_free(&p);
	sh_spaces_free(&s);
	sh_zfs_close(z);
	cap_close(&wc);
	cap_close(&c);
	scratch_clean();
	printf("check_build: %d checks\n", checks);
	return (0);
}
