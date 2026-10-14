/*
 * shtetl create X --on F (planning.md 7.4): tests/MATRIX.md family X,
 * over the fake ZFS, a lake and a config root in one pool and a run
 * root in another, with jail, jexec and pkg played by scripts that log
 * what they were asked.
 */

#include <sys/stat.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "check.h"
#include "core/beam.h"
#include "core/build.h"
#include "core/create.h"
#include "core/spaces.h"
#include "core/zfs.h"
#include "zfake.h"

#define	LF	"tank/shtetl/foundations/generic"
#define	RF	"fast/run/foundations/generic"
#define	RX	"fast/run/containers/x"
#define	LX	"tank/shtetl/containers/x"
#define	V1	"15.1-releng-2026-10-01-0f691888dc56"
#define	BSNAP	"build-generic-20261001-120000"
#define	BCFG	"tank/zconfig@" BSNAP
#define	XCFG	"tank/zconfig@create-x-20261008-120000"
#define	S1	"save-2026-10-08-120000"
/* 2026-10-08 12:00:00 UTC */
#define	NOON	((time_t)1791460800)

static struct cap c, wc;
static struct sh_zfs *z;
static struct sh_spaces s;
static struct sh_cplan p;
static struct sh_tools fake;
static char root[1200], jaildir[1200], logbase[1200], mnt[1200];
static char hostresolv[1200], toollog[1200], failed[1200];
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

/* Whether NAME, a dataset or a snapshot, is there. */
static int
there(const char *name)
{
	return (strcmp(prop(name, "type"), "(refused)") != 0);
}

/* The file PATH, whole, or "(none)". */
static const char *
slurp(const char *path)
{
	static char buf[8192];
	size_t n;
	FILE *f;

	if ((f = fopen(path, "r")) == NULL)
		return ("(none)");
	n = fread(buf, 1, sizeof (buf) - 1, f);
	buf[n] = '\0';
	(void) fclose(f);
	return (buf);
}

/* Whether the tools were asked for TEXT, a line of the log. */
static int
asked(const char *text)
{
	return (strstr(slurp(toollog), text) != NULL);
}

/* A file of the scratch directory, copied out of scratch_path's one. */
static void
spath(char *buf, size_t len, const char *name)
{
	(void) snprintf(buf, len, "%s", scratch_path(name));
}

/*
 * A script NAME in the scratch tools directory, logging its calls and
 * failing while the scratch file fail/NAME is there; BODY then runs.
 */
static const char *
tool(const char *name, const char *body)
{
	char text[4096], rel[64];
	static char path[3][1200];
	static int n;

	(void) snprintf(text, sizeof (text), "#!/bin/sh\n"
	    "echo \"%s $*\" >> %s\n"
	    "[ -f %s/%s ] && { echo \"%s failed\" >&2; exit 3; }\n%s"
	    "exit 0\n", name, toollog, failed, name, name, body);
	(void) snprintf(rel, sizeof (rel), "tools/%s", name);
	(void) scratch_write(rel, text);
	n = (n + 1) % 3;
	spath(path[n], sizeof (path[n]), rel);
	CHECK(chmod(path[n], 0755) == 0);
	return (path[n]);
}

/* What the jail's block reads with the recipe x's jailconf. */
static const char *
block(void)
{
	static char want[2048];

	(void) snprintf(want, sizeof (want),
	    "# written by shtetl for the container x; the recipe's "
	    "jailconf\n# is where to change it\nx {\n"
	    "\tpath = \"%s/x\";\n\thost.hostname = \"x\";\n"
	    "\texec.start = \"/bin/sh /etc/rc\";\n"
	    "\texec.stop = \"/bin/sh /etc/rc.shutdown jail\";\n"
	    "\texec.clean;\n\tmount.devfs;\n\tdevfs_ruleset = 4;\n"
	    "\tip4 = inherit;\n\tip6 = inherit;\n"
	    "\tallow.raw_sockets;\n\tip4.addr = \"10.0.0.5\";\n}\n", mnt);
	return (want);
}

/*
 * shtetl.conf, made by config: an external command the foundation ran,
 * and the container block's, the host's resolv.conf put in by one, as
 * shtetl-recipes puts it in, another saying where it ran.
 */
static char shtetl_conf[2048];

/*
 * The configuration root: shtetl.conf, the foundation generic, the
 * containers; and the config snapshot generic's version was built
 * from, as .zfs/snapshot shows it, the same shtetl.conf.
 */
static void
config(void)
{
	static const char *const files[] = {
		"conf/shtetl.conf", shtetl_conf,
		"conf/foundations/generic.conf",
		"src_branch = \"releng/15.1\";\n",
		"conf/containers/x.conf",
		"pkg = [ \"nginx\", \"curl\" ];\n"
		"datasets { var { compression = \"zstd\"; } \"www/data\" {} }\n"
		"commands { base = \"echo x\"; web = \"echo web\"; }\n"
		"jailconf = \"allow.raw_sockets;\\n\\nip4.addr = "
		"\\\"10.0.0.5\\\";\";\n",
		"conf/containers/db.conf",
		"pkg = [ \"postgresql17-server\" ];\n",
		"conf/containers/needy.conf", "requires = [ \"db\" ];\n",
		"conf/containers/new.conf", "ver = \">=16.0\";\n",
		"conf/containers/fails.conf",
		"commands { bad = \"echo fail\"; }\n",
		"conf/containers/123.conf", "",
		"conf/containers/extfail.conf",
		"commands-external { bad = \"exit 3\"; }\n",
		"conf/.zfs/snapshot/" BSNAP "/shtetl.conf", shtetl_conf,
		"conf/.zfs/snapshot/" BSNAP "/foundations/generic.conf",
		"src_branch = \"releng/15.1\";\n",
		NULL
	};
	size_t i;

	spath(toollog, sizeof (toollog), "tools.log");
	(void) scratch_write("resolv.conf", "nameserver 192.0.2.53\n");
	spath(hostresolv, sizeof (hostresolv), "resolv.conf");
	(void) snprintf(shtetl_conf, sizeof (shtetl_conf),
	    "ver = \">=15.1\";\n"
	    "datasets { var {} \"usr/local\" {} }\n"
	    "commands { base = \"echo base\"; }\n"
	    "commands-external { early = \"echo early >> %s\"; }\n"
	    "container {\n"
	    "    commands { motd = \"echo motd\"; }\n"
	    "    commands-external {\n"
	    "        resolv = \"[ -e \\\"$SHTETL_ROOT/etc/resolv.conf\\\" ] || "
	    "cp %s \\\"$SHTETL_ROOT/etc/resolv.conf\\\"\";\n"
	    "        mark = \"echo \\\"external $SHTETL_NAME $SHTETL_KIND "
	    "$SHTETL_ROOT\\\" >> %s\";\n"
	    "    }\n"
	    "}\n", toollog, hostresolv, toollog);
	for (i = 0; files[i] != NULL; i += 2)
		(void) scratch_write(files[i], files[i + 1]);
	spath(root, sizeof (root), "conf");
	spath(jaildir, sizeof (jaildir), "jail.conf.d");
	spath(logbase, sizeof (logbase), "log");
	spath(mnt, sizeof (mnt), "mnt");
	spath(failed, sizeof (failed), "fail");
	(void) scratch_write("fail/.keep", "");
	fake.tl_jail = tool("jail", "");
	fake.tl_jexec = tool("jexec", "case \"$5\" in *fail*) exit 4;; esac\n");
	fake.tl_pkg = tool("pkg", "");
	fake.tl_sh = "/bin/sh";
}

/* LF and its datasets snapshotted as V, KIND, its revision REV. */
static void
version(const char *v, const char *kind, const char *rev)
{
	static const char *const ds[] = { LF, LF "/usr", LF "/usr/local",
	    LF "/var" };
	char names[4][512];
	const char *np[4];
	struct sh_zprop zp[4];
	size_t i, n;

	for (i = 0; i < 4; i++) {
		(void) snprintf(names[i], sizeof (names[i]), "%s@%s", ds[i], v);
		np[i] = names[i];
	}
	zp[0].zp_name = "shtetl:kind";
	zp[0].zp_value = kind;
	zp[1].zp_name = "shtetl:config";
	zp[1].zp_value = BCFG;
	n = 2;
	if (rev != NULL) {
		zp[n].zp_name = "shtetl:revision";
		zp[n++].zp_value = rev;
	}
	cap_reset(&wc);
	CHECK(sh_zfs_snapshot(z, np, 4, zp, n, &wc.c_err) == 0);
}

/*
 * The roots, the foundation generic in zlake at V1, X's mount empty
 * but for etc/; with RUN 0 no run root, with BEAMED V1 on zrun too.
 */
static void
world(int run, int beamed)
{
	static const struct sh_zprop cfg = { "shtetl:kind", "config" };
	static const char *const bcfg[] = { BCFG };
	static const char *const xs[] = { "x", "db", "fails", "extfail" };
	char path[1300];
	struct sh_beam b;
	size_t i;
	FILE *f;

	zfake_reset();
	zfake_pool("tank");
	zfake_fs("tank/shtetl");
	zfake_set("tank/shtetl", "shtetl:space", "lake");
	zfake_set("tank/shtetl", "mountpoint", "none");
	zfake_fs("tank/shtetl/foundations");
	zfake_fs("tank/shtetl/containers");
	zfake_fs("tank/zconfig");
	zfake_set("tank/zconfig", "shtetl:space", "config");
	zfake_fs(LF);
	zfake_fs(LF "/usr");
	zfake_set(LF "/usr", "canmount", "off");
	zfake_fs(LF "/usr/local");
	zfake_fs(LF "/var");
	cap_reset(&wc);
	CHECK(sh_zfs_snapshot(z, bcfg, 1, &cfg, 1, &wc.c_err) == 0);
	version(V1, "version", "15.1");
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
	/* each container's /etc, as a foundation's clone has it; no jail */
	for (i = 0; i < 4; i++) {
		(void) snprintf(path, sizeof (path), "%s/%s/etc/resolv.conf",
		    mnt, xs[i]);
		(void) unlink(path);
		(void) snprintf(path, sizeof (path), "%s/%s.conf", jaildir,
		    xs[i]);
		(void) unlink(path);
		(void) snprintf(path, sizeof (path), "mnt/%s/etc/.keep", xs[i]);
		(void) scratch_write(path, "");
	}
	(void) unlink(toollog);
	if (!beamed)
		return;
	CHECK(sh_beam_plan(z, &s, "generic", V1, &b, &c.c_err) == 0);
	CHECK((f = fopen("/dev/null", "w")) != NULL);
	CHECK(sh_beam_run(z, &b, f, &c.c_err) == 0);
	(void) fclose(f);
}

/* The plan for TARGET on FOUND at VERSION: 0, or -1, reasons in c. */
static int
plan(const char *target, const char *found, const char *version)
{
	int rc;

	sh_create_free(&p);
	cap_reset(&c);
	cap_reset(&wc);
	rc = sh_create_plan(z, &s, root, target, found, version, jaildir,
	    logbase, &p, &c.c_err, &wc.c_err);
	return (rc);
}

static void
refused(const char *target, const char *found, const char *version,
    const char *want)
{
	int rc;

	rc = plan(target, found, version);
	if (rc == 0 || !cap_has(&c, want))
		printf("%s: wanted \"%s\", got:\n%s", target, want,
		    cap_text(&c));
	CHECK(rc == -1 && cap_has(&c, want));
}

/* The plan carried out, with MSG: its return; what it said in ran. */
static int
run(const char *msg)
{
	size_t len;
	FILE *f;
	int rc;

	free(ran);
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	cap_reset(&c);
	rc = sh_create_run(z, &p, &s, &fake, msg, NOON, f, &c.c_err);
	CHECK(fclose(f) == 0);
	return (rc);
}

/* TARGET planned and run on generic, MSG its message: 0 or -1. */
static int
create(const char *target, const char *msg)
{
	if (plan(target, "generic", NULL) != 0) {
		printf("plan refused:\n%s", cap_text(&c));
		CHECK(0);
	}
	return (run(msg));
}

/* X1: what is refused. */
static void
test_refused(void)
{
	world(0, 0);
	refused("x", "generic", NULL, "no run root");
	world(1, 0);
	refused("X.y", "generic", NULL, "X.y: invalid container name");
	refused("123", "generic", NULL, "123: invalid container name: "
	    "digits only");
	refused("foundations/x", "generic", NULL, "foundations/x: invalid "
	    "container");
	refused("x", "containers/x", NULL, "containers/x: invalid "
	    "foundation");
	refused("nope", "generic", NULL, "containers/nope.conf: no such file");
	refused("x", "nope", NULL, "no foundation nope");
	zfake_fs("tank/shtetl/foundations/bare");
	refused("x", "bare", NULL, "foundation bare exists without a version");
	refused("x", "generic", "v9", "foundation generic has no version v9");
	refused("new", "generic", NULL, "16.0");
	refused("needy", "generic", NULL, "requires: no container db");
	version("v-norev", "version", NULL);
	refused("x", "generic", NULL, "generic@v-norev: no shtetl:revision");
	world(1, 0);
	zfake_fs(RX);
	refused("x", "generic", NULL, "container x exists");
	/* each reason, not the first */
	world(1, 0);
	zfake_fs("fast/run/containers/new");
	CHECK(plan("new", "generic", "v9") == -1);
	CHECK(cap_has(&c, "container new exists"));
	CHECK(cap_has(&c, "foundation generic has no version v9"));
}

/* X2: the plan, and its showing. */
static void
test_plan(void)
{
	size_t len;
	FILE *f;

	world(1, 0);
	CHECK(plan("containers/x", "generic", NULL) == 0);
	CHECK(is(p.cp_name, "x") && is(p.cp_found, "generic"));
	CHECK(is(p.cp_version, V1) && p.cp_beam == 1);
	CHECK(is(p.cp_beamplan.bm_version, V1));
	CHECK(is(p.cp_run, RX) && is(p.cp_runf, RF));
	CHECK(is(p.cp_config, "tank/zconfig"));
	CHECK(p.cp_nds == 6);
	CHECK(is(p.cp_dss[0].cd_rel, "") && p.cp_dss[0].cd_clone &&
	    !p.cp_dss[0].cd_placeholder && p.cp_dss[0].cd_props == NULL);
	CHECK(is(p.cp_dss[1].cd_rel, "usr") && p.cp_dss[1].cd_clone &&
	    p.cp_dss[1].cd_placeholder);
	CHECK(is(p.cp_dss[2].cd_rel, "usr/local") && p.cp_dss[2].cd_clone);
	CHECK(is(p.cp_dss[3].cd_rel, "var") && p.cp_dss[3].cd_clone &&
	    !p.cp_dss[3].cd_placeholder && p.cp_dss[3].cd_props != NULL);
	CHECK(is(p.cp_dss[4].cd_rel, "www") && !p.cp_dss[4].cd_clone &&
	    p.cp_dss[4].cd_placeholder);
	CHECK(is(p.cp_dss[5].cd_rel, "www/data") && !p.cp_dss[5].cd_clone &&
	    !p.cp_dss[5].cd_placeholder);
	/*
	 * shtetl.conf's base and early the foundation ran; x's base and
	 * web not, nor the container block's, no foundation's
	 */
	CHECK(p.cp_ncmds == 3 && p.cp_skipped == 1);
	CHECK(is(p.cp_cmds[0].cp_label, "base"));
	CHECK(is(p.cp_cmds[0].cp_file, "containers/x.conf"));
	CHECK(is(p.cp_cmds[0].cp_text, "echo x"));
	CHECK(is(p.cp_cmds[1].cp_label, "motd"));
	CHECK(is(p.cp_cmds[1].cp_file, "container in shtetl.conf"));
	CHECK(is(p.cp_cmds[2].cp_label, "web"));
	CHECK(p.cp_next == 2 && p.cp_extskipped == 1);
	CHECK(is(p.cp_ext[0].cp_label, "resolv"));
	CHECK(is(p.cp_ext[0].cp_file, "container in shtetl.conf"));
	CHECK(is(p.cp_ext[1].cp_label, "mark"));
	CHECK(strncmp(p.cp_jailfile, jaildir, strlen(jaildir)) == 0 &&
	    is(p.cp_jailfile + strlen(jaildir), "/x.conf"));
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	sh_create_show(f, &p);
	CHECK(fclose(f) == 0);
	CHECK(strstr(ran, "container x, on generic@" V1 ", beamed to zrun "
	    "first\n") != NULL);
	CHECK(strstr(ran, "  + " RX "  clone\n") != NULL);
	CHECK(strstr(ran, "  + " RX "/usr  clone, canmount=off\n") != NULL);
	CHECK(strstr(ran, "  + " RX "/var  clone, compression=zstd\n") !=
	    NULL);
	CHECK(strstr(ran, "  + " RX "/www  its own, canmount=off\n") != NULL);
	CHECK(strstr(ran, "  + " RX "/www/data  its own\n") != NULL);
	CHECK(strstr(ran, "  pkg: nginx, curl\n") != NULL);
	CHECK(strstr(ran, "  external: resolv (container in shtetl.conf), "
	    "mark (container in shtetl.conf); 1 the foundation ran\n") !=
	    NULL);
	CHECK(strstr(ran, "  commands: base (containers/x.conf), motd "
	    "(container in shtetl.conf), web (containers/x.conf); 1 the "
	    "foundation ran\n") != NULL);
	free(ran);
	ran = NULL;

	/* the shtetl.conf piece changed since the version: it runs */
	(void) scratch_write("conf/shtetl.conf", "ver = \">=15.1\";\n"
	    "datasets { var {} \"usr/local\" {} }\n"
	    "commands { base = \"echo base, again\"; }\n");
	CHECK(plan("x", "generic", NULL) == 0);
	CHECK(p.cp_ncmds == 3 && p.cp_skipped == 0);
	CHECK(is(p.cp_cmds[0].cp_file, "shtetl.conf"));
	(void) scratch_write("conf/shtetl.conf", shtetl_conf);

	/* the newest, a -pkg; one given; on zrun already, not beamed */
	version(V1 "-pkg", "pkg", "15.1");
	CHECK(plan("x", "generic", NULL) == 0 && is(p.cp_version, V1 "-pkg"));
	CHECK(plan("x", "generic", V1) == 0 && is(p.cp_version, V1));
	world(1, 1);
	CHECK(plan("x", "generic", NULL) == 0 && p.cp_beam == 0);
	/* a dataset F split after the version is not the version's */
	zfake_fs(LF "/home");
	CHECK(plan("x", "generic", NULL) == 0 && p.cp_nds == 6);
}

/* X3 - X6: the run. */
static void
test_run(void)
{
	char path[1300];

	world(1, 0);
	CHECK(create("x", "hello") == 0);
	/* X3 */
	CHECK(is(prop(XCFG, "shtetl:kind"), "config"));
	CHECK(is(prop(RF "@" V1, "shtetl:kind"), "version"));
	CHECK(is(prop(RX, "origin"), RF "@" V1));
	CHECK(is(prop(RX "/usr", "origin"), RF "/usr@" V1));
	CHECK(is(prop(RX "/usr", "canmount"), "off"));
	CHECK(is(prop(RX "/usr/local", "origin"), RF "/usr/local@" V1));
	CHECK(is(prop(RX "/var", "compression"), "zstd"));
	CHECK(is(prop(RX "/www", "origin"), "-"));
	CHECK(is(prop(RX "/www", "canmount"), "off"));
	CHECK(is(prop(RX "/www/data", "origin"), "-"));
	CHECK(is(prop(RX, "mounted"), "yes"));
	CHECK(is(prop(RX "/usr", "mounted"), "no"));
	CHECK(is(prop(RX "/usr/local", "mounted"), "yes"));
	CHECK(is(prop(RX "/www", "mounted"), "no"));
	CHECK(is(prop(RX "/www/data", "mounted"), "yes"));
	/* a clone keeps none of its origin's local properties */
	CHECK(is(prop(RX, "readonly"), "off"));
	CHECK(strstr(ran, "snapshot " XCFG "\n") != NULL);
	CHECK(strstr(ran, "cloned " RX "/usr/local\n") != NULL);
	CHECK(strstr(ran, "made " RX "/www/data\n") != NULL);
	/* X4 */
	(void) snprintf(path, sizeof (path), "%s/x.conf", jaildir);
	CHECK(is(slurp(path), block()));
	(void) snprintf(path, sizeof (path), "jail -f %s/x.conf -c x\n",
	    jaildir);
	CHECK(asked(path));
	/* X5 */
	(void) snprintf(path, sizeof (path), "%s/x/etc/resolv.conf", mnt);
	CHECK(is(slurp(path), "nameserver 192.0.2.53\n"));
	CHECK(asked("pkg -j x install -y -- nginx curl\n"));
	CHECK(asked("jexec x /bin/sh -e -c echo x\n"));
	CHECK(asked("jexec x /bin/sh -e -c echo web\n"));
	CHECK(asked("jexec x /bin/sh -e -c echo motd\n"));
	CHECK(!asked("echo base") && !asked("early"));
	/* the external pieces on the host, X's root theirs, before the jail */
	(void) snprintf(path, sizeof (path), "external x container %s/x\n",
	    mnt);
	CHECK(asked(path));
	CHECK(strstr(slurp(toollog), "external x") < strstr(slurp(toollog),
	    "jail -f"));
	CHECK(strstr(slurp(toollog), "pkg -j") < strstr(slurp(toollog),
	    "jexec"));
	(void) snprintf(path, sizeof (path), "%s/x/create.log", logbase);
	CHECK(strcmp(slurp(path), "(none)") != 0);
	/* X6 */
	CHECK(is(prop(RX "@" S1, "shtetl:kind"), "save"));
	CHECK(is(prop(RX "@" S1, "shtetl:commit"), "hello"));
	CHECK(is(prop(RX "@" S1, "shtetl:config"), XCFG));
	CHECK(is(prop(RX "/www/data@" S1, "shtetl:kind"), "save"));
	CHECK(is(prop(LX "@" S1, "shtetl:kind"), "save"));
	CHECK(is(prop(LX, "origin"), LF "@" V1));
	CHECK(is(prop(LX "/www/data", "origin"), "-"));
	CHECK(strstr(ran, "created x on generic@" V1 "\n") != NULL);

	/* the message create's own; resolv.conf there already, kept */
	world(1, 1);
	(void) snprintf(path, sizeof (path), "%s/x/etc/resolv.conf", mnt);
	(void) scratch_write("mnt/x/etc/resolv.conf", "nameserver ::1\n");
	CHECK(create("x", NULL) == 0);
	CHECK(is(prop(RX "@" S1, "shtetl:commit"), "create x on generic@"
	    V1));
	CHECK(is(slurp(path), "nameserver ::1\n"));
	CHECK(strstr(ran, "beamed") == NULL);
	/* db's packages; of commands only the container block's */
	world(1, 1);
	CHECK(create("db", "db") == 0);
	CHECK(asked("pkg -j db install -y -- postgresql17-server\n"));
	CHECK(asked("jexec db /bin/sh -e -c echo motd\n"));
	CHECK(asked("external db container "));
	CHECK(!asked("echo x") && !asked("echo base"));
}

/* Whether nothing of x is left but the config snapshot. */
static int
undone(void)
{
	char path[1300];

	(void) snprintf(path, sizeof (path), "%s/x.conf", jaildir);
	if (zfake_exists(RX) || access(path, F_OK) == 0 ||
	    !there(XCFG) || !cap_has(&c, "create of x failed; log ")) {
		printf("not undone:\n%s", cap_text(&c));
		return (0);
	}
	return (1);
}

/* X7: a failure at each step undone; the mirror's after the save not. */
static void
test_undo(void)
{
	static const char *const zfs[] = { "clone", "create", "mount",
	    NULL };
	char path[1300], stop[1300];
	size_t i;

	(void) snprintf(stop, sizeof (stop), "jail -f %s/x.conf -r x\n",
	    jaildir);
	for (i = 0; zfs[i] != NULL; i++) {
		world(1, 1);
		CHECK(plan("x", "generic", NULL) == 0);
		zfake_fail_nth(zfs[i], 2);
		CHECK(run("m") == -1);
		CHECK(undone());
		CHECK(!asked("jail"));
	}
	/* the beam */
	world(1, 0);
	CHECK(plan("x", "generic", NULL) == 0);
	zfake_fail("send");
	CHECK(run("m") == -1 && undone());
	CHECK(cap_has(&c, "beam of generic@" V1 " failed"));
	CHECK(!zfake_exists(RF));
	/* the jail's start: its block removed, nothing to stop */
	world(1, 1);
	CHECK(plan("x", "generic", NULL) == 0);
	(void) scratch_write("fail/jail", "");
	CHECK(run("m") == -1 && undone() && !asked(stop));
	(void) unlink(scratch_path("fail/jail"));
	/* pkg, a command, the save: the jail stopped too */
	world(1, 1);
	CHECK(plan("x", "generic", NULL) == 0);
	(void) scratch_write("fail/pkg", "");
	CHECK(run("m") == -1 && undone() && asked(stop));
	(void) unlink(scratch_path("fail/pkg"));
	world(1, 1);
	CHECK(plan("fails", "generic", NULL) == 0);
	CHECK(run("m") == -1);
	CHECK(!zfake_exists("fast/run/containers/fails"));
	CHECK(asked("jail -f "));
	CHECK(cap_has(&c, "create of fails failed"));
	world(1, 1);
	CHECK(plan("x", "generic", NULL) == 0);
	zfake_fail_nth("snapshot", 2);
	CHECK(run("m") == -1 && undone() && asked(stop));
	world(1, 1);
	CHECK(plan("x", "generic", NULL) == 0);
	CHECK(run("") == -1 && undone() && cap_has(&c, "empty message"));
	/* an external command failing: before the jail, all undone */
	world(1, 1);
	CHECK(plan("extfail", "generic", NULL) == 0);
	CHECK(run("m") == -1);
	CHECK(!zfake_exists("fast/run/containers/extfail"));
	CHECK(asked("external extfail") && !asked("jail -f"));
	CHECK(cap_has(&c, "create of extfail failed"));
	/* the mirror, after the save: x stands, saved, its jail running */
	world(1, 1);
	CHECK(plan("x", "generic", NULL) == 0);
	zfake_fail("send");
	CHECK(run("m") == -1);
	CHECK(zfake_exists(RX) && there(RX "@" S1));
	CHECK(cap_has(&c, "x@" S1 " is saved; its mirror into zlake failed"));
	CHECK(!cap_has(&c, "create of x failed"));
	CHECK(!asked(stop));
	(void) snprintf(path, sizeof (path), "%s/x.conf", jaildir);
	CHECK(access(path, F_OK) == 0);
	(void) unlink(path);
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
	test_undo();
	sh_create_free(&p);
	sh_spaces_free(&s);
	sh_zfs_close(z);
	free(ran);
	cap_close(&c);
	cap_close(&wc);
	scratch_clean();
	printf("check_create: %d checks\n", checks);
	return (0);
}
