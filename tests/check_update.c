/*
 * shtetl update X (planning.md 7.6, 7.6.2, 7.7): tests/MATRIX.md
 * families U and G, over the fake ZFS and the fake rebaser
 * (tests/rbfake.c), with jail and jls played by scripts that keep which
 * jails run.
 */

#include <sys/stat.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "check.h"
#include "core/beam.h"
#include "core/build.h"
#include "core/cfgsnap.h"
#include "core/cutover.h"
#include "core/parse.h"
#include "core/rebase.h"
#include "core/spaces.h"
#include "core/update.h"
#include "core/zfs.h"
#include "core/zwalk.h"
#include "rbfake.h"
#include "zfake.h"

#define	LF	"tank/shtetl/foundations/generic"
#define	RF	"fast/run/foundations/generic"
#define	RX	"fast/run/containers/x"
#define	RXS	"fast/run/containers/x-staging"
#define	LX	"tank/shtetl/containers/x"
#define	LXS	"tank/shtetl/containers/x-staging"
#define	V0	"15.1-releng-2026-09-01-0f691888dc56"
#define	V1	"15.1-releng-2026-10-01-0f691888dc56"
#define	V2	"15.1-releng-2026-10-08-0f691888dc56"
#define	BCFG	"tank/zconfig@build-generic-20261001-120000"
#define	S1	"save-2026-10-02-100000"
#define	S2	"save-2026-10-03-100000"
#define	S3	"save-2026-10-08-120000"
#define	S4	"save-2026-10-08-120000-2"
/* 2026-10-08 12:00:00 UTC */
#define	NOON	((time_t)1791460800)

static struct cap c, wc;
static struct sh_zfs *z;
static struct sh_spaces s;
static struct sh_uplan p;
static struct sh_kplan kp;
static struct sh_tools fake;
static struct sh_node *site;
static char root[1200], jaildir[1200], rundir[1200], mnt[1200];
static char toollog[1200], running[1200];
static char *ran;

static int
is(const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("got \"%s\", wanted \"%s\"\n", got, want);
		return (0);
	}
	return (1);
}

static const char *
prop(const char *name, const char *pr)
{
	static char val[4][512];
	static int n;

	n = (n + 1) % 4;
	cap_reset(&wc);
	if (sh_zfs_get(z, name, pr, val[n], sizeof (val[n]), NULL,
	    &wc.c_err) != 0)
		return ("(refused)");
	return (val[n]);
}

static int
there(const char *name)
{
	return (strcmp(prop(name, "type"), "(refused)") != 0);
}

static const char *
slurp(const char *path)
{
	static char buf[16384];
	size_t n;
	FILE *f;

	if ((f = fopen(path, "r")) == NULL)
		return ("(none)");
	n = fread(buf, 1, sizeof (buf) - 1, f);
	buf[n] = '\0';
	(void) fclose(f);
	return (buf);
}

static int
asked(const char *text)
{
	return (strstr(slurp(toollog), text) != NULL);
}

/* Whether the jail NAME runs, as the fakes keep it. */
static int
runs(const char *name)
{
	char path[1300];

	(void) snprintf(path, sizeof (path), "%s/%s", running, name);
	return (access(path, F_OK) == 0);
}

static void
spath(char *buf, size_t len, const char *name)
{
	(void) snprintf(buf, len, "%s", scratch_path(name));
}

static const char *
tool(const char *name, const char *body)
{
	char text[4096], rel[64];
	static char path[4][1200];
	static int n;

	(void) snprintf(text, sizeof (text), "#!/bin/sh\n"
	    "echo \"%s $*\" >> %s\n%sexit 0\n", name, toollog, body);
	(void) snprintf(rel, sizeof (rel), "tools/%s", name);
	(void) scratch_write(rel, text);
	n = (n + 1) % 4;
	spath(path[n], sizeof (path[n]), rel);
	CHECK(chmod(path[n], 0755) == 0);
	return (path[n]);
}

/* The configuration root, and the fakes. */
static void
config(void)
{
	static const char *const files[] = {
		"conf/shtetl.conf",
		"ver = \">=15.1\";\ndatasets { var {} \"usr/local\" {} }\n",
		"conf/containers/x.conf",
		"datasets { var { compression = \"zstd\"; } www {} }\n"
		"jailconf = \"allow.raw_sockets;\";\n"
		"derivations {\n"
		"  \"etc/pwd.db\" { source = \"etc/master.passwd\"; "
		"command = \"pwd_mkdb -p -d /etc /etc/master.passwd\"; }\n"
		"  \"var/db/x.db\" { source = \"var/db/x\"; "
		"command = \"mkx\"; }\n"
		"  \"etc/bad.db\" { source = \"etc/bad\"; "
		"command = \"fail me\"; }\n"
		"  \"etc/spool.db\" { source = \"var/spool\"; "
		"command = \"cross\"; }\n"
		"}\n",
		"conf/containers/new.conf", "ver = \">=16.0\";\n",
		NULL
	};
	char body[2048];
	size_t i;

	for (i = 0; files[i] != NULL; i += 2)
		(void) scratch_write(files[i], files[i + 1]);
	spath(root, sizeof (root), "conf");
	spath(jaildir, sizeof (jaildir), "jail.conf.d");
	spath(rundir, sizeof (rundir), "update");
	spath(mnt, sizeof (mnt), "mnt");
	spath(toollog, sizeof (toollog), "tools.log");
	spath(running, sizeof (running), "running");
	(void) scratch_write("running/.keep", "");
	(void) snprintf(body, sizeof (body), "case \"$3\" in\n"
	    "-c) [ -f %s ] && exit 4; touch %s/$4;;\n-r) rm -f %s/$4;;\n"
	    "esac\n", scratch_path("jailfails"), running, running);
	fake.tl_jail = tool("jail", body);
	(void) snprintf(body, sizeof (body), "[ -f %s/$2 ] || exit 1\n",
	    running);
	fake.tl_jls = tool("jls", body);
	(void) snprintf(body, sizeof (body),
	    "case \"$5\" in *fail*) [ -f %s ] && exit 4;; esac\n",
	    scratch_path("failing"));
	fake.tl_chroot = tool("chroot", body);
	fake.tl_sh = "/bin/sh";
}

/* LF and its datasets snapshotted as V, its revision 15.1. */
static void
version(const char *v)
{
	static const char *const ds[] = { LF, LF "/usr", LF "/usr/local",
	    LF "/var" };
	char names[4][512];
	const char *np[4];
	struct sh_zprop zp[3];
	size_t i;

	for (i = 0; i < 4; i++) {
		(void) snprintf(names[i], sizeof (names[i]), "%s@%s", ds[i], v);
		np[i] = names[i];
	}
	zp[0].zp_name = "shtetl:kind";
	zp[0].zp_value = "version";
	zp[1].zp_name = "shtetl:config";
	zp[1].zp_value = BCFG;
	zp[2].zp_name = "shtetl:revision";
	zp[2].zp_value = "15.1";
	cap_reset(&wc);
	CHECK(sh_zfs_snapshot(z, np, 4, zp, 3, &wc.c_err) == 0);
}

/* X's datasets snapshotted as the save S, its message MSG. */
static void
xsave(const char *snap, const char *msg)
{
	static const char *const ds[] = { RX, RX "/usr", RX "/usr/local",
	    RX "/var", RX "/www" };
	char names[5][512];
	const char *np[5];
	struct sh_zprop zp[3];
	size_t i;

	for (i = 0; i < 5; i++) {
		(void) snprintf(names[i], sizeof (names[i]), "%s@%s", ds[i],
		    snap);
		np[i] = names[i];
	}
	zp[0].zp_name = "shtetl:kind";
	zp[0].zp_value = "save";
	zp[1].zp_name = "shtetl:commit";
	zp[1].zp_value = msg;
	zp[2].zp_name = "shtetl:config";
	zp[2].zp_value = BCFG;
	cap_reset(&wc);
	CHECK(sh_zfs_snapshot(z, np, 5, zp, 3, &wc.c_err) == 0);
}

/*
 * The world: generic in zlake at V0, V1, V2; V1 on zrun; x cloned from
 * V1 as create clones (usr a placeholder, www its own), mounted, its
 * jail running, saved twice.
 */
static void
world(int run)
{
	static const struct sh_zprop cfg = { "shtetl:kind", "config" };
	static const struct sh_zprop off = { "canmount", "off" };
	static const char *const bcfg[] = { BCFG };
	static const char *const xs[] = { RX, RX "/usr/local", RX "/var",
	    RX "/www" };
	struct sh_beam b;
	size_t i;
	FILE *f;

	zfake_reset();
	rbfake_reset();
	zfake_pool("tank");
	zfake_fs("tank/shtetl");
	zfake_set("tank/shtetl", "shtetl:space", "lake");
	zfake_set("tank/shtetl", "mountpoint", "none");
	zfake_fs("tank/shtetl/foundations");
	zfake_fs("tank/shtetl/containers");
	zfake_fs("tank/shtetl/archive");
	zfake_fs("tank/shtetl/archive/containers");
	zfake_fs("tank/zconfig");
	zfake_set("tank/zconfig", "shtetl:space", "config");
	zfake_fs(LF);
	zfake_fs(LF "/usr");
	zfake_set(LF "/usr", "canmount", "off");
	zfake_fs(LF "/usr/local");
	zfake_fs(LF "/var");
	cap_reset(&wc);
	CHECK(sh_zfs_snapshot(z, bcfg, 1, &cfg, 1, &wc.c_err) == 0);
	version(V0);
	version(V1);
	zfake_pool("fast");
	if (run) {
		zfake_fs("fast/run");
		zfake_set("fast/run", "shtetl:space", "run");
		zfake_set("fast/run", "mountpoint", "none");
		zfake_fs("fast/run/foundations");
		zfake_set("fast/run/foundations", "mountpoint",
		    "/shtetl/foundations");
		zfake_set("fast/run/foundations", "canmount", "off");
		zfake_fs("fast/run/containers");
		zfake_set("fast/run/containers", "mountpoint", mnt);
	}
	sh_spaces_free(&s);
	cap_reset(&c);
	CHECK(sh_spaces_discover(z, &s, &c.c_err) == 0);
	(void) unlink(toollog);
	(void) unlink(scratch_path("jail.conf.d/x-staging.conf"));
	(void) unlink(scratch_path("running/x"));
	(void) unlink(scratch_path("running/x-staging"));
	if (!run)
		return;
	CHECK(sh_beam_plan(z, &s, "generic", V1, &b, &c.c_err) == 0);
	CHECK((f = fopen("/dev/null", "w")) != NULL);
	CHECK(sh_beam_run(z, &b, f, &c.c_err) == 0);
	(void) fclose(f);
	version(V2);
	CHECK(sh_zfs_clone(z, RF "@" V1, RX, NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_clone(z, RF "/usr@" V1, RX "/usr", &off, 1, &c.c_err) ==
	    0);
	CHECK(sh_zfs_clone(z, RF "/usr/local@" V1, RX "/usr/local", NULL, 0,
	    &c.c_err) == 0);
	CHECK(sh_zfs_clone(z, RF "/var@" V1, RX "/var", NULL, 0, &c.c_err) ==
	    0);
	CHECK(sh_zfs_create(z, RX "/www", NULL, 0, &c.c_err) == 0);
	for (i = 0; i < 4; i++)
		CHECK(sh_zfs_mount(z, xs[i], &c.c_err) == 0);
	xsave(S1, "first");
	xsave(S2, "second");
	(void) scratch_write("jail.conf.d/x.conf", "x {}\n");
	(void) scratch_write("running/x", "");
}

/* The site's settings TEXT, or none. */
static void
settings(const char *text)
{
	sh_node_free(site);
	site = NULL;
	if (text != NULL) {
		cap_reset(&wc);
		site = sh_parse_buf(text, strlen(text), "shtetl.conf",
		    &wc.c_err);
		CHECK(site != NULL);
	}
}

static int
plan(const char *target, const char *version, enum sh_uverb verb)
{
	sh_update_free(&p);
	cap_reset(&c);
	cap_reset(&wc);
	return (sh_update_plan(z, &s, site, root, target, version, verb,
	    jaildir, rundir, &p, &c.c_err, &wc.c_err));
}

static void
refused(const char *target, const char *version, const char *want)
{
	int rc;

	rc = plan(target, version, SH_UV_NEW);
	if (rc == 0 || !cap_has(&c, want))
		printf("%s: wanted \"%s\", got:\n%s", target, want,
		    cap_text(&c));
	CHECK(rc == -1 && cap_has(&c, want));
}

static int
run(int tty)
{
	size_t len;
	FILE *f;
	int rc;

	free(ran);
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	cap_reset(&c);
	rc = sh_update_run(z, &p, &s, &fake, &rbfake, tty, NOON, f, &c.c_err);
	CHECK(fclose(f) == 0);
	return (rc);
}

/* VERB on x, planned and run. */
static int
update(enum sh_uverb verb, int tty)
{
	if (plan("x", NULL, verb) != 0) {
		printf("plan refused:\n%s", cap_text(&c));
		CHECK(0);
	}
	return (run(tty));
}

/* U1: what is refused. */
static void
test_refused(void)
{
	settings(NULL);
	world(0);
	refused("x", NULL, "no run root");
	world(1);
	refused("X.y", NULL, "X.y: invalid container name");
	refused("nope", NULL, "no container nope");
	refused("x", "v9", "foundation generic has no version v9");
	refused("x", V1, "container x is on generic@" V1 " already");
	refused("x", V0, "generic@" V0 " is older than " V1 ", x's");
	zfake_set(RX "/var", "zfs_rebase:phase", "conflicts");
	refused("x", NULL, RX "/var: an open rebase");
	/* an update under way, as ZFS says it: a staging tree not done */
	world(1);
	zfake_fs(RXS);
	refused("x", NULL, "an update of x is under way: shtetl update "
	    "--continue x, or --abort x");
	world(1);
	CHECK(plan("x", NULL, SH_UV_CONTINUE) == -1);
	CHECK(cap_has(&c, "no update of x to take up"));
	CHECK(plan("x", NULL, SH_UV_ABORT) == -1);
	CHECK(cap_has(&c, "no update of x to give up"));
	/* a container no clone of a foundation's version */
	world(1);
	CHECK(sh_zfs_create(z, "fast/run/containers/new", NULL, 0,
	    &c.c_err) == 0);
	refused("new", NULL, "container new is no clone of a foundation's "
	    "version");
}

/* U2: the plan, and its showing. */
static void
test_plan(void)
{
	size_t len;
	FILE *f;

	settings(NULL);
	world(1);
	CHECK(plan("containers/x", NULL, SH_UV_NEW) == 0);
	CHECK(is(p.up_name, "x") && is(p.up_stagename, "x-staging"));
	CHECK(is(p.up_found, "generic") && is(p.up_old, V1));
	CHECK(is(p.up_version, V2) && p.up_beam == 1);
	CHECK(is(p.up_stage, RXS) && is(p.up_runf, RF));
	CHECK(p.up_nds == 5);
	CHECK(is(p.up_dss[0].ud_rel, "") &&
	    p.up_dss[0].ud_how == SH_UH_REPLAY);
	CHECK(is(p.up_dss[1].ud_rel, "usr") &&
	    p.up_dss[1].ud_how == SH_UH_TARGET && p.up_dss[1].ud_placeholder);
	CHECK(is(p.up_dss[2].ud_rel, "usr/local") &&
	    p.up_dss[2].ud_how == SH_UH_REPLAY);
	CHECK(is(p.up_dss[3].ud_rel, "var") &&
	    p.up_dss[3].ud_how == SH_UH_REPLAY && p.up_dss[3].ud_props != NULL);
	CHECK(is(p.up_dss[4].ud_rel, "www") &&
	    p.up_dss[4].ud_how == SH_UH_OWN);
	CHECK(p.up_nsaves == 2 && is(p.up_saves[0], S1) &&
	    is(p.up_saves[1], S2));
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	sh_update_show(f, &p);
	CHECK(fclose(f) == 0);
	CHECK(strstr(ran, "container x, from generic@" V1 " to @" V2 ", "
	    "beamed to zrun first\n") != NULL);
	CHECK(strstr(ran, "  + " RXS "/usr  cloned from the target, "
	    "canmount=off\n") != NULL);
	CHECK(strstr(ran, "  + " RXS "/www  cloned from the from-save\n") !=
	    NULL);
	CHECK(strstr(ran, "  saves replayed: " S1 " " S2 ", then the "
	    "from-save\n") != NULL);
	free(ran);
	ran = NULL;
	/* U5: update.depth */
	settings("update { depth = 2; }\n");
	CHECK(plan("x", NULL, SH_UV_NEW) == 0 && p.up_nsaves == 1 &&
	    is(p.up_saves[0], S2));
	settings("update { depth = 1; suffix = \"next\"; }\n");
	CHECK(plan("x", NULL, SH_UV_NEW) == 0 && p.up_nsaves == 0);
	CHECK(is(p.up_stagename, "x-next"));
	settings(NULL);
	/* one given, on zrun already */
	world(1);
	CHECK(plan("x", V2, SH_UV_NEW) == 0 && p.up_beam == 1);
}

/* U3, U4: a whole run, no conflict. */
static void
test_run(void)
{
	char path[1300];
	const char *log;

	settings(NULL);
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	if (c.c_err.se_count != 0)
		printf("%s", cap_text(&c));
	/* X stopped; its from-save */
	CHECK(asked("jail -f ") && !runs("x"));
	CHECK(is(prop(RX "@" S3, "shtetl:kind"), "save"));
	CHECK(is(prop(RX "@" S3, "shtetl:commit"), "update to generic@" V2));
	/* the replays, three saves each */
	log = rbfake_log();
	CHECK(strstr(log, "start --from " RX "@" S1 " --onto " RF "@" V2
	    " --result " RXS " -m\n") != NULL);
	CHECK(strstr(log, "start --from " RX "@" S2 " --onto " RXS
	    " --result " S1 " --base -m\n  base " RX "@" S1 "\n") != NULL);
	CHECK(strstr(log, "start --from " RX "@" S3 " --onto " RXS
	    " --result " S2 " --base -m\n  base " RX "@" S2 "\n") != NULL);
	CHECK(strstr(log, "start --from " RX "/var@" S1 " --onto " RF
	    "/var@" V2 " --result " RXS "/var -m\n") != NULL);
	CHECK(strstr(log, "--result " RXS "/usr -m\n") == NULL);
	CHECK(strstr(log, "--result " RXS "/www -m\n") == NULL);
	/* each replayed save as X's, its name and shtetl:* */
	CHECK(is(prop(RXS "@" S1, "shtetl:kind"), "save"));
	CHECK(is(prop(RXS "@" S1, "shtetl:commit"), "first"));
	CHECK(is(prop(RXS "@" S2, "shtetl:commit"), "second"));
	CHECK(is(prop(RXS "@" S3, "shtetl:commit"), "update to generic@" V2));
	CHECK(is(prop(RXS "/var@" S1, "shtetl:commit"), "first"));
	CHECK(is(prop(RXS "/var@" S1, "shtetl:replayed-from"), "-"));
	/* placed, its properties, its placeholder, its own */
	CHECK(is(prop(RXS, "origin"), RF "@" V2));
	CHECK(is(prop(RXS, "readonly"), "off"));
	CHECK(is(prop(RXS, "mounted"), "yes"));
	(void) snprintf(path, sizeof (path), "%s/x-staging", mnt);
	CHECK(is(prop(RXS, "mountpoint"), path));
	CHECK(is(prop(RXS "/var", "compression"), "zstd"));
	CHECK(is(prop(RXS "/usr", "origin"), RF "/usr@" V2));
	CHECK(is(prop(RXS "/usr", "canmount"), "off"));
	CHECK(is(prop(RXS "/usr/local", "origin"), RF "/usr/local@" V2));
	CHECK(is(prop(RXS "/www", "origin"), RX "/www@" S3));
	CHECK(is(prop(RXS "/www", "mounted"), "yes"));
	/* F away again; X-staging the version, started, saved */
	CHECK(is(prop(RF, "mounted"), "no"));
	CHECK(is(prop(RXS "@" V2, "shtetl:kind"), "version"));
	/* N8: the config root unchanged since the build: the build's */
	CHECK(is(prop(RXS "/www@" V2, "shtetl:config"), BCFG));
	CHECK(strstr(ran, "snapshot tank/zconfig@") == NULL);
	(void) snprintf(path, sizeof (path), "%s/x-staging.conf", jaildir);
	CHECK(strstr(slurp(path), "x-staging {\n\tpath = \"") != NULL);
	CHECK(strstr(slurp(path), "\thost.hostname = \"x\";\n") != NULL);
	CHECK(strstr(slurp(path), "\tallow.raw_sockets;\n") != NULL);
	CHECK(runs("x-staging") && !runs("x"));
	CHECK(is(prop(RXS "@" S4, "shtetl:kind"), "save"));
	CHECK(is(prop(LXS, "origin"), LF "@" V2));
	CHECK(is(prop(LXS "/www", "origin"), LX "/www@" S3));
	/* done, as ZFS says it; no record of shtetl's, the tool's gone */
	{
		char v[256], from[256];

		CHECK(sh_update_state(z, RX, RXS, v, sizeof (v), from,
		    sizeof (from), &c.c_err) == SH_US_DONE);
		CHECK(is(v, V2) && is(from, S3));
		CHECK(is(prop(RX "@" S3, "shtetl:update"), V2));
	}
	CHECK(access(scratch_path("update/x/rebase/d0.1.manifest"), F_OK) !=
	    0);
	CHECK(strstr(ran, "staged x-staging on generic@" V2 ": shtetl "
	    "cutover x, or abandon x\n") != NULL);
	/* the update done: continuing it refused */
	CHECK(plan("x", NULL, SH_UV_CONTINUE) == -1);
	CHECK(cap_has(&c, "the update of x is done"));
	/* U5: depth 1, one rebase a dataset */
	settings("update { depth = 1; }\n");
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	CHECK(strstr(rbfake_log(), "--base") == NULL);
	CHECK(strstr(rbfake_log(), "start --from " RX "@" S3 " --onto " RF
	    "@" V2 " --result " RXS " -m\n") != NULL);
	CHECK(is(prop(RXS "@" S3, "shtetl:commit"), "update to generic@" V2));
	CHECK(!there(RXS "@" S1));
	settings(NULL);
	/* N8: a recipe written since: a config snapshot of its own */
	world(1);
	zfake_write("tank/zconfig", 512);
	CHECK(update(SH_UV_NEW, 0) == 0);
	(void) strcpy(path, "snapshot tank/zconfig@");
	sh_cfgsnap_name(NOON, 0, path + strlen(path),
	    sizeof (path) - strlen(path));
	CHECK(strstr(ran, path) != NULL);
	CHECK(is(prop(RXS "@" V2, "shtetl:config"), path + 9));
	CHECK(is(prop(path + 9, "shtetl:kind"), "config"));
	CHECK(is(prop(path + 9, "shtetl:commit"), "update x to generic@" V2));
}

/* Every line still "-" of the resolution at PATH answered keep. */
static int
answer(const char *path, void *arg)
{
	struct sh_resolution r;
	const char *all[16];
	size_t i, n;

	(void) arg;
	(void) memset(&r, 0, sizeof (r));
	if (sh_res_read(path, &r, &wc.c_err) != 0) {
		sh_res_free(&r);
		return (-1);
	}
	for (i = n = 0; i < r.rs_n && n < 16; i++)
		all[n++] = r.rs_lines[i].rl_path;
	(void) sh_res_keep(&r, all, n);
	(void) sh_res_write(path, &r, &wc.c_err);
	sh_res_free(&r);
	return (0);
}

/* U6: a gate, paused and taken up; the picker; --abort. */
/*
 * G6: a conflict zfs_rebase merges three ways with no conflict goes on
 * by itself (ruled 2026-10-15): -m at each start and --continue. A
 * step whose every conflict merges does not stop; one with a conflict
 * that does not merge pauses on that one alone, the merged one keep.
 */
static void
test_gate_merge(void)
{
	static const char *const one[] = { "/etc/rc.conf", NULL };
	static const char *const two[] = { "/etc/motd", "/etc/rc.conf",
	    NULL };
	struct sh_resolution r;
	const char *res;

	settings(NULL);
	world(1);
	rbfake_conflicts(RXS "/var", 2, one);
	rbfake_merges(one);
	CHECK(update(SH_UV_NEW, 0) == 0);
	CHECK(strstr(ran, "paused") == NULL);
	CHECK(strstr(rbfake_log(), "--result " S1 " --base -m\n") != NULL);
	CHECK(runs("x-staging"));
	world(1);
	rbfake_conflicts(RXS "/var", 2, two);
	rbfake_merges(one);
	CHECK(update(SH_UV_NEW, 0) == 1);
	CHECK(strstr(ran, "1 name unanswered") != NULL);
	CHECK((res = rbfake_resolution(RXS "/var")) != NULL);
	(void) memset(&r, 0, sizeof (r));
	CHECK(sh_res_read(res, &r, &c.c_err) == 0);
	CHECK(is(sh_res_find(&r, "/etc/rc.conf")->rl_choice, "keep"));
	CHECK(is(sh_res_find(&r, "/etc/motd")->rl_choice, "-"));
	sh_res_free(&r);
	/* the rest by hand: on, the -m --continue passing it */
	CHECK(answer(res, NULL) == 0);
	CHECK(update(SH_UV_CONTINUE, 0) == 0);
	CHECK(strstr(rbfake_log(), "continue " RXS "/var -m\n") != NULL);
}

static void
test_gate(void)
{
	static const char *const gate[] = { "/etc/rc.conf", NULL };
	const char *res;

	settings(NULL);
	world(1);
	rbfake_conflicts(RXS "/var", 2, gate);
	CHECK(update(SH_UV_NEW, 0) == 1);
	CHECK(strstr(ran, "paused: " RXS "/var stopped at conflicts, step 2 "
	    "of 3, 1 name unanswered: answer ") != NULL);
	/* paused: the tool's documents, and ZFS, say so */
	{
		char v[256], from[256];

		CHECK(sh_update_state(z, RX, RXS, v, sizeof (v), from,
		    sizeof (from), &c.c_err) == SH_US_UNDERWAY);
	}
	CHECK(access(scratch_path("update/x/rebase/d3.2.manifest.resolution"),
	    F_OK) == 0);
	refused("x", NULL, "an update of x is under way");
	CHECK(!runs("x") && !runs("x-staging"));
	CHECK(is(prop(RF, "mounted"), "no"));
	CHECK(is(prop(RXS "/var", "zfs_rebase:phase"), "conflicts"));
	/* taken up unanswered: paused again */
	CHECK(update(SH_UV_CONTINUE, 0) == 1);
	CHECK(strstr(ran, "paused: ") != NULL);
	/* answered by hand: on to the end */
	CHECK((res = rbfake_resolution(RXS "/var")) != NULL);
	CHECK(answer(res, NULL) == 0);
	CHECK(update(SH_UV_CONTINUE, 0) == 0);
	if (c.c_err.se_count != 0)
		printf("%s", cap_text(&c));
	CHECK(is(prop(RXS "/var", "zfs_rebase:phase"), "-"));
	CHECK(there(RXS "/var@" S3) && there(RXS "/www@" V2));
	CHECK(runs("x-staging"));
	/* the picker, on a terminal: answered there, no pause */
	world(1);
	rbfake_conflicts(RXS, 1, gate);
	rbfake_picker(answer, NULL);
	CHECK(update(SH_UV_NEW, 1) == 0);
	CHECK(strstr(rbfake_log(), "continue " RXS " -m --no-merge -i\n") !=
	    NULL);
	CHECK(strstr(rbfake_log(), "continue " RXS " -m\n") != NULL);
	/* --abort: the rebase aborted, the tree gone, x running again */
	world(1);
	rbfake_conflicts(RXS "/usr/local", 3, gate);
	CHECK(update(SH_UV_NEW, 0) == 1);
	CHECK(update(SH_UV_ABORT, 0) == 0);
	CHECK(strstr(rbfake_log(), "abort " RXS "/usr/local\n") != NULL);
	CHECK(!zfake_exists(RXS));
	CHECK(runs("x") && !runs("x-staging"));
	CHECK(access(scratch_path("update/x/rebase/d2.3.manifest"), F_OK) !=
	    0);
	CHECK(there(RX "@" S3));
	CHECK(is(prop(RX "@" S3, "shtetl:update"), "-"));
	CHECK(strstr(ran, "gave up the update of x\n") != NULL);
}

/* The line of NAME in the resolution at PATH given CHOICE. */
static void
choose(const char *path, const char *name, const char *choice)
{
	struct sh_resolution r;
	const struct sh_resline *l;
	char *line, *sp, *nl, *un;
	size_t at;

	(void) memset(&r, 0, sizeof (r));
	CHECK(sh_res_read(path, &r, &wc.c_err) == 0);
	CHECK((l = sh_res_find(&r, name)) != NULL);
	at = (size_t)(l - r.rs_lines);
	line = r.rs_text[l->rl_line];
	CHECK((sp = strrchr(line, ' ')) != NULL);
	CHECK((nl = malloc((size_t)(sp - line) + strlen(choice) + 2)) != NULL);
	(void) sprintf(nl, "%.*s %s", (int)(sp - line), line, choice);
	free(r.rs_text[l->rl_line]);
	r.rs_text[l->rl_line] = nl;
	(void) snprintf(r.rs_lines[at].rl_choice,
	    sizeof (r.rs_lines[at].rl_choice), "%s", choice);
	CHECK((un = malloc(32)) != NULL);
	(void) snprintf(un, 32, "#unanswered %u", sh_res_unanswered(&r));
	free(r.rs_text[r.rs_unline]);
	r.rs_text[r.rs_unline] = un;
	CHECK(sh_res_write(path, &r, &wc.c_err) == 0);
	sh_res_free(&r);
}

/* G1 - G5: conflicts and derivations, the text first. */
static void
test_derivations(void)
{
	static const char *const pw[] = { "/etc/master.passwd",
	    "/etc/pwd.db", NULL };
	static const char *const vx[] = { "/db/x", "/db/x.db", NULL };
	static const char *const bad[] = { "/etc/bad", "/etc/bad.db", NULL };
	struct sh_resolution r;
	const char *res;
	char priv[1100], want[1400];

	settings(NULL);
	/* G1, G3: pwd.db kept, made again in the private mount */
	world(1);
	rbfake_conflicts(RXS, 1, pw);
	CHECK(update(SH_UV_NEW, 0) == 1);
	CHECK((res = rbfake_resolution(RXS)) != NULL);
	(void) memset(&r, 0, sizeof (r));
	CHECK(sh_res_read(res, &r, &wc.c_err) == 0);
	CHECK(is(sh_res_find(&r, "/etc/pwd.db")->rl_choice, "keep"));
	CHECK(is(sh_res_find(&r, "/etc/master.passwd")->rl_choice, "-"));
	CHECK(sh_res_unanswered(&r) == 1);
	sh_res_free(&r);
	CHECK(!asked("chroot "));
	CHECK(strstr(ran, "warning: the derivation of etc/spool.db has its "
	    "source in another dataset: not run\n") != NULL);
	choose(res, "/etc/master.passwd", "keep");
	CHECK(update(SH_UV_CONTINUE, 0) == 0);
	sh_rb_privmnt(RXS, priv, sizeof (priv));
	(void) snprintf(want, sizeof (want), "chroot %s /bin/sh -e -c "
	    "pwd_mkdb -p -d /etc /etc/master.passwd\n", priv);
	CHECK(asked(want));
	CHECK(!asked("-c mkx") && !asked("-c fail"));
	/* G4: its source answered from, made after done where placed */
	world(1);
	rbfake_conflicts(RXS, 1, pw);
	CHECK(update(SH_UV_NEW, 0) == 1);
	choose(rbfake_resolution(RXS), "/etc/master.passwd", "from");
	CHECK(update(SH_UV_CONTINUE, 0) == 0);
	CHECK(!asked(want));
	(void) snprintf(want, sizeof (want), "chroot %s/x-staging /bin/sh "
	    "-e -c pwd_mkdb -p -d /etc /etc/master.passwd\n", mnt);
	CHECK(asked(want));
	CHECK(access(scratch_path("update/x/rebase/d0.1.derive"), F_OK) != 0);
	/* in var's tree: /db/x.db */
	world(1);
	rbfake_conflicts(RXS "/var", 1, vx);
	CHECK(update(SH_UV_NEW, 0) == 1);
	(void) memset(&r, 0, sizeof (r));
	CHECK(sh_res_read(rbfake_resolution(RXS "/var"), &r, &wc.c_err) == 0);
	CHECK(is(sh_res_find(&r, "/db/x.db")->rl_choice, "keep"));
	sh_res_free(&r);
	choose(rbfake_resolution(RXS "/var"), "/db/x", "onto");
	CHECK(update(SH_UV_CONTINUE, 0) == 0);
	sh_rb_privmnt(RXS "/var", priv, sizeof (priv));
	(void) snprintf(want, sizeof (want), "chroot %s /bin/sh -e -c mkx\n",
	    priv);
	CHECK(asked(want));
	/* G5: one failing pauses; taken up, it runs again */
	world(1);
	(void) scratch_write("failing", "");
	rbfake_conflicts(RXS, 1, bad);
	CHECK(update(SH_UV_NEW, 0) == 1);
	choose(rbfake_resolution(RXS), "/etc/bad", "keep");
	CHECK(update(SH_UV_CONTINUE, 0) == 1);
	CHECK(strstr(ran, "paused: the derivation of /etc/bad.db failed") !=
	    NULL);
	CHECK(is(prop(RXS, "zfs_rebase:phase"), "conflicts"));
	(void) unlink(scratch_path("failing"));
	CHECK(update(SH_UV_CONTINUE, 0) == 0);
	CHECK(is(prop(RXS, "zfs_rebase:phase"), "-"));
}

#define	ARCH	"tank/shtetl/archive/containers/x-" V1
#define	S5	"save-2026-10-08-120000-3"
#define	S6	"save-2026-10-08-120000-4"

/* Cutting x over (ABANDON 0) or abandoning it: the plan's return. */
static int
kplan(const char *target, int abandon)
{
	sh_cutover_free(&kp);
	cap_reset(&c);
	cap_reset(&wc);
	return (sh_cutover_plan(z, &s, site, root, target, abandon, jaildir,
	    rundir, &kp, &c.c_err, &wc.c_err));
}

static int
krun(void)
{
	size_t len;
	FILE *f;
	int rc;

	free(ran);
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	cap_reset(&c);
	cap_reset(&wc);
	rc = sh_cutover_run(z, &kp, &s, &fake, NOON, f, &c.c_err,
	    &wc.c_err);
	CHECK(fclose(f) == 0);
	return (rc);
}

#define	ASIDE	RX "-" V1

static void test_cutover_back(void);

/* The plan as the person confirming it reads it, into RAN. */
static void
kshow(void)
{
	size_t len;
	FILE *f;

	free(ran);
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	sh_cutover_show(f, &kp);
	CHECK(fclose(f) == 0);
}

/* Whether a snapshot of DS carries SH_UPDATE_MARK, its name said. */
static int
marked(const char *ds)
{
	char name[1024], **snaps;
	size_t i;
	int r;

	CHECK((snaps = sh_zw_snaps(z, ds, &c.c_err)) != NULL);
	for (i = 0, r = 0; snaps != NULL && snaps[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", ds, snaps[i]);
		if (strcmp(prop(name, SH_UPDATE_MARK), "-") != 0) {
			printf("%s marked\n", name);
			r = 1;
		}
	}
	sh_zw_free(snaps);
	return (r);
}

/* K1 - K4: cutover and abandon, after an update. */
static void
test_cutover(void)
{
	static const char *const gate[] = { "/etc/rc.conf", NULL };
	const char *stop, *save, *aside, *rename;
	char path[1300];

	settings(NULL);
	/* K1 */
	world(1);
	CHECK(kplan("nope", 0) == -1 && cap_has(&c, "no container nope"));
	CHECK(kplan("x", 0) == -1);
	CHECK(cap_has(&c, "no x-staging: no update of x to cut over"));
	rbfake_conflicts(RXS, 1, gate);
	CHECK(update(SH_UV_NEW, 0) == 1);
	CHECK(kplan("x", 1) == -1 && cap_has(&c, "the update of x is not "
	    "done: shtetl update --continue x, or --abort x"));
	CHECK(update(SH_UV_ABORT, 0) == 0);
	/* K2, K3: x written to during the soak, then cut over */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	zfake_write(RX "/var", 4096);
	CHECK(kplan("x", 0) == 0);
	CHECK(kp.kp_wrote == 4096 && kp.kp_nown == 1 && kp.kp_nlown == 1);
	CHECK(is(kp.kp_own[0], RXS "/www") && is(kp.kp_from, S3));
	CHECK(is(kp.kp_aside, ASIDE) && is(kp.kp_archive, ARCH));
	kshow();
	CHECK(strstr(ran, "  x was written to since its last save " S3
	    ", 4096 bytes: var 4096\n  a final save of x, for its "
	    "archive\n  x stopped, renamed " ASIDE ", its previous root\n"
	    "  x-staging stopped, renamed " RX ", its block x's\n") != NULL);
	CHECK(strstr(ran, "  x started and saved\n  last, the prune: "
	    "promoted " RX "/www; " ASIDE " destroyed\n  in zlake: promoted "
	    LX "/www off the archive\n") != NULL);
	/* the replayed from-save carries the mark, as x's does */
	CHECK(is(prop(RXS "@" S3, SH_UPDATE_MARK), V2));
	CHECK(krun() == 0);
	if (c.c_err.se_count != 0)
		printf("%s", cap_text(&c));
	CHECK(wc.c_err.se_count == 0);
	/* x aside, x-staging renamed x, x started and saved, then pruned */
	aside = strstr(ran, "renamed " RX " " ASIDE "\n");
	rename = strstr(ran, "renamed " RXS " " RX "\n");
	save = strstr(ran, "saved " RX "@" S5 "\n");
	stop = strstr(ran, "destroyed " ASIDE "\n");
	CHECK(aside != NULL && rename != NULL && save != NULL &&
	    stop != NULL && aside < rename && rename < save && save < stop);
	/* zrun: the staging tree is x, its own clone promoted */
	CHECK(!zfake_exists(RXS) && !zfake_exists(ASIDE));
	CHECK(is(prop(RX, "origin"), RF "@" V2));
	CHECK(is(prop(RX "@" V2, "shtetl:kind"), "version"));
	CHECK(is(prop(RX "/www", "origin"), "-"));
	CHECK(there(RX "/www@" S1) && there(RX "/www@" S3));
	CHECK(is(prop(RX, "mounted"), "yes"));
	(void) snprintf(path, sizeof (path), "%s/x", mnt);
	CHECK(is(prop(RX, "mountpoint"), path));
	/* its jail */
	(void) snprintf(path, sizeof (path), "%s/x.conf", jaildir);
	CHECK(strstr(slurp(path), "x {\n\tpath = \"") != NULL);
	(void) snprintf(path, sizeof (path), "%s/x-staging.conf", jaildir);
	CHECK(access(path, F_OK) != 0);
	CHECK(runs("x") && !runs("x-staging"));
	/* zlake: the old x archived, its final save in it */
	CHECK(zfake_exists(ARCH) && zfake_exists(ARCH "/www"));
	CHECK(is(prop(ARCH "@" S4, "shtetl:commit"), "written during the "
	    "soak of x-staging"));
	CHECK(is(prop(ARCH "/www", "origin"), LX "/www@" S3));
	CHECK(!zfake_exists(LXS));
	CHECK(is(prop(LX, "origin"), LF "@" V2));
	CHECK(is(prop(LX "/www", "origin"), "-"));
	/* saved, and mirrored onto the renamed mirror */
	CHECK(is(prop(RX "@" S5, "shtetl:commit"), "cutover to " V2));
	CHECK(there(LX "@" S5) && there(LX "/www@" S5));
	CHECK(strstr(ran, "cut over x\n") != NULL);
	/* the marks off, on zrun and in zlake */
	CHECK(!marked(RX) && !marked(LX));
	/* done: nothing more to cut over or abandon */
	CHECK(kplan("x", 0) == -1);
	CHECK(cap_has(&c, "no x-staging: no update of x to cut over"));
	CHECK(kplan("x", 1) == -1);
	CHECK(cap_has(&c, "no x-staging: no update of x to abandon"));
	/* the next update finds x on V2 */
	CHECK(plan("x", NULL, SH_UV_NEW) == -1);
	CHECK(cap_has(&c, "container x is on generic@" V2 " already"));
	/*
	 * K2: x started again and written to after the plan: stopped
	 * first, its writes saved all the same
	 */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	CHECK(kplan("x", 0) == 0 && kp.kp_wrote == 0);
	(void) scratch_write("running/x", "");
	zfake_write(RX "/var", 2048);
	CHECK(krun() == 0);
	stop = strstr(ran, "stopped the jail x\n");
	save = strstr(ran, "saved " RX "@" S4 "\n");
	CHECK(stop != NULL && save != NULL && stop < save);
	CHECK(is(prop(ARCH "@" S4, "shtetl:commit"), "written during the "
	    "soak of x-staging"));
	/* K4: abandon, the staging tree destroyed last */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	CHECK(kplan("containers/x", 1) == 0);
	kshow();
	CHECK(is(ran, "abandon x-staging, the update of x to @" V2 "\n"
	    "  x-staging stopped, its block removed\n  x started\n"
	    "  last, x-staging destroyed on zrun and in zlake\n"));
	CHECK(krun() == 0);
	CHECK(wc.c_err.se_count == 0);
	stop = strstr(ran, "started the jail x\n");
	save = strstr(ran, "destroyed " RXS "\n");
	CHECK(stop != NULL && save != NULL && stop < save);
	CHECK(!zfake_exists(RXS) && !zfake_exists(LXS));
	CHECK(runs("x") && !runs("x-staging"));
	(void) snprintf(path, sizeof (path), "%s/x-staging.conf", jaildir);
	CHECK(access(path, F_OK) != 0);
	CHECK(is(prop(RX, "origin"), RF "@" V1));
	CHECK(!marked(RX));
	CHECK(strstr(ran, "abandoned x-staging\n") != NULL);
	/* no mirror in zlake: a cutover refused */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	CHECK(sh_zw_destroy(z, LXS, &c.c_err) == 0);
	CHECK(kplan("x", 0) == -1);
	CHECK(cap_has(&c, "x-staging has no mirror in zlake: save it first"));
	/* an archive of x on that version there: refused */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	zfake_fs(ARCH);
	CHECK(kplan("x", 0) == -1);
	CHECK(cap_has(&c, ARCH ": an archive of x on that version exists"));
	/* the previous root's name taken: refused */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	zfake_fs(ASIDE);
	CHECK(kplan("x", 0) == -1);
	CHECK(cap_has(&c, ASIDE ": the name of x's previous root is taken"));
	/* K5, K8, K9 */
	test_cutover_back();
}

/*
 * K5: x's cutover failing at the NTH call of WHAT, or at x's jail's
 * start (WHAT NULL, x-staging's stopped beforehand so that putting it
 * back does not meet the same), x running (XRUNS) or not and written
 * to (WROTE) or not: put back whole, the trees, jails, blocks and marks
 * as they were; and run again, cut over.
 */
static void
back(const char *what, int nth, int xruns, int wrote)
{
	char xconf[1300], sconf[1300], xblk[4096], sblk[4096];
	int sruns, xm, sm;

	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	if (xruns)
		(void) scratch_write("running/x", "");
	else
		(void) unlink(scratch_path("running/x"));
	if (what == NULL)
		(void) unlink(scratch_path("running/x-staging"));
	if (wrote)
		zfake_write(RX "/var", 2048);
	(void) snprintf(xconf, sizeof (xconf), "%s/x.conf", jaildir);
	(void) snprintf(sconf, sizeof (sconf), "%s/x-staging.conf", jaildir);
	(void) snprintf(xblk, sizeof (xblk), "%s", slurp(xconf));
	(void) snprintf(sblk, sizeof (sblk), "%s", slurp(sconf));
	sruns = runs("x-staging");
	xm = is(prop(RX, "mounted"), "yes");
	sm = is(prop(RXS, "mounted"), "yes");
	CHECK(kplan("x", 0) == 0);
	if (what != NULL)
		zfake_fail_nth(what, nth);
	else
		(void) scratch_write("jailfails", "");
	CHECK(krun() == -1);
	(void) unlink(scratch_path("jailfails"));
	if (!cap_has(&c, "cutover of x failed and was put back")) {
		printf("%s: %d: %s", what != NULL ? what : "jail", nth,
		    cap_text(&c));
		CHECK(0);
	}
	/* zrun's trees, mounted as they were */
	CHECK(!zfake_exists(ASIDE));
	CHECK(is(prop(RX, "origin"), RF "@" V1));
	CHECK(is(prop(RXS, "origin"), RF "@" V2));
	CHECK(is(prop(RXS "/www", "origin"), RX "/www@" S3));
	CHECK(is(prop(RX, "mounted"), "yes") == xm);
	CHECK(is(prop(RXS, "mounted"), "yes") == sm);
	/* zlake's */
	CHECK(!zfake_exists(ARCH));
	CHECK(is(prop(LX, "origin"), LF "@" V1));
	CHECK(is(prop(LXS, "origin"), LF "@" V2));
	/* the jails, their blocks, the marks */
	CHECK(runs("x") == xruns && runs("x-staging") == sruns);
	CHECK(is(slurp(xconf), xblk));
	CHECK(is(slurp(sconf), sblk));
	CHECK(is(prop(RX "@" S3, SH_UPDATE_MARK), V2));
	CHECK(is(prop(RXS "@" S3, SH_UPDATE_MARK), V2));
	/* run again, from the start */
	CHECK(kplan("x", 0) == 0 && krun() == 0);
	CHECK(!zfake_exists(RXS) && !zfake_exists(ASIDE));
	CHECK(zfake_exists(ARCH) && is(prop(LX, "origin"), LF "@" V2));
	CHECK(is(prop(RX, "origin"), RF "@" V2) && runs("x"));
	CHECK(is(prop(RX "/www", "origin"), "-"));
	CHECK(!marked(RX) && !marked(LX));
}

/*
 * K8: an abandon failing, put back; its destroys failing, done all the
 * same; refused on what would keep the staging tree.
 */
static void
test_abandon(void)
{
	char sconf[1300], sblk[4096];

	/* x's start failing: x-staging's block and the mark back */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	(void) unlink(scratch_path("running/x"));
	(void) unlink(scratch_path("running/x-staging"));
	(void) snprintf(sconf, sizeof (sconf), "%s/x-staging.conf", jaildir);
	(void) snprintf(sblk, sizeof (sblk), "%s", slurp(sconf));
	CHECK(strstr(sblk, "x-staging {") != NULL);
	CHECK(kplan("x", 1) == 0);
	(void) scratch_write("jailfails", "");
	CHECK(krun() == -1);
	(void) unlink(scratch_path("jailfails"));
	CHECK(cap_has(&c, "abandon of x failed and was put back"));
	CHECK(zfake_exists(RXS) && zfake_exists(LXS) && !runs("x"));
	CHECK(is(slurp(sconf), sblk));
	CHECK(is(prop(RX "@" S3, SH_UPDATE_MARK), V2));
	CHECK(kplan("x", 1) == 0 && krun() == 0);
	CHECK(!zfake_exists(RXS) && !zfake_exists(LXS) && runs("x"));
	CHECK(!marked(RX));
	/* the staging tree's destroy failing: abandoned, it left */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	CHECK(kplan("x", 1) == 0);
	zfake_fail("destroy");
	CHECK(krun() == 0);
	CHECK(cap_has(&wc, RXS " not destroyed: the update of x is "
	    "abandoned, the staging tree left as it is"));
	CHECK(c.c_err.se_count == 0);
	CHECK(strstr(ran, "abandoned x-staging\n") != NULL);
	CHECK(zfake_exists(RXS) && !zfake_exists(LXS) && runs("x"));
	CHECK(!marked(RX));
	/* refused: a hold on the staging tree */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	zfake_hold(RXS "@" V2);
	CHECK(kplan("x", 1) == -1);
	CHECK(cap_has(&c, RXS "@" V2 " is held (zfs holds), which keeps it "
	    "from being destroyed"));
}

/*
 * K9: what the plan refuses on beforehand, and what only the prune
 * meets: x cut over all the same, the previous root left whole and
 * said.
 */
static void
test_prune(void)
{
	static const struct sh_zprop kind = { "shtetl:kind", "save" };
	const char *clash[] = { RXS "/www@" S1 };

	/* a hold, and a clone outside: x cut over, its previous root left */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	zfake_hold(RX "/var@" S1);
	zfake_hold(RX "/www@" S1);	/* the promote takes it along */
	CHECK(sh_zfs_clone(z, RX "/usr/local@" S2, "fast/run/containers/y",
	    NULL, 0, &c.c_err) == 0);
	/* the promote takes its origin along */
	CHECK(sh_zfs_clone(z, RX "/www@" S2, "fast/run/containers/w", NULL,
	    0, &c.c_err) == 0);
	CHECK(kplan("x", 0) == 0);
	CHECK(krun() == 0);
	CHECK(c.c_err.se_count == 0);
	CHECK(cap_has(&wc, ASIDE "/var@" S1 " is held (zfs holds), which "
	    "keeps it from being destroyed"));
	CHECK(!cap_has(&wc, "/www@" S1 " is held"));
	CHECK(cap_has(&wc, "fast/run/containers/y is a clone of " ASIDE
	    "/usr/local@" S2 ", which keeps it from being destroyed"));
	CHECK(!cap_has(&wc, "fast/run/containers/w is a clone"));
	CHECK(cap_has(&wc, ASIDE " not pruned: x is cut over, its previous "
	    "root left as it is"));
	CHECK(strstr(ran, "cut over x\n") != NULL);
	CHECK(zfake_exists(ASIDE) && there(ASIDE "/var@" S1));
	CHECK(is(prop(ASIDE, "mounted"), "yes"));
	CHECK(is(prop(RX "/www", "origin"), "-") && there(RX "/www@" S1));
	CHECK(is(prop(RX, "origin"), RF "@" V2) && runs("x"));
	CHECK(!marked(RX));
	/* only the hold the promote takes along: pruned */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	zfake_hold(RX "/www@" S1);
	CHECK(kplan("x", 0) == 0 && krun() == 0);
	CHECK(wc.c_err.se_count == 0 && !zfake_exists(ASIDE));
	CHECK(there(RX "/www@" S1));
	/* the promote failing: left too */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	CHECK(kplan("x", 0) == 0);
	zfake_fail("promote");
	CHECK(krun() == 0);
	CHECK(cap_has(&wc, ASIDE " not pruned"));
	CHECK(zfake_exists(ASIDE));
	CHECK(is(prop(RX "/www", "origin"), ASIDE "/www@" S3));
	/* zlake's promote failing: the archive not stood alone, said */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	CHECK(kplan("x", 0) == 0);
	zfake_fail_nth("promote", 2);
	CHECK(krun() == 0);
	CHECK(cap_has(&wc, LX " not promoted off the archive " ARCH));
	CHECK(!cap_has(&wc, "not pruned"));
	CHECK(!zfake_exists(ASIDE));
	CHECK(is(prop(LX "/www", "origin"), ARCH "/www@" S3));
	/* refused beforehand: a snapshot name a promote would meet */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	CHECK(sh_zfs_snapshot(z, clash, 1, &kind, 1, &c.c_err) == 0);
	CHECK(kplan("x", 0) == -1);
	CHECK(cap_has(&c, RX "/www@" S1 ": " RXS "/www has a snapshot of "
	    "that name, which a promote of it would meet"));
	CHECK(zfake_exists(RXS) && runs("x-staging"));
	/* and in zlake: the clash, and the archive's name taken */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	clash[0] = LXS "/www@" S3;	/* the mirror's history starts there */
	CHECK(sh_zfs_snapshot(z, clash, 1, &kind, 1, &c.c_err) == 0);
	zfake_fs(ARCH);
	CHECK(kplan("x", 0) == -1);
	CHECK(cap_has(&c, LX "/www@" S3 ": " LXS "/www has a snapshot of "
	    "that name, which a promote of it would meet"));
	CHECK(cap_has(&c, ARCH ": an archive of x on that version exists"));
}

/* K5, K8, K9. */
static void
test_cutover_back(void)
{
	/* K5: a failure at each step that changes something */
	back("sync", 1, 0, 0);		/* x's writes read again */
	back("snapshot", 1, 1, 1);	/* its final save; x started again */
	back("rename", 1, 1, 1);	/* x aside, after the final save */
	back("rename", 2, 0, 0);	/* x-staging renamed x */
	back("rename", 3, 1, 0);	/* in zlake: x archived */
	back("rename", 4, 0, 0);	/* the staging mirror renamed x */
	back("inherit", 1, 0, 0);	/* the mark off */
	back(NULL, 0, 0, 0);		/* x's jail started */
	back("snapshot", 2, 0, 1);	/* the closing save */
	/* putting back stopped part way: x-staging's jail would not start */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	CHECK(runs("x-staging"));
	CHECK(kplan("x", 0) == 0);
	(void) scratch_write("jailfails", "");
	CHECK(krun() == -1);
	(void) unlink(scratch_path("jailfails"));
	CHECK(cap_has(&c, "not put back: starting the jail x-staging"));
	CHECK(cap_has(&c, "cutover of x failed, and putting it back stopped "
	    "part way"));
	CHECK(zfake_exists(RXS) && !zfake_exists(ASIDE) && !zfake_exists(ARCH));
	CHECK(!runs("x-staging") && !runs("x"));
	CHECK(is(prop(RXS "@" S3, SH_UPDATE_MARK), V2));
	/* the closing save's mirror failing: cut over, the mirror behind */
	world(1);
	CHECK(update(SH_UV_NEW, 0) == 0);
	CHECK(kplan("x", 0) == 0);
	zfake_fail("send");
	CHECK(krun() == 0);
	CHECK(c.c_err.se_count == 0);
	CHECK(cap_has(&wc, "@" S5 " is saved; its mirror into zlake failed, "
	    "and the next save brings it up to date"));
	CHECK(strstr(ran, "cut over x\n") != NULL);
	CHECK(there(RX "@" S5) && !there(LX "@" S5) && !zfake_exists(ASIDE));
	/* K8, K9 */
	test_abandon();
	test_prune();
}

/* U7: a rebase refused, or failed part way: undone. */
static void
test_failures(void)
{
	settings(NULL);
	world(1);
	rbfake_exit(RXS "/usr/local", 1, SH_RB_REFUSED);
	CHECK(update(SH_UV_NEW, 0) == -1);
	CHECK(cap_has(&c, RXS "/usr/local: zfs_rebase refused"));
	CHECK(cap_has(&c, "update of x failed and was given up"));
	CHECK(!zfake_exists(RXS) && runs("x"));
	world(1);
	rbfake_exit(RXS "/var", 2, SH_RB_FAILED);
	CHECK(update(SH_UV_NEW, 0) == -1);
	CHECK(strstr(rbfake_log(), "abort " RXS "/var\n") != NULL);
	CHECK(!zfake_exists(RXS) && runs("x"));
}

int
main(void)
{
	cap_open(&c);
	cap_open(&wc);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	config();
	test_refused();
	test_plan();
	test_run();
	test_gate();
	test_gate_merge();
	test_derivations();
	test_failures();
	test_cutover();
	sh_update_free(&p);
	sh_cutover_free(&kp);
	sh_spaces_free(&s);
	sh_node_free(site);
	sh_zfs_close(z);
	free(ran);
	rbfake_reset();
	cap_close(&c);
	cap_close(&wc);
	scratch_clean();
	printf("check_update: %d checks\n", checks);
	return (0);
}
