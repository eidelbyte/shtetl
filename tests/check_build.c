/*
 * shtetl build F (planning.md 7.1): tests/MATRIX.md family F, over
 * the fake ZFS and a configuration root in the scratch directory.
 */

#include <sys/stat.h>
#include <sys/utsname.h>

#include <stdarg.h>
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
static enum sh_bverb verb = SH_BV_NEW;
static struct sh_tools fake;
static int seclevel;		/* what the fake securelevel says */

static int
fakelevel(void)
{
	return (seclevel);
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
	return (sh_build_plan(z, &s, NULL, root, target, runbase, &fake, verb,
	    &p, &c.c_err, &wc.c_err));
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
	/* F7, F21: there with no version: neither new nor an update */
	zfake_fs("tank/shtetl/foundations/generic");
	refused("generic", "foundation generic exists without a version");
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
	    "  patches: none\n"
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
	char body[4096], nv[256], log[1200], fail[1200];
	char kf[1200], kd[1200];

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
	/* what installkernel installs: kfiles, and kdebug while there */
	(void) snprintf(kf, sizeof (kf), "%s", scratch_path("kfiles"));
	(void) snprintf(kd, sizeof (kd), "%s", scratch_path("kdebug"));
	(void) scratch_write("kfiles/kernel", "kernel one\n");
	(void) snprintf(body, sizeof (body),
	    "echo \"make env MAKEOBJDIRPREFIX=$MAKEOBJDIRPREFIX\" >> %s\n"
	    "d=; t=\n"
	    "for a; do case \"$a\" in DESTDIR=*) d=${a#DESTDIR=};; "
	    "buildworld|installworld|installkernel|distribution) t=$a;; "
	    "esac; done\n"
	    "[ -f %s/make-$t ] && { echo \"make $t failed\" >&2; exit 2; }\n"
	    "[ -n \"$d\" ] && mkdir -p \"$d/etc\" && echo x > \"$d/etc/$t\"\n"
	    "[ -f \"$2/a.c\" ] && cp \"$2/a.c\" \"$2/../seen-a.c\"\n"
	    "[ -f \"$2/m.c\" ] && cp \"$2/m.c\" \"$2/../seen-m.c\"\n"
	    "if [ \"$t\" = installkernel ] && [ ! -f %s/nokernel ]; then\n"
	    "  mkdir -p \"$d/boot/kernel\" && cp -p %s/* \"$d/boot/kernel/\"\n"
	    "  [ -d %s ] && mkdir -p \"$d/usr/lib/debug/boot/kernel\" &&\n"
	    "    cp -p %s/* \"$d/usr/lib/debug/boot/kernel/\"\n"
	    "fi\n",
	    log, fail, fail, kf, kd, kd);
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
	fake.tl_securelevel = fakelevel;
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

	if (plan("generic") != 0) {
		printf("plan refused:\n%s", cap_text(&c));
		CHECK(0);
	}
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

/* Whether the file of the run directory REL is there. */
static int
inrun(const char *rel)
{
	char path[1400];

	(void) snprintf(path, sizeof (path), "%s/generic/%s", runbase, rel);
	return (access(path, F_OK) == 0);
}

/* Whether REL in F's mount, the run's root, is there. */
static int
inroot(const char *rel)
{
	char path[1400];

	(void) snprintf(path, sizeof (path), "%s/generic/root/%s", runbase,
	    rel);
	return (access(path, F_OK) == 0);
}

/* F20 - F25: a foundation built again, an update (planning.md 7.2). */
static void
test_update(void)
{
	static const char *const extra[] = {
		"foundations/generic.conf", "src_branch = \"releng/15.1\";\n"
		"datasets { opt {} }\n",
		NULL
	};
	static const char *const backups[] = { "boot/kernel.old/kernel",
	    "boot/loader.old", "libexec/ld-elf.so.1.old",
	    "libexec/ld-elf32.so.1.old", "sbin/init.bak", NULL };
	static const char *const kept[] = { "boot/kernel/kernel",
	    "boot/loader", "libexec/ld-elf.so.1", "sbin/init",
	    "sbin/init.bak2", "etc/rc.old", "boot/.old", NULL };
	static const char *const kinstall[] = {
		"make -C ", " installkernel DESTDIR=",
		"run/generic/obj/kernel-install INSTALL_CMD=install -C ", NULL
	};
	static const char *const order[] = {
		"make -C ", " buildworld buildkernel ",
		"make -C ", " delete-old delete-old-libs DESTDIR=",
		"root BATCH_DELETE_OLD_FILES=yes SRCCONF=",
		"make -C ", " installworld DESTDIR=", NULL
	};
	char rel[256], text[1400], *out;
	struct stat st;
	ino_t ino;
	size_t i, len;
	FILE *f;

	runworld();
	fakes("15.1");
	config("u", recipe);
	/*
	 * F34: the kernel, a debug file with it, into an empty F; the
	 * fake's etc/installkernel stands for anything else it installs
	 */
	/* scratch_path's one buffer: a call to each command */
	(void) snprintf(text, sizeof (text), "rm -rf %s",
	    scratch_path("run/generic/root"));
	CHECK(system(text) == 0);
	(void) snprintf(text, sizeof (text), "rm -rf %s",
	    scratch_path("kdebug"));
	CHECK(system(text) == 0);
	(void) scratch_write("kfiles/a.ko", "a\n");
	(void) scratch_write("kfiles/b.ko", "b\n");
	(void) scratch_write("kdebug/kernel.debug", "debug one\n");
	CHECK(build(NULL) == 0);
	CHECK(strstr(ran, "kernel into " F ": 0 kept, 5 copied, 0 removed\n")
	    != NULL);
	CHECK(inroot("usr/lib/debug/boot/kernel/kernel.debug"));
	(void) snprintf(text, sizeof (text), "%s",
	    scratch_path("run/generic/root/boot/kernel/a.ko"));
	CHECK(stat(text, &st) == 0);
	ino = st.st_ino;
	CHECK(!inrun("obj/kernel-install"));
	/* F20 */
	CHECK(plan("generic") == 0 && p.fp_update == 1);
	CHECK(is(p.fp_last, VERSION));
	out = NULL;
	f = open_memstream(&out, &len);
	CHECK(f != NULL);
	sh_build_show(f, &p);
	CHECK(fclose(f) == 0);
	CHECK(strstr(out, "foundation generic, an update of @" VERSION
	    ", from releng/15.1\n  version ") != NULL);
	CHECK(strstr(out, "  = " F "\n  = " F "/tmp\n") != NULL);
	CHECK(strstr(out, "  = " F "/usr/local\n") != NULL);
	CHECK(strstr(out, "  make delete-old delete-old-libs, then\n"
	    "  install world, kernel and distribution (install -C)\n"
	    "  installworld's backups removed\n") != NULL);
	free(out);
	/* F22 */
	seclevel = 1;
	refused("generic", "securelevel 1: schg flags cannot be cleared");
	seclevel = 0;
	/* F21 */
	zfake_fs(F "/extra");
	refused("generic", F "/extra: not in the recipe");
	CHECK(sh_zfs_destroy(z, F "/extra", &wc.c_err) == 0);
	config("u2", extra);
	refused("generic", "foundations/generic.conf: datasets: opt: not in "
	    "foundation generic");
	config("u", recipe);

	/* F23, F24: the run; installworld's backups, some immutable */
	for (i = 0; backups[i] != NULL; i++) {
		(void) snprintf(rel, sizeof (rel), "run/generic/root/%s",
		    backups[i]);
		(void) scratch_write(rel, "old\n");
	}
	for (i = 0; kept[i] != NULL; i++) {
		(void) snprintf(rel, sizeof (rel), "run/generic/root/%s",
		    kept[i]);
		(void) scratch_write(rel, "new\n");
	}
	(void) snprintf(text, sizeof (text), "%s",
	    scratch_path("run/generic/root/sbin/init.bak"));
	CHECK(chflags(text, UF_IMMUTABLE) == 0);
	(void) snprintf(text, sizeof (text), "%s",
	    scratch_path("run/generic/root/boot/kernel.old"));
	CHECK(chflags(text, UF_IMMUTABLE) == 0);
	/* F34: one module the same, one changed, one new, one gone, no debug */
	(void) scratch_write("kfiles/kernel", "kernel two\n");
	(void) scratch_write("kfiles/c.ko", "c\n");
	(void) unlink(scratch_path("kfiles/b.ko"));
	(void) snprintf(text, sizeof (text), "rm -rf %s",
	    scratch_path("kdebug"));
	CHECK(system(text) == 0);
	(void) unlink(scratch_path("tools.log"));
	CHECK(build(NULL) == 0);
	CHECK(strstr(ran, "kernel into " F ": 2 kept, 2 copied, 2 removed\n")
	    != NULL);
	(void) snprintf(text, sizeof (text), "%s",
	    scratch_path("run/generic/root/boot/kernel/a.ko"));
	CHECK(stat(text, &st) == 0 && st.st_ino == ino);
	CHECK(is(slurp(scratch_path("run/generic/root/boot/kernel/kernel")),
	    "kernel two\n"));
	CHECK(inroot("boot/kernel/c.ko") && !inroot("boot/kernel/b.ko"));
	CHECK(!inroot("usr/lib/debug/boot/kernel"));
	CHECK(in_order(slurp(scratch_path("tools.log")), kinstall));
	/* the second build in the second: the config snapshot's -2 */
	CHECK(strstr(ran, "snapshot tank/zconfig@build-generic-"
	    "20261008-120000-2\n") != NULL);
	CHECK(strstr(ran, "made " F) == NULL);
	/* pkg and the commands again: the installs undid them */
	CHECK(strstr(ran, "pkg install in ") != NULL);
	CHECK(strstr(ran, "commands setup, from common/cmd.conf\n"
	    "commands setup, from foundations/generic.conf\n") != NULL);
	CHECK(strstr(ran, "removed installworld's backups: boot/kernel.old, "
	    "boot/loader.old, libexec/ld-elf.so.1.old, "
	    "libexec/ld-elf32.so.1.old, sbin/init.bak\n") != NULL);
	CHECK(strstr(ran, "built " F "@" VERSION ".2\n") != NULL);
	CHECK(in_order(slurp(scratch_path("tools.log")), order));
	for (i = 0; backups[i] != NULL; i++)
		CHECK(!inroot(backups[i]));
	CHECK(!inroot("boot/kernel.old"));
	for (i = 0; kept[i] != NULL; i++)
		CHECK(inroot(kept[i]));
	CHECK(zfake_snapshot(F "/usr/local@" VERSION) &&
	    zfake_snapshot(F "/usr/local@" VERSION ".2"));
	CHECK(is(prop("tank/zbuild", "shtetl:build", NULL), "-"));

	/* F34: installkernel that leaves no boot/kernel fails the build */
	(void) scratch_write("fail/nokernel", "");
	CHECK(build(NULL) == -1);
	(void) unlink(scratch_path("fail/nokernel"));
	CHECK(cap_has(&c, "installkernel: no boot/kernel in "));
	CHECK(inroot("boot/kernel/kernel"));
	/* F25: a failure: F back to its last version, nothing destroyed */
	(void) scratch_write("fail/make-installworld", "");
	CHECK(build(NULL) == -1);
	(void) unlink(scratch_path("fail/make-installworld"));
	CHECK(strstr(ran, "rolled back " F " to @" VERSION ".2\n") != NULL);
	CHECK(strstr(ran, "rolled back " F "/usr/local to @" VERSION ".2\n")
	    != NULL);
	CHECK(strstr(ran, "destroyed") == NULL);
	CHECK(zfake_exists(F) && zfake_exists(F "/usr/local"));
	CHECK(is(zfake_last_rollback(), F "@" VERSION ".2"));
	CHECK(is(prop("tank/zbuild", "shtetl:build", NULL), "-"));
	CHECK(!zfake_snapshot(F "@" VERSION ".3"));
}

/* The pkg-upgrade plan for TARGET: 0, or -1 with the reasons in c. */
static int
pkgplan(const char *target)
{
	sh_build_free(&p);
	cap_reset(&c);
	return (sh_pkg_plan(z, &s, target, runbase, &p, &c.c_err));
}

/* pkg-upgrade of generic run with MSG: its return. */
static int
pkgup(const char *msg)
{
	size_t len;
	FILE *f;
	int rc;

	if (pkgplan("generic") != 0) {
		printf("pkg plan refused:\n%s", cap_text(&c));
		CHECK(0);
	}
	free(ran);
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	cap_reset(&c);
	rc = sh_pkg_run(z, &p, &fake, msg, f, &c.c_err);
	CHECK(fclose(f) == 0);
	return (rc);
}

/* F28 - F31: shtetl pkg-upgrade F. */
static void
test_pkgup(void)
{
	static const char *const steps[] = {
		"mount -t devfs devfs ", "run/generic/root/dev",
		"pkg -c ", "run/generic/root upgrade -y",
		"umount ", "run/generic/root/dev", NULL
	};
	char *out;
	size_t len;
	FILE *f;

	runworld();
	fakes("15.1");
	config("k", recipe);
	/* F29 */
	CHECK(pkgplan("generic") == -1);
	CHECK(cap_has(&c, "no foundation generic"));
	CHECK(pkgplan("Gen.eric") == -1);
	zfake_fs(F);
	CHECK(pkgplan("generic") == -1);
	CHECK(cap_has(&c, "foundation generic exists without a version"));
	CHECK(sh_zfs_destroy(z, F, &wc.c_err) == 0);
	CHECK(build(NULL) == 0);
	zfake_set("tank/zbuild", "shtetl:build", "gaming");
	CHECK(pkgplan("generic") == -1);
	CHECK(cap_has(&c, "build of gaming in progress"));
	CHECK(sh_zfs_inherit(z, "tank/zbuild", "shtetl:build",
	    &wc.c_err) == 0);
	/* F28 */
	CHECK(pkgplan("generic") == 0);
	CHECK(is(p.fp_last, VERSION) && is(p.fp_next, VERSION "-pkg"));
	out = NULL;
	f = open_memstream(&out, &len);
	CHECK(f != NULL);
	sh_pkg_show(f, &p);
	CHECK(fclose(f) == 0);
	CHECK(strstr(out, "foundation generic, its packages upgraded, from @"
	    VERSION "\n") != NULL);
	CHECK(strstr(out, "  snapshot -r " F "@" VERSION "-pkg\n") != NULL);
	free(out);
	/* F30 */
	(void) unlink(scratch_path("tools.log"));
	CHECK(pkgup(NULL) == 0);
	CHECK(in_order(slurp(scratch_path("tools.log")), steps));
	CHECK(strstr(ran, "upgraded " F "@" VERSION "-pkg\n") != NULL);
	CHECK(is(prop(F "/usr/local@" VERSION "-pkg", "shtetl:kind", NULL),
	    "pkg"));
	CHECK(is(prop(F "@" VERSION "-pkg", "shtetl:commit", NULL),
	    "pkg upgrade of " VERSION));
	CHECK(is(prop(F "@" VERSION "-pkg", "shtetl:config", NULL),
	    prop(F "@" VERSION, "shtetl:config", NULL)));
	CHECK(is(prop(F "@" VERSION "-pkg", "shtetl:patches", NULL), "-"));
	CHECK(is(prop(F, "mounted", NULL), "no"));
	CHECK(is(prop(F "/usr/local", "mounted", NULL), "no"));
	CHECK(is(prop(F "/usr", "mounted", NULL), "no"));
	CHECK(is(prop("tank/zbuild", "shtetl:build", NULL), "-"));
	/* again: from the -pkg, its version's -pkg.2 */
	CHECK(pkgplan("generic") == 0);
	CHECK(is(p.fp_last, VERSION "-pkg") &&
	    is(p.fp_next, VERSION "-pkg.2"));
	CHECK(pkgup("the message") == 0);
	CHECK(is(prop(F "@" VERSION "-pkg.2", "shtetl:commit", NULL),
	    "the message"));
	/* F31 */
	(void) scratch_write("fail/pkg", "");
	CHECK(pkgup(NULL) == -1);
	(void) unlink(scratch_path("fail/pkg"));
	CHECK(cap_has(&c, "pkg upgrade of generic failed; log "));
	CHECK(strstr(ran, "rolled back " F " to @" VERSION "-pkg.2\n") !=
	    NULL);
	CHECK(!zfake_snapshot(F "@" VERSION "-pkg.3"));
	CHECK(is(prop("tank/zbuild", "shtetl:build", NULL), "-"));
	CHECK(inrun("pkg.log"));
	/* an update after it starts from the -pkg */
	CHECK(plan("generic") == 0 && is(p.fp_last, VERSION "-pkg.2"));
}

/* ---- family P: patches ------------------------------------------ */

/* CMD, made from FMT, run by the shell: it must succeed. */
static void
sh(const char *fmt, ...)
{
	char cmd[8192];
	va_list ap;

	va_start(ap, fmt);
	(void) vsnprintf(cmd, sizeof (cmd), fmt, ap);
	va_end(ap);
	if (system(cmd) != 0) {
		printf("failed: %s\n", cmd);
		CHECK(0);
	}
}

/* What CMD prints, its first line. */
static const char *
said(const char *cmd)
{
	static char line[512];
	FILE *f;

	line[0] = '\0';
	if ((f = popen(cmd, "r")) != NULL) {
		if (fgets(line, sizeof (line), f) == NULL)
			line[0] = '\0';
		(void) pclose(f);
	}
	line[strcspn(line, "\n")] = '\0';
	return (line);
}

static char tree[1200], snaps[1200], pgit[1200];

/*
 * The fake's snapshots, rollbacks and destroys of the build root's
 * snapshots carried out on the tree: copies of it in snaps.
 */
static void
treehook(const char *op, const char *name, void *arg)
{
	const char *at = strchr(name, '@');

	(void) arg;
	if (strncmp(name, "tank/zconfig@", 13) == 0 &&
	    strcmp(op, "snapshot") == 0) {
		/* the snapshot's files where a resumed build reads them */
		sh("rm -rf %s.snap && cp -Rp %s %s.snap && "
		    "rm -rf %s.snap/.zfs && mkdir -p %s/.zfs/snapshot && "
		    "rm -rf %s/.zfs/snapshot/%s && "
		    "mv %s.snap %s/.zfs/snapshot/%s", root, root, root, root,
		    root, root, at + 1, root, root, at + 1);
		return;
	}
	if (strncmp(name, "tank/zbuild@", 12) != 0 || at == NULL)
		return;
	if (strcmp(op, "snapshot") == 0)
		sh("rm -rf %s/%s && mkdir -p %s && cp -Rp %s %s/%s", snaps,
		    at + 1, snaps, tree, snaps, at + 1);
	else if (strcmp(op, "rollback") == 0)
		sh("rm -rf %s && cp -Rp %s/%s %s", tree, snaps, at + 1, tree);
	else
		sh("rm -rf %s/%s", snaps, at + 1);
}

#define	GIT	"git -c user.name=t -c user.email=t@t " \
	"-c init.defaultBranch=main"

static const char base_a[] = "one\ntwo\nthree\nfour\nfive\nsix\n";
static const char base_m[] =
	"l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\n";

static const char p_two[] =
	"--- a/a.c\n+++ b/a.c\n@@ -1,3 +1,3 @@\n one\n-two\n+TWO\n three\n";
static const char p_five[] =
	"--- a/a.c\n+++ b/a.c\n@@ -4,3 +4,3 @@\n four\n-five\n+FIVE\n six\n";
static const char p_new[] =
	"diff --git a/c.c b/c.c\nnew file mode 100644\n"
	"--- /dev/null\n+++ b/c.c\n@@ -0,0 +1 @@\n+int c;\n";
static const char p_in[] =
	"--- a/b.c\n+++ b/b.c\n@@ -1 +1 @@\n-broken\n+fixed\n";
static const char p_bad[] =
	"--- a/a.c\n+++ b/a.c\n@@ -1,3 +1,3 @@\n uno\n-dos\n+DOS\n tres\n";

/*
 * The build root a git checkout at tree, the fake git handing it
 * everything but the checkout's own fetch, checkout and commit; and a
 * patch repository, pgit: main with 15.1/1-a.patch, tagged t1, and v2
 * with 2-new.patch and 10-b.patch beside it and a README.
 */
static void
patchworld(void)
{
	char body[2048];

	runworld();
	fakes("15.1");
	(void) snprintf(tree, sizeof (tree), "%s", scratch_path("ptree"));
	(void) snprintf(snaps, sizeof (snaps), "%s", scratch_path("psnaps"));
	(void) snprintf(pgit, sizeof (pgit), "%s", scratch_path("pgit"));
	sh("rm -rf %s %s %s", tree, snaps, pgit);
	(void) snprintf(body, sizeof (body), "%s/sys/conf/newvers.sh", tree);
	sh("mkdir -p %s/sys/conf && cp %s %s", tree,
	    scratch_path("src/sys/conf/newvers.sh"), body);
	(void) snprintf(body, sizeof (body), "%s/a.c", tree);
	(void) scratch_write("ptree/a.c", base_a);
	(void) scratch_write("ptree/b.c", "fixed\n");
	(void) scratch_write("ptree/m.c", base_m);
	sh("cd %s && " GIT " init -q && " GIT " add -A && "
	    GIT " commit -qm base", tree);
	(void) scratch_write("pgit/15.1/1-a.patch", p_two);
	(void) scratch_write("pgit/README", "patches\n");
	sh("cd %s && " GIT " init -q && " GIT " add -A && "
	    GIT " commit -qm one && " GIT " tag t1 && "
	    GIT " checkout -q -b v2", pgit);
	(void) scratch_write("pgit/15.1/2-new.patch", p_new);
	(void) scratch_write("pgit/15.1/10-b.patch", p_in);
	sh("cd %s && " GIT " add -A && " GIT " commit -qm two && "
	    GIT " checkout -q main", pgit);
	zfake_set("tank/zbuild", "mountpoint", tree);
	(void) snprintf(body, sizeof (body),
	    "case \"$3\" in\n"
	    "diff) [ \"$4\" = --quiet ] && exit 0;;\n"
	    "fetch|checkout) [ \"$2\" = %s ] && exit 0;;\n"
	    "rev-parse) [ \"$2\" = %s ] && { echo " SHA "; exit 0; };;\n"
	    "esac\n"
	    "exec git \"$@\"\n", tree, tree);
	(void) snprintf(toolpath[0], sizeof (toolpath[0]), "%s",
	    tool("git", body));
	fake.tl_git = toolpath[0];
	zfake_hook(treehook, NULL);
}

/* The recipe generic with PATCHES, the patches block's inside. */
static void
patchconf(const char *name, const char *patches)
{
	char text[4096];
	const char *files[] = { "foundations/generic.conf", text,
	    "patches/local/1-d.patch", p_five,
	    "patches/local/notes.txt", "not a patch\n",
	    "patches/bad/1-x.patch", p_bad, NULL };

	(void) snprintf(text, sizeof (text), "src_branch = \"releng/15.1\";\n"
	    "patches { %s }\n", patches);
	config(name, files);
}

/* P3 - P7, P14 - P16: a build with patches, and its fetch. */
static void
test_patches(void)
{
	char text[4096], url[1300], commit[64], snap[256];
	struct stat st;
	char *out;
	size_t len;
	FILE *f;

	if (system("git --version >/dev/null 2>&1") != 0) {
		printf("check_build: no git: P3 - P16 not checked\n");
		return;
	}
	patchworld();
	(void) snprintf(url, sizeof (url), "file://%s", pgit);
	(void) snprintf(text, sizeof (text), "git -C %s rev-parse v2", pgit);
	(void) snprintf(commit, sizeof (commit), "%s", said(text));
	CHECK(strlen(commit) == 40);
	(void) snprintf(text, sizeof (text),
	    "\"%s\" { ref = \"v2\"; dir = \"15.1\"; } \"patches/local\" {}",
	    url);
	patchconf("pa", text);
	/* P16 */
	CHECK(plan("generic") == 0);
	out = NULL;
	f = open_memstream(&out, &len);
	CHECK(f != NULL);
	sh_build_show(f, &p);
	CHECK(fclose(f) == 0);
	(void) snprintf(text, sizeof (text), "  patches: %s (ref v2, "
	    "dir 15.1), patches/local\n", url);
	CHECK(strstr(out, text) != NULL);
	free(out);
	/* P3 - P7: two sources, each whole, each in natural order */
	(void) snprintf(text, sizeof (text), "touch -t 200001010000 %s/a.c "
	    "%s/b.c", tree, tree);
	sh("%s", text);
	CHECK(build(NULL) == 0);
	{
		char l1[1400], l2[1400], l3[1400], l4[1400];
		const char *const steps[] = { l1, "copied patches/local\n",
		    "4 patches from 2 sources\n",
		    "snapshot tank/zbuild@shtetl-patch-0\n", l2, l3, l4,
		    "patch 1-d.patch from patches/local: applied\n",
		    "make buildworld", "snapshot -r " F "@" VERSION "\n",
		    "rolled back tank/zbuild to @shtetl-patch-0\n",
		    "built " F "@" VERSION "\n", NULL };

		(void) snprintf(l1, sizeof (l1), "fetched %s at %.12s\n", url,
		    commit);
		(void) snprintf(l2, sizeof (l2), "patch 1-a.patch from %s: "
		    "applied\n", url);
		(void) snprintf(l3, sizeof (l3), "patch 2-new.patch from %s: "
		    "applied\n", url);
		(void) snprintf(l4, sizeof (l4), "patch 10-b.patch from %s: "
		    "already in, skipped\n", url);
		CHECK(in_order(ran, steps));
	}
	/* what make saw, patched; the tree after, as it was */
	CHECK(is(slurp(scratch_path("seen-a.c")),
	    "one\nTWO\nthree\nfour\nFIVE\nsix\n"));
	(void) snprintf(text, sizeof (text), "%s/a.c", tree);
	CHECK(is(slurp(text), base_a));
	(void) snprintf(text, sizeof (text), "%s/c.c", tree);
	CHECK(access(text, F_OK) != 0);
	/*
	 * P15: the patched files given the time of now, not the old ones
	 * the rollback gave back; c.c, made by a patch, gone with it
	 */
	CHECK(strstr(ran, "rolled back tank/zbuild to @shtetl-patch-0\n"
	    "the time of now on 2 patched files, for the next build's "
	    "make\n") != NULL);
	(void) snprintf(text, sizeof (text), "%s/a.c", tree);
	CHECK(stat(text, &st) == 0 && st.st_mtime > time(NULL) - 3600);
	(void) snprintf(text, sizeof (text), "%s/b.c", tree);
	CHECK(stat(text, &st) == 0 && st.st_mtime > time(NULL) - 3600);
	/* P5, P15: every patch snapshot gone, the rollback to the first */
	CHECK(is(zfake_last_rollback(), "tank/zbuild@shtetl-patch-0"));
	for (len = 0; len <= 4; len++) {
		(void) snprintf(snap, sizeof (snap),
		    "tank/zbuild@shtetl-patch-%zu", len);
		CHECK(!zfake_snapshot(snap));
	}
	/* P3: the list fixed in the run directory */
	(void) snprintf(text, sizeof (text), "%s/generic/patches/1/15.1/"
	    "10-b.patch", runbase);
	CHECK(access(text, F_OK) == 0);
	/* P14 */
	(void) snprintf(text, sizeof (text), "%s@%s: 10-b.patch already; "
	    "patches/local", url, commit);
	CHECK(is(prop(F "@" VERSION, "shtetl:patches", NULL), text));
	CHECK(is(prop(F "/usr/local@" VERSION, "shtetl:patches", NULL), text));
	CHECK(is(prop("tank/zbuild", "shtetl:build", NULL), "-"));

	/* P3: the remote's HEAD, a tag, a commit */
	patchworld();
	(void) snprintf(text, sizeof (text), "\"%s\" { dir = \"15.1\"; }", url);
	patchconf("pb", text);
	CHECK(build(NULL) == 0);
	CHECK(strstr(ran, "1 patch from 1 source\n") != NULL);
	patchworld();
	(void) snprintf(text, sizeof (text), "\"%s\" { ref = \"t1\"; "
	    "dir = \"15.1\"; }", url);
	patchconf("pc", text);
	CHECK(build(NULL) == 0);
	CHECK(strstr(ran, "1 patch from 1 source\n") != NULL);
	patchworld();
	/* the repository is made anew: its commit too */
	(void) snprintf(text, sizeof (text), "git -C %s rev-parse v2", pgit);
	(void) snprintf(commit, sizeof (commit), "%s", said(text));
	(void) snprintf(text, sizeof (text), "\"%s\" { ref = \"%s\"; "
	    "dir = \"15.1\"; }", url, commit);
	patchconf("pd", text);
	CHECK(build(NULL) == 0);
	CHECK(strstr(ran, "3 patches from 1 source\n") != NULL);

	/* P3: no repository, no ref: refused, undone, no snapshot taken */
	patchworld();
	patchconf("pe", "\"file:///nonexistent/shtetl-p\" {}");
	CHECK(build(NULL) == -1);
	CHECK(cap_has(&c, "patches file:///nonexistent/shtetl-p: fetch of "
	    "HEAD failed"));
	CHECK(!zfake_exists(F));
	CHECK(!zfake_snapshot("tank/zbuild@shtetl-patch-0"));
	CHECK(is(prop("tank/zbuild", "shtetl:build", NULL), "-"));
	patchworld();
	(void) snprintf(text, sizeof (text), "\"%s\" { ref = \"nope\"; }",
	    url);
	patchconf("pf", text);
	CHECK(build(NULL) == -1);
	(void) snprintf(text, sizeof (text), "patches %s: fetch of nope "
	    "failed", url);
	CHECK(cap_has(&c, text));

	/* P15: a failure after the patches, rolled back all the same */
	patchworld();
	patchconf("pg", "\"patches/local\" {}");
	(void) scratch_write("fail/make-buildworld", "");
	CHECK(build(NULL) == -1);
	(void) unlink(scratch_path("fail/make-buildworld"));
	CHECK(is(zfake_last_rollback(), "tank/zbuild@shtetl-patch-0"));
	CHECK(!zfake_snapshot("tank/zbuild@shtetl-patch-1"));
	(void) snprintf(text, sizeof (text), "%s/a.c", tree);
	CHECK(is(slurp(text), base_a));
	CHECK(!zfake_exists(F));

	/* no patches: no snapshot of the build root, no record */
	patchworld();
	config("pi", generic);
	CHECK(build(NULL) == 0);
	CHECK(strstr(ran, "shtetl-patch") == NULL);
	CHECK(is(prop(F "@" VERSION, "shtetl:patches", NULL), "-"));
	zfake_hook(NULL, NULL);
}

/* The build paused, carried on by VERB with MSG: its return. */
static int
resume(enum sh_bverb v, const char *msg)
{
	size_t len;
	FILE *f;
	int rc;

	verb = v;
	if (plan("generic") != 0) {
		printf("plan refused:\n%s", cap_text(&c));
		verb = SH_BV_NEW;
		return (-2);
	}
	verb = SH_BV_NEW;
	free(ran);
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	cap_reset(&c);
	rc = sh_build_run(z, &p, &fake, msg, NOON, f, &c.c_err);
	CHECK(fclose(f) == 0);
	return (rc);
}

/*
 * The patches of patches/pause in the configuration root: 1-m4, l4 to
 * L4, made by hand; 2-x4, l4 to X4, which conflicts with it; with
 * THIRD, 3-l1, l1 to L1, whose context l4 is no longer there but
 * which merges three ways. The last two by git, so they carry the
 * blobs they apply to.
 */
static void
pausepatches(int third)
{
	static const char m4[] = "--- a/m.c\n+++ b/m.c\n@@ -1,7 +1,7 @@\n"
	    " l1\n l2\n l3\n-l4\n+L4\n l5\n l6\n l7\n";
	char dir[1400];

	char path[1500];
	FILE *f;

	(void) snprintf(dir, sizeof (dir), "%s/patches/pause", root);
	sh("mkdir -p %s", dir);
	(void) snprintf(path, sizeof (path), "%s/1-m4.patch", dir);
	CHECK((f = fopen(path, "w")) != NULL);
	CHECK(fputs(m4, f) != EOF && fclose(f) == 0);
	sh("cd %s && sed 's/^l4$/X4/' m.c > m.t && mv m.t m.c && "
	    "git -c diff.noprefix=false diff --no-color > %s/2-x4.patch && "
	    "git checkout -q m.c", tree, dir);
	if (third)
		sh("cd %s && sed 's/^l1$/L1/' m.c > m.t && mv m.t m.c && "
		    "git -c diff.noprefix=false diff --no-color > "
		    "%s/3-l1.patch && git checkout -q m.c", tree, dir);
}

/* P8 - P13: a patch that does not apply, the pause, and its verbs. */
static void
test_pause(void)
{
	char text[4096], path[1400];

	if (system("git --version >/dev/null 2>&1") != 0) {
		printf("check_build: no git: P8 - P13 not checked\n");
		return;
	}
	/* P12: nothing paused */
	patchworld();
	patchconf("qz", "\"patches/local\" {}");
	verb = SH_BV_CONTINUE;
	CHECK(plan("generic") == -1);
	CHECK(cap_has(&c, "no build of generic paused"));
	zfake_set("tank/zbuild", "shtetl:build", "gaming");
	CHECK(plan("generic") == -1);
	CHECK(cap_has(&c, "no build of generic paused"));
	verb = SH_BV_NEW;

	/* P8: conflict markers; refused; fixed; merged; fixed; built */
	patchworld();
	patchconf("qa", "\"patches/pause\" {}");
	pausepatches(1);
	CHECK(build(NULL) == 1);
	CHECK(strstr(ran, "patch 1-m4.patch from patches/pause: applied\n")
	    != NULL);
	CHECK(strstr(ran, "patch 2-x4.patch from patches/pause: does not "
	    "apply; merged three ways, conflict markers left\n") != NULL);
	CHECK(strstr(ran, " m.c\nbuild of generic paused; then one of:\n"
	    "  shtetl build generic --continue") != NULL);
	CHECK(strstr(ran, "make ") == NULL && strstr(ran, "mounted") == NULL);
	CHECK(inrun(SH_BUILD_PAUSED));
	CHECK(is(prop("tank/zbuild", "shtetl:build", NULL), "generic"));
	CHECK(zfake_exists(F));
	CHECK(zfake_snapshot("tank/zbuild@shtetl-patch-2"));
	(void) snprintf(path, sizeof (path), "%s/m.c", tree);
	CHECK(strstr(slurp(path), "<<<<<<<") != NULL);
	/* P12: a build while one is paused */
	CHECK(plan("generic") == -1);
	CHECK(cap_has(&c, "build of generic paused"));
	/* P9: nothing fixed yet */
	CHECK(resume(SH_BV_CONTINUE, NULL) == -1);
	CHECK(cap_has(&c, "m.c: conflict marker left"));
	CHECK(cap_has(&c, "build of generic still paused"));
	CHECK(inrun(SH_BUILD_PAUSED) && zfake_exists(F));
	CHECK(is(prop("tank/zbuild", "shtetl:build", NULL), "generic"));
	/* fixed: X4 it is; then 3-l1 merges without conflict */
	(void) scratch_write("ptree/m.c",
	    "l1\nl2\nl3\nX4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\n");
	CHECK(resume(SH_BV_CONTINUE, NULL) == 1);
	(void) snprintf(text, sizeof (text), "patch 2-x4.patch from "
	    "patches/pause: fixed by hand; the fix: %s/generic/fixed/1/"
	    "2-x4.patch\n", runbase);
	CHECK(strstr(ran, text) != NULL);
	CHECK(strstr(ran, "patch 3-l1.patch from patches/pause: does not "
	    "apply; merged three ways without conflict, to be looked "
	    "over\n") != NULL);
	(void) snprintf(path, sizeof (path), "%s/generic/fixed/1/2-x4.patch",
	    runbase);
	CHECK(strstr(slurp(path), "-L4\n+X4\n") != NULL);
	CHECK(strstr(slurp(path), "diff --git a/m.c b/m.c\n") != NULL);
	CHECK(resume(SH_BV_CONTINUE, NULL) == 0);
	CHECK(strstr(ran, "built " F "@" VERSION "\n") != NULL);
	CHECK(is(slurp(scratch_path("seen-m.c")),
	    "L1\nl2\nl3\nX4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\n"));
	/* P14: the record, and the fixes kept in the obj cache */
	CHECK(is(prop(F "@" VERSION, "shtetl:patches", NULL),
	    "patches/pause: 2-x4.patch fixed, 3-l1.patch fixed"));
	(void) snprintf(text, sizeof (text), "obj/patches/%s/1/3-l1.patch",
	    VERSION);
	CHECK(inrun(text));
	/* P15: the end */
	CHECK(!inrun(SH_BUILD_PAUSED));
	CHECK(is(prop("tank/zbuild", "shtetl:build", NULL), "-"));
	CHECK(!zfake_snapshot("tank/zbuild@shtetl-patch-0"));
	(void) snprintf(path, sizeof (path), "%s/m.c", tree);
	CHECK(is(slurp(path), base_m));

	/* P10, P13: --skip; the recipe the build began with */
	patchworld();
	patchconf("qb", "\"patches/pause\" {}");
	pausepatches(0);
	CHECK(build("the message") == 1);
	(void) snprintf(path, sizeof (path), "%s/shtetl.conf", root);
	sh("echo 'commands { late = \"echo late\"; }' >> %s", path);
	CHECK(resume(SH_BV_SKIP, NULL) == 0);
	CHECK(strstr(ran, "patch 2-x4.patch from patches/pause: left "
	    "out\n") != NULL);
	CHECK(strstr(ran, "commands late") == NULL);
	CHECK(is(slurp(scratch_path("seen-m.c")),
	    "l1\nl2\nl3\nL4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\n"));
	CHECK(is(prop(F "@" VERSION, "shtetl:patches", NULL),
	    "patches/pause: 2-x4.patch skipped"));
	CHECK(is(prop(F "@" VERSION, "shtetl:commit", NULL), "the message"));
	CHECK(!inrun(SH_BUILD_PAUSED));

	/* P11: --abort */
	patchworld();
	patchconf("qc", "\"patches/pause\" {}");
	pausepatches(0);
	CHECK(build(NULL) == 1);
	/* taking it up fails, the config snapshot gone: still paused */
	CHECK(sh_zfs_destroy(z, "tank/zconfig@build-generic-20261008-120000",
	    &wc.c_err) == 0);
	CHECK(resume(SH_BV_CONTINUE, NULL) == -1);
	CHECK(cap_has(&c, "build of generic still paused"));
	CHECK(inrun(SH_BUILD_PAUSED) && zfake_exists(F));
	CHECK(zfake_snapshot("tank/zbuild@shtetl-patch-2"));
	CHECK(resume(SH_BV_ABORT, NULL) == 0);
	CHECK(strstr(ran, "build of generic given up, nothing built\n") !=
	    NULL);
	CHECK(!zfake_exists(F));
	CHECK(is(prop("tank/zbuild", "shtetl:build", NULL), "-"));
	CHECK(!inrun(SH_BUILD_PAUSED) && !inrun("patches"));
	CHECK(inrun("build.log"));
	CHECK(!zfake_snapshot("tank/zbuild@shtetl-patch-0"));
	(void) snprintf(path, sizeof (path), "%s/m.c", tree);
	CHECK(is(slurp(path), base_m));
	verb = SH_BV_ABORT;
	CHECK(plan("generic") == -1);
	verb = SH_BV_NEW;

	/* P8, P9: no base to merge against: --reject, a .rej left */
	patchworld();
	patchconf("qd", "\"patches/bad\" {}");
	CHECK(build(NULL) == 1);
	CHECK(strstr(ran, "patch 1-x.patch from patches/bad: does not "
	    "apply; applied with --reject, .rej files left\n") != NULL);
	CHECK(resume(SH_BV_CONTINUE, NULL) == -1);
	CHECK(cap_has(&c, "a.c.rej left"));
	CHECK(resume(SH_BV_ABORT, NULL) == 0);
	(void) snprintf(path, sizeof (path), "%s/a.c.rej", tree);
	CHECK(access(path, F_OK) != 0);

	/* F26: an update paused: --abort rolls F back, --skip updates */
	patchworld();
	config("qe", generic);
	CHECK(build(NULL) == 0);
	patchconf("qf", "\"patches/pause\" {}");
	pausepatches(0);
	CHECK(build(NULL) == 1);
	CHECK(resume(SH_BV_ABORT, NULL) == 0);
	CHECK(strstr(ran, "rolled back " F " to @" VERSION "\n") != NULL);
	CHECK(strstr(ran, "destroyed") == NULL && zfake_exists(F));
	CHECK(zfake_snapshot(F "@" VERSION));
	CHECK(is(prop("tank/zbuild", "shtetl:build", NULL), "-"));
	CHECK(build(NULL) == 1);
	CHECK(resume(SH_BV_SKIP, NULL) == 0);
	CHECK(strstr(ran, "make delete-old delete-old-libs") != NULL);
	CHECK(strstr(ran, "built " F "@" VERSION ".2\n") != NULL);
	zfake_hook(NULL, NULL);
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
	test_update();
	test_pkgup();
	test_patches();
	test_pause();
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
