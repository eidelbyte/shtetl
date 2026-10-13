/*
 * shtetl build F (planning.md 7.1): tests/MATRIX.md family F, over
 * the fake ZFS and a configuration root in the scratch directory.
 */

#include <sys/stat.h>
#include <sys/utsname.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
static const char *runbase = "/var/db/x";

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
	return (sh_build_plan(z, &s, NULL, root, target, runbase, &p,
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

/* ---- the run ---------------------------------------------------- */

/* 2026-10-08 12:00:00 UTC */
#define	NOON	((time_t)1791460800)
#define	SHA	"0f691888dc56a1b2c3d4e5f60718293a4b5c6d7e"
#define	VERSION	"15.1-releng-2026-10-08-0f691888dc56"
#define	F	"tank/shtetl/foundations/generic"

static struct sh_tools fake;
static char toolpath[7][1200];
static char *ran;		/* what the run printed */

/*
 * A script NAME in the scratch tools directory, logging its calls and
 * failing while the scratch file fail/NAME is there; BODY then runs.
 */
static const char *
tool(const char *name, const char *body)
{
	char text[4096], log[1200], fail[1200], rel[64];
	static char path[7][1200];
	static int n;

	(void) snprintf(log, sizeof (log), "%s", scratch_path("tools.log"));
	(void) snprintf(fail, sizeof (fail), "%s", scratch_path("fail"));
	(void) snprintf(text, sizeof (text), "#!/bin/sh\n"
	    "echo \"%s $*\" >> %s\n"
	    "[ -f %s/%s ] && { echo \"%s failed\" >&2; exit 3; }\n%s"
	    "exit 0\n", name, log, fail, name, name, body);
	(void) snprintf(rel, sizeof (rel), "tools/%s", name);
	(void) scratch_write(rel, text);
	n = (n + 1) % 7;
	(void) snprintf(path[n], sizeof (path[n]), "%s", scratch_path(rel));
	CHECK(chmod(path[n], 0755) == 0);
	return (path[n]);
}

/* The fakes, and a checkout whose newvers.sh says REVISION. */
static void
fakes(const char *revision)
{
	char body[2048], nv[256], log[1200], fail[1200];

	(void) snprintf(body, sizeof (body),
	    "case \"$3\" in\n"
	    "diff) [ -f %s/dirty ] && exit 1;;\n"
	    "rev-parse) echo " SHA ";;\n"
	    "esac\n", scratch_path("fail"));
	(void) snprintf(toolpath[0], sizeof (toolpath[0]), "%s",
	    tool("git", body));
	/* scratch_path's buffer is one: each path copied before the next */
	(void) snprintf(log, sizeof (log), "%s", scratch_path("tools.log"));
	(void) snprintf(fail, sizeof (fail), "%s", scratch_path("fail"));
	(void) snprintf(body, sizeof (body),
	    "echo \"make env MAKEOBJDIRPREFIX=$MAKEOBJDIRPREFIX\" >> %s\n"
	    "d=; t=\n"
	    "for a; do case \"$a\" in DESTDIR=*) d=${a#DESTDIR=};; "
	    "buildworld|installworld|installkernel|distribution) t=$a;; "
	    "esac; done\n"
	    "[ -f %s/make-$t ] && { echo \"make $t failed\" >&2; exit 2; }\n"
	    "[ -n \"$d\" ] && mkdir -p \"$d/etc\" && echo x > \"$d/etc/$t\"\n",
	    log, fail);
	(void) snprintf(toolpath[1], sizeof (toolpath[1]), "%s",
	    tool("make", body));
	(void) snprintf(toolpath[2], sizeof (toolpath[2]), "%s",
	    tool("pkg", ""));
	(void) snprintf(body, sizeof (body),
	    "case \"$5\" in *fail*) exit 4;; esac\n");
	(void) snprintf(toolpath[3], sizeof (toolpath[3]), "%s",
	    tool("chroot", body));
	(void) snprintf(toolpath[4], sizeof (toolpath[4]), "%s",
	    tool("mount", ""));
	(void) snprintf(toolpath[5], sizeof (toolpath[5]), "%s",
	    tool("umount", ""));
	fake.tl_git = toolpath[0];
	fake.tl_make = toolpath[1];
	fake.tl_pkg = toolpath[2];
	fake.tl_chroot = toolpath[3];
	fake.tl_mount = toolpath[4];
	fake.tl_umount = toolpath[5];
	fake.tl_sh = "/bin/sh";
	(void) snprintf(nv, sizeof (nv), "[ \"$1\" = -V ] && "
	    "echo \"$2=\\\"%s\\\"\"\n", revision);
	(void) scratch_write("src/sys/conf/newvers.sh", nv);
	(void) unlink(scratch_path("tools.log"));
	(void) scratch_write("fail/.keep", "");
}

/* The box of world(), as bootstrap leaves it, the build root at src. */
static void
runworld(void)
{
	world();
	zfake_set("tank/shtetl", "mountpoint", "none");
	zfake_set("tank/zbuild", "mountpoint", scratch_path("src"));
	runbase = scratch_path("run");
	runbase = strdup(runbase);
}

/* What the fakes were asked, and what the run printed. */
static char *
slurp(const char *path)
{
	static char buf[65536];
	FILE *f;
	size_t n;

	buf[0] = '\0';
	if ((f = fopen(path, "r")) == NULL)
		return (buf);
	n = fread(buf, 1, sizeof (buf) - 1, f);
	buf[n] = '\0';
	(void) fclose(f);
	return (buf);
}

/* Whether the strings WANT (NULL-ended) are in TEXT, in that order. */
static int
in_order(const char *text, const char *const *want)
{
	const char *at = text;

	for (; *want != NULL; want++) {
		if ((at = strstr(at, *want)) == NULL) {
			printf("not found, in order: \"%s\" in:\n%s\n", *want,
			    text);
			return (0);
		}
		at += strlen(*want);
	}
	return (1);
}

/* The plan for generic, run with MSG: its return. */
static int
build(const char *msg)
{
	size_t len;
	FILE *f;
	int rc;

	CHECK(plan("generic") == 0);
	free(ran);
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	cap_reset(&c);
	rc = sh_build_run(z, &p, &fake, msg, NOON, f, &c.c_err);
	CHECK(fclose(f) == 0);
	if (rc != 0)
		printf("%s", "");
	return (rc);
}

static const char *const recipe[] = {
	"common/cmd.conf", "commands { setup = \"echo one\"; }\n",
	"foundations/generic.conf", "import = \"common/cmd.conf\";\n"
	"src_branch = \"releng/15.1\";\n"
	"kernconf = <<EOD\nnooptions WITNESS\nEOD\n"
	"buildopts = \"WITHOUT_LIB32=yes\";\n"
	"datasets { \"usr/local\" { compression = \"zstd-9\"; } }\n"
	"commands { setup = <<EOD\necho two\nEOD\n}\n",
	NULL
};

/* PROP of DS, or "(refused)". */
static const char *
prop(const char *ds, const char *name, enum sh_zsrc *src)
{
	static char val[512];

	if (sh_zfs_get(z, ds, name, val, sizeof (val), src, &wc.c_err) != 0)
		return ("(refused)");
	return (val);
}

/* F11, F16, F17: a whole run. */
static void
test_run(void)
{
	static const char *const steps[] = {
		"git -C ", "src diff --quiet HEAD",
		"git -C ", "src fetch origin releng/15.1",
		"git -C ", "src checkout -q --detach FETCH_HEAD",
		"git -C ", "src rev-parse HEAD",
		"make -C ", "src -j", " buildworld buildkernel SRCCONF=",
		"run/generic/src.conf KERNCONFDIR=",
		"run/generic KERNCONF=GENERIC",
		"make env MAKEOBJDIRPREFIX=", "run/generic/obj",
		"make -C ", " installworld DESTDIR=", "run/generic/root "
		"INSTALL_CMD=install -C SRCCONF=",
		"make -C ", " installkernel DESTDIR=",
		"make -C ", " distribution DESTDIR=",
		"mount -t devfs devfs ", "run/generic/root/dev",
		"pkg -c ", "run/generic/root install -y -- sudo",
		"chroot ", "run/generic/root /bin/sh -e -c echo one",
		"chroot ", "run/generic/root /bin/sh -e -c echo two",
		"umount ", "run/generic/root/dev",
		NULL
	};
	static const char *const said[] = {
		"snapshot tank/zconfig@build-generic-20261008-120000\n",
		"made " F "\n", "made " F "/usr/local\n",
		"made tank/shtetl/foundation-builds/generic\n",
		"checkout releng/15.1 at 0f691888dc56\n",
		"version " VERSION " (REVISION 15.1)\n",
		"make buildworld buildkernel (log ",
		"make installworld", "make installkernel", "make distribution",
		"pkg install in ", "commands setup, from common/cmd.conf\n",
		"commands setup, from foundations/generic.conf\n",
		"snapshot -r " F "@" VERSION "\n",
		"built " F "@" VERSION "\n",
		NULL
	};
	struct utsname u;
	char want[2048], guid[64];
	enum sh_zsrc src;

	config("run", recipe);
	runworld();
	fakes("15.1");
	CHECK(build(NULL) == 0);
	if (c.c_err.se_count != 0)
		printf("%s", cap_text(&c));
	CHECK(in_order(slurp(scratch_path("tools.log")), steps));
	CHECK(in_order(ran, said));
	/* the version, on every dataset, with its properties */
	CHECK(zfake_snapshot(F "@" VERSION));
	CHECK(zfake_snapshot(F "/usr@" VERSION));
	CHECK(zfake_snapshot(F "/usr/local@" VERSION));
	CHECK(zfake_snapshot(F "/tmp@" VERSION));
	CHECK(zfake_snapshot(F "/var@" VERSION));
	CHECK(is(prop(F "/var@" VERSION, "shtetl:kind", NULL), "version"));
	CHECK(is(prop(F "@" VERSION, "shtetl:commit", NULL),
	    "build releng/15.1 at 0f691888dc56"));
	CHECK(is(prop(F "@" VERSION, "shtetl:config", NULL),
	    "tank/zconfig@build-generic-20261008-120000"));
	(void) snprintf(guid, sizeof (guid), "%s",
	    prop("tank/zconfig@build-generic-20261008-120000", "guid", NULL));
	CHECK(is(prop(F "@" VERSION, "shtetl:config-guid", NULL), guid));
	CHECK(is(prop("tank/zconfig@build-generic-20261008-120000",
	    "shtetl:kind", NULL), "config"));
	/* the datasets as made */
	CHECK(is(prop(F "/usr", "canmount", &src), "off") &&
	    src == SH_ZSRC_LOCAL);
	CHECK(is(prop(F "/usr/local", "compression", &src), "zstd-9") &&
	    src == SH_ZSRC_LOCAL);
	/* everything back */
	CHECK(is(prop(F, "mountpoint", &src), "none") &&
	    src == SH_ZSRC_INHERITED);
	CHECK(is(prop(F, "mounted", NULL), "no"));
	CHECK(is(prop(F "/usr/local", "mounted", NULL), "no"));
	CHECK(is(prop("tank/shtetl/foundation-builds/generic", "mounted",
	    NULL), "no"));
	(void) prop("tank/shtetl/foundation-builds/generic", "mountpoint",
	    &src);
	CHECK(src == SH_ZSRC_INHERITED);
	(void) prop("tank/zbuild", "shtetl:build", &src);
	CHECK(src == SH_ZSRC_NONE);
	/* F16: the configs */
	CHECK(uname(&u) == 0);
	(void) snprintf(want, sizeof (want),
	    "# written by shtetl for foundation generic\n"
	    "include \"%s/sys/%s/conf/GENERIC\"\nident GENERIC\n"
	    "nooptions WITNESS\n", scratch_path("src"), u.machine);
	CHECK(is(slurp(scratch_path("run/generic/GENERIC")), want));
	CHECK(is(slurp(scratch_path("run/generic/src.conf")),
	    "WITHOUT_LIB32=yes\n"));
	/* the install reached F's mount, and the copy of resolv.conf went */
	CHECK(is(slurp(scratch_path("run/generic/root/etc/installworld")),
	    "x\n"));
	CHECK(access(scratch_path("run/generic/root/etc/resolv.conf"), F_OK)
	    != 0);

	/* built: a second build of it is an update's, and refused here */
	CHECK(plan("generic") == -1);
	CHECK(cap_has(&c, "foundation generic exists"));
}

/* F12 - F15: refusals and failures, each undone. */
static void
test_failures(void)
{
	static const char *const fail[] = { "fail/git", "fail/make-buildworld",
	    "fail/make-installkernel", "fail/pkg", "fail/mount",
	    "fail/dirty" };
	static const char *const failcmd[] = {
		"foundations/generic.conf", "src_branch = \"releng/15.1\";\n"
		"commands { x = \"fail here\"; }\n",
		NULL
	};
	static const char *const bare[] = {
		"foundations/generic.conf", "src_branch = \"releng/15.1\";\n"
		"commands { x = \"echo\"; }\n",
		NULL
	};
	enum sh_zsrc src;
	size_t i;

	config("run", recipe);
	for (i = 0; i < sizeof (fail) / sizeof (fail[0]); i++) {
		runworld();
		fakes("15.1");
		(void) scratch_write(fail[i], "");
		CHECK(build(NULL) == -1);
		if (!cap_has(&c, "build of generic failed; log "))
			printf("%s: %s", fail[i], cap_text(&c));
		CHECK(cap_has(&c, "build of generic failed; log "));
		CHECK(cap_has(&c, "run/generic/build.log"));
		/* F13: undone */
		CHECK(!zfake_exists(F) && !zfake_exists(F "/usr"));
		CHECK(zfake_snapshot("tank/zconfig@build-generic-"
		    "20261008-120000"));
		(void) prop("tank/zbuild", "shtetl:build", &src);
		CHECK(src == SH_ZSRC_NONE);
		if (zfake_exists("tank/shtetl/foundation-builds/generic")) {
			(void) prop("tank/shtetl/foundation-builds/generic",
			    "mountpoint", &src);
			CHECK(src != SH_ZSRC_LOCAL);
		}
		(void) unlink(scratch_path(fail[i]));
	}
	/* F14 */
	runworld();
	fakes("15.1");
	(void) scratch_write("fail/dirty", "");
	CHECK(build(NULL) == -1);
	CHECK(cap_has(&c, "build root tank/zbuild: changes of its own, or "
	    "no checkout"));
	(void) unlink(scratch_path("fail/dirty"));
	/* F12: REVISION against ver, after the checkout */
	runworld();
	fakes("15.0");
	CHECK(build(NULL) == -1);
	CHECK(cap_has(&c, "foundations/generic.conf: ver: 15.0 does not "
	    "satisfy >=15.1 in shtetl.conf"));
	CHECK(!zfake_exists(F));
	/* a command failing */
	config("cmd", failcmd);
	runworld();
	fakes("15.1");
	CHECK(build(NULL) == -1);
	CHECK(cap_has(&c, "chroot: exit status 4"));
	CHECK(!zfake_exists(F));
	/* F15: no packages but commands: devfs, no resolv.conf */
	(void) scratch_write("bare/shtetl.conf", "ver = \">=15.1\";\n");
	(void) scratch_write("bare/foundations/generic.conf", bare[1]);
	(void) snprintf(root, sizeof (root), "%s", scratch_path("bare"));
	runworld();
	fakes("15.1");
	CHECK(build("given") == 0);
	CHECK(strstr(slurp(scratch_path("tools.log")), "pkg ") == NULL);
	CHECK(strstr(slurp(scratch_path("tools.log")), "mount -t devfs") !=
	    NULL);
	CHECK(is(prop(F "@15.1-releng-2026-10-08-0f691888dc56",
	    "shtetl:commit", NULL), "given"));
	/* nothing to do inside: no devfs at all */
	(void) scratch_write("bare/foundations/generic.conf",
	    "src_branch = \"releng/15.1\";\n");
	runworld();
	fakes("15.1");
	CHECK(build(NULL) == 0);
	CHECK(strstr(slurp(scratch_path("tools.log")), "devfs") == NULL);
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
	test_run();
	test_failures();
	free(ran);
	sh_build_free(&p);
	sh_spaces_free(&s);
	sh_zfs_close(z);
	cap_close(&wc);
	cap_close(&c);
	scratch_clean();
	printf("check_build: %d checks\n", checks);
	return (0);
}
