/*
 * A foundation's roll, src/core/roll.c: family F of tests/MATRIX.md,
 * over the fake ZFS, with git real on the repositories
 * tests/src-fixture.sh makes in the scratch directory.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>

#include <time.h>

#include "check.h"
#include "core/builder.h"
#include "core/foundation.h"
#include "core/job.h"
#include "core/roll.h"
#include "core/shake.h"
#include "core/src.h"
#include "core/zfs.h"
#include "zfake.h"

#define	FROOT	"lake/l/foundations"
#define	OROOT	"lake/l/foundation-builds"

static struct cap c;
static struct sh_zfs *z;
static struct sh_builder b;
static struct sh_rollenv env;
static struct sh_foundation **fs;
static size_t nfs;
static char config[1100], build[1100];
static char *logbuf;
static size_t loglen;
static FILE *logf;
static char *statusbuf;

/* One event of the roll's kqueue handled, in 60 s at most. */
static void
one(void)
{
	struct timespec ts = { 60, 0 };
	struct kevent ev;

	CHECK(kevent(env.re_kq, NULL, 0, &ev, 1, &ts) == 1);
	sh_job_event(ev.udata, env.re_kq, &ev);
}

/* F's roll run to its end. */
static void
pump(struct sh_foundation *f)
{
	while (f->f_roll != NULL)
		one();
}

/* F's roll run until its child is STEP's. */
static void
pump_to(struct sh_foundation *f, const char *step)
{
	while (f->f_roll != NULL &&
	    strcmp(sh_roll_step(f->f_roll), step) != 0)
		one();
	CHECK(f->f_roll != NULL);
}

/*
 * A roll of NAME asked for, as shtetl roll NAME asks: in a second of
 * its own, since a version is named to the second and the rolls here
 * take less than one.
 */
static struct sh_foundation *
ask(const char *name)
{
	static time_t last;
	struct sh_foundation *f;

	while (time(NULL) == last)
		(void) usleep(50000);
	last = time(NULL);
	/* what make is asked, this roll's alone */
	(void) unlink(scratch_path("make.log"));
	f = sh_foundation_find(&fs, &nfs, FROOT, name);
	cap_reset(&c);
	CHECK(sh_roll_ask(f, &env, config, build, &c.c_err) == 0);
	return (f);
}

/* NAME rolled to its end. */
static struct sh_foundation *
roll(const char *name)
{
	struct sh_foundation *f = ask(name);

	pump(f);
	return (f);
}

/* What status says of the foundations. */
static const char *
status(void)
{
	size_t len = 0, i;
	FILE *f;

	free(statusbuf);
	statusbuf = NULL;
	CHECK((f = open_memstream(&statusbuf, &len)) != NULL);
	for (i = 0; i < nfs; i++)
		sh_foundation_status(fs[i], f);
	(void) fclose(f);
	return (statusbuf);
}

/* The user property PROP of NAME, or "". */
static const char *
user(const char *name, const char *prop)
{
	static char val[256];

	if (sh_zfs_user(z, name, prop, val, sizeof (val), &c.c_err) != 0)
		return ("");
	return (val);
}

/* NAME as a dataset of the foundations: FROOT/NAME. */
static const char *
ds(const char *name)
{
	static char buf[4][256];
	static int i;

	i = (i + 1) % 4;
	(void) snprintf(buf[i], sizeof (buf[i]), "%s/%s", FROOT, name);
	return (buf[i]);
}

/* CMD run in the scratch directory: whether it exited 0. */
static int
sh(const char *cmd)
{
	char buf[4096];

	(void) snprintf(buf, sizeof (buf), "cd '%s' && %s", scratch_dir,
	    cmd);
	return (system(buf) == 0);
}

/* The daemon stopped and started again over the same pools. */
static void
restart(void)
{
	size_t i;

	for (i = 0; i < nfs; i++)
		sh_roll_stop(fs[i]);
	sh_foundations_free(fs, nfs);
	sh_builder_init(&b);
	cap_reset(&c);
	CHECK(sh_foundations_start(z, FROOT, &fs, &nfs, &c.c_err) == 0);
}

static int
has(const char *s)
{
	return (cap_has(&c, s));
}

/* A is there and B. */
static int
is(const char *a, const char *b)
{
	return (a != NULL && strcmp(a, b) == 0);
}

/* The scratch file NAME whole, until the next call; NULL if none. */
static const char *
slurp(const char *name)
{
	static char *text;
	size_t n = 0;
	FILE *in;

	free(text);
	text = NULL;
	if ((in = fopen(scratch_path(name), "r")) == NULL)
		return (NULL);
	if (getdelim(&text, &n, '\0', in) < 0) {
		free(text);
		text = NULL;
	}
	(void) fclose(in);
	return (text);
}

static int
there(const char *name)
{
	struct stat st;

	return (lstat(scratch_path(name), &st) == 0);
}

static ino_t
ino(const char *name)
{
	struct stat st;

	return (lstat(scratch_path(name), &st) == 0 ? st.st_ino : 0);
}

int
main(void)
{
	static const struct sh_zprop mp = { "mountpoint", NULL };
	static const struct sh_zprop off = { "canmount", "off" };
	struct sh_foundation *f, *g;
	struct sh_zprop lake;
	char want[4096], cwd[1024], oid[72], snap[512], first[256];
	char src[1100], obj[1100], head[1100], all[3500];
	struct utsname u;
	struct stat st;
	const char *text;
	ino_t kept;

	cap_open(&c);
	(void) scratch_path("");
	(void) snprintf(config, sizeof (config), "%s", scratch_path("c1"));
	(void) snprintf(build, sizeof (build), "%s", scratch_path("build"));
	CHECK(setenv("HOME", scratch_dir, 1) == 0);
	CHECK(setenv("GIT_CONFIG_NOSYSTEM", "1", 1) == 0);
	CHECK(setenv("GIT_AUTHOR_NAME", "t", 1) == 0);
	CHECK(setenv("GIT_AUTHOR_EMAIL", "t@t", 1) == 0);
	CHECK(setenv("GIT_COMMITTER_NAME", "t", 1) == 0);
	CHECK(setenv("GIT_COMMITTER_EMAIL", "t@t", 1) == 0);
	CHECK(sh("git config --global init.defaultBranch main"));
	(void) snprintf(want, sizeof (want), "sh '%s/tests/src-fixture.sh' "
	    "'%s'", getcwd(cwd, sizeof (cwd)), scratch_dir);
	CHECK(system(want) == 0);
	/* releng/15.1 at B, which c5.patch conflicts with */
	CHECK(sh("git -C up branch -f releng/15.1 main"));
	/* make is tests/bin's, which logs its words */
	(void) snprintf(want, sizeof (want), "%s/tests/bin:%s", cwd,
	    getenv("PATH"));
	CHECK(setenv("PATH", want, 1) == 0);
	CHECK(setenv("FAKE_MAKE_LOG", scratch_path("make.log"), 1) == 0);
	(void) snprintf(src, sizeof (src), "%s", scratch_path("build/src"));
	(void) snprintf(obj, sizeof (obj), "%s",
	    scratch_path("lake/foundation-builds/r1"));
	(void) snprintf(head, sizeof (head), "%s",
	    scratch_path("lake/foundations/r1"));

	/* the lake: its root mounted in the scratch directory */
	zfake_reset();
	zfake_pool("lake");
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	lake = mp;
	lake.zp_value = scratch_path("lake");
	CHECK(sh_zfs_create(z, "lake/l", &lake, 1, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, FROOT, &off, 1, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, OROOT, &off, 1, &c.c_err) == 0);
	CHECK((logf = open_memstream(&logbuf, &loglen)) != NULL);
	sh_builder_init(&b);
	env.re_z = z;
	env.re_builder = &b;
	env.re_obj = OROOT;
	CHECK((env.re_kq = kqueue()) >= 0);
	env.re_log = logf;
	env.re_err = &c.c_err;
	env.re_flags = UF_NODUMP;
	CHECK(sh_foundations_start(z, FROOT, &fs, &nfs, &c.c_err) == 0);

	/* F1, F14: a first roll, its status while its first child runs */
	f = ask("r1");
	CHECK(f->f_state == SH_F_ROLLING);
	(void) snprintf(want, sizeof (want), "foundation r1 rolling %s\n",
	    sh_roll_step(f->f_roll));
	CHECK(strcmp(status(), want) == 0 && strchr(want, ':') != NULL);
	pump(f);
	CHECK(f->f_state == SH_F_IDLE && f->f_newest != NULL && !has("r1"));
	CHECK(strncmp(f->f_newest, "15.1-releng-", 12) == 0 &&
	    strcmp(f->f_newest + strlen(f->f_newest) - 7, "-create") == 0);
	(void) snprintf(first, sizeof (first), "%s", f->f_newest);
	CHECK(is(zfake_local(ds("r1"), "compression"), "zstd-9") &&
	    is(zfake_local(ds("r1"), "atime"), "off"));
	CHECK(is(zfake_local(ds("r1/usr"), "canmount"), "off"));
	CHECK(is(zfake_local(ds("r1/var"), "atime"), "on"));
	CHECK(sh_src_commit(scratch_path("build/src"), oid, sizeof (oid),
	    &c.c_err) == 0);
	(void) snprintf(snap, sizeof (snap), "%s@%s", ds("r1/usr/local"),
	    f->f_newest);
	CHECK(strcmp(user(snap, "shtetl:kind"), "create") == 0 &&
	    strcmp(user(snap, "shtetl:commit"), oid) == 0);
	(void) snprintf(snap, sizeof (snap), "%s@%s", ds("r1/usr"),
	    f->f_newest);
	CHECK(strcmp(user(snap, "shtetl:kind"), "create") == 0);
	CHECK(sh_zfs_mounted(z, ds("r1"), want, sizeof (want), &c.c_err) ==
	    0 && sh_zfs_mounted(z, ds("r1/var"), want, sizeof (want),
	    &c.c_err) == 0);
	(void) snprintf(want, sizeof (want), "# src releng/15.1 at %s, from "
	    "file://%s/up\n", oid, scratch_dir);
	{
		char *text = NULL;
		size_t n = 0;
		FILE *in;

		CHECK((in = fopen(scratch_path("lake/foundations/r1/"
		    SH_SHAKEN_FOUNDATION), "r")) != NULL);
		CHECK(getdelim(&text, &n, '\0', in) > 0);
		(void) fclose(in);
		CHECK(strncmp(text, want, strlen(want)) == 0 &&
		    strstr(text, "src_branch = \"releng/15.1\";") != NULL);
		free(text);
	}
	CHECK(stat(scratch_path("lake/foundations/r1/" SH_SHAKEN_FOUNDATION),
	    &st) == 0 && (st.st_flags & UF_NODUMP) != 0);
	CHECK(b.b_busy == 0);

	/* K9, K10, K14: the build, a first roll's install */
	(void) snprintf(all, sizeof (all), " SRCCONF=%s/shtetl/src.conf "
	    "KERNCONFDIR=%s/shtetl KERNCONF=R1\n", obj, obj);
	(void) snprintf(want, sizeof (want),
	    "-C %s -j3 buildworld buildkernel%s"
	    "-C %s installworld DESTDIR=%s INSTALL_CMD=install -C%s"
	    "-C %s installkernel DESTDIR=%s/kernel-install%s"
	    "-C %s distribution DESTDIR=%s%s",
	    src, all, src, head, all, src, obj, all, src, head, all);
	CHECK(is(slurp("make.log"), want));
	CHECK(is(slurp("lake/foundation-builds/r1/buildworld"),
	    "buildworld\n"));
	CHECK(is(slurp("lake/foundations/r1/boot/kernel/kernel"),
	    "kernel 1\n") && is(slurp("lake/foundations/r1/etc/rc.conf"),
	    "dist\n") && there("lake/foundations/r1/usr/lib/debug/boot/"
	    "kernel/kernel.debug") && !there("lake/foundations/r1/sbin/"
	    "init.bak"));
	/* K12: the kernel config and src.conf */
	CHECK(uname(&u) == 0);
	(void) snprintf(want, sizeof (want), "# written by shtetl for "
	    "foundation r1\ninclude \"%s/sys/%s/conf/GENERIC\"\nident R1\n"
	    "device vmm\n", src, u.machine);
	CHECK(is(slurp("lake/foundation-builds/r1/shtetl/R1"), want));
	CHECK(is(slurp("lake/foundation-builds/r1/shtetl/src.conf"),
	    "WITHOUT_LIB32=yes\n"));
	/* K17: the obj cache made, mounted; the scratch directory gone */
	CHECK(is(zfake_local(OROOT "/r1", "compression"), "zstd-9") &&
	    sh_zfs_mounted(z, OROOT "/r1", want, sizeof (want), &c.c_err) ==
	    1 && !there("lake/foundation-builds/r1/kernel-install"));

	/* F2, K11, K18: an update, its install, its kernel synced */
	kept = ino("lake/foundations/r1/boot/kernel/a.ko");
	CHECK(setenv("FAKE_KERNEL", "2", 1) == 0);
	f = roll("r1");
	CHECK(unsetenv("FAKE_KERNEL") == 0);
	CHECK(f->f_state == SH_F_IDLE && strcmp(f->f_newest, first) != 0 &&
	    strcmp(f->f_newest + strlen(f->f_newest) - 7, "-update") == 0);
	(void) snprintf(snap, sizeof (snap), "%s@%s", ds("r1"), f->f_newest);
	CHECK(strcmp(user(snap, "shtetl:kind"), "update") == 0);
	(void) snprintf(want, sizeof (want),
	    "-C %s -j3 buildworld buildkernel%s"
	    "-C %s delete-old delete-old-libs DESTDIR=%s "
	    "BATCH_DELETE_OLD_FILES=yes%s"
	    "-C %s installworld DESTDIR=%s INSTALL_CMD=install -C%s",
	    src, all, src, head, all, src, head, all);
	CHECK(strncmp(slurp("make.log"), want, strlen(want)) == 0);
	CHECK(!there("lake/foundations/r1/sbin/init.bak") &&
	    !there("lake/foundations/r1/libexec/ld-elf.so.1.old") &&
	    !there("lake/foundations/r1/boot/loader.old") &&
	    there("lake/foundations/r1/boot/loader"));
	CHECK(is(slurp("lake/foundations/r1/boot/kernel/kernel"),
	    "kernel 2\n") && ino("lake/foundations/r1/boot/kernel/a.ko") ==
	    kept && !there("lake/foundations/r1/boot/kernel/b.ko") &&
	    there("lake/foundations/r1/boot/kernel/c.ko") &&
	    !there("lake/foundations/r1/usr/lib/debug/boot/kernel"));

	/* F5: an update failing at a patch, after a word was written */
	CHECK(sh("printf '%s\\n' 'src_branch = \"releng/15.1\";' "
	    "'datasets { \"usr/local\" {} var { atime = true; } }' "
	    "'patches { \"patches/conflict\" {} }' "
	    "> c1/foundations/r1.conf"));
	f = ask("r1");
	pump_to(f, "src: fetch");
	CHECK(zfake_put(ds("r1"), "junk") == 0);
	pump(f);
	CHECK(has("foundation r1: patches/conflict: c5.patch: exited 1"));
	CHECK(f->f_state == SH_F_STALLED &&
	    is(zfake_local(ds("r1"), "shtetl:stall"),
	    "patches/conflict: c5.patch: exited 1"));
	CHECK(strcmp(status(), "foundation r1 stalled patches/conflict: "
	    "c5.patch: exited 1\n") == 0);
	CHECK(sh_zfs_mount(z, ds("r1"), &c.c_err) == 0 &&
	    zfake_has(ds("r1"), "junk") == 0 &&
	    sh_zfs_unmount(z, ds("r1"), &c.c_err) == 0);
	CHECK(sh("test -z \"$(git -C build/src status --porcelain "
	    "--ignored)\""));
	CHECK(b.b_busy == 0);

	/* F4, F3: the stall cleared by the ask; asked again, refused */
	CHECK(sh("printf '%s\\n' 'src_branch = \"releng/15.1\";' "
	    "'datasets { \"usr/local\" {} var { atime = true; } }' "
	    "> c1/foundations/r1.conf"));
	f = ask("r1");
	CHECK(zfake_local(ds("r1"), "shtetl:stall") == NULL &&
	    f->f_stall == NULL);
	CHECK(sh_roll_ask(f, &env, config, build, &c.c_err) == -1 &&
	    has("r1: rolling"));
	pump(f);
	CHECK(f->f_state == SH_F_IDLE);

	/* F12: the snapshot refused */
	zfake_fail("snapshot");
	f = roll("r1");
	CHECK(has("foundation r1: snapshot: zfake: snapshot failed as asked"));
	CHECK(f->f_state == SH_F_STALLED &&
	    strcmp(f->f_stall, "snapshot: zfake: snapshot failed as asked") ==
	    0);
	f = roll("r1");
	CHECK(f->f_state == SH_F_IDLE);

	/* K15: a make failing: its last lines told; rolled back, stalled */
	CHECK(setenv("FAKE_FAIL", "installworld", 1) == 0);
	f = roll("r1");
	CHECK(unsetenv("FAKE_FAIL") == 0);
	CHECK(has("foundation r1: installworld: exited 2") &&
	    has("r1: make: installworld failed"));
	CHECK(f->f_state == SH_F_STALLED &&
	    is(f->f_stall, "installworld: exited 2"));
	f = roll("r1");
	CHECK(f->f_state == SH_F_IDLE);

	/* K13: kernconf a path, a file of zconfig in the recipe's place */
	f = roll("r6");
	(void) snprintf(want, sizeof (want), "include \"%s/site/kern/MINE\"\n",
	    config);
	CHECK(f->f_state == SH_F_IDLE &&
	    (text = slurp("lake/foundation-builds/r6/shtetl/R6")) != NULL &&
	    strstr(text, want) != NULL);

	/* F6: a first roll failing: nothing left of it */
	f = roll("r2");
	CHECK(has("foundation r2: patches/conflict: c5.patch: exited 1"));
	CHECK(f->f_state == SH_F_IDLE && f->f_newest == NULL &&
	    f->f_stall == NULL);
	CHECK(sh_zfs_mounted(z, ds("r2"), want, sizeof (want), &c.c_err) ==
	    -1);
	CHECK(strstr(status(), "r2") == NULL);

	/* F7, F8, F9: ver before and after the checkout; no REVISION */
	f = roll("r3");
	CHECK(has("foundation r3: recipe: foundations/r3.conf: ver: "
	    "releng/15.1 is not >=16"));
	CHECK(sh_zfs_mounted(z, ds("r3"), want, sizeof (want), &c.c_err) ==
	    -1);
	f = roll("r4");
	CHECK(has("foundation r4: version: foundations/r4.conf: ver: 15.1 is "
	    "not >=16"));
	f = roll("r5");
	CHECK(has("foundation r5: version: sys/conf/newvers.sh: No such file "
	    "or directory"));

	/* F15: a recipe that is not there */
	f = roll("nope");
	CHECK(has("foundation nope: recipe: "));
	CHECK(sh_zfs_mounted(z, ds("nope"), want, sizeof (want), &c.c_err) ==
	    -1);

	/* F13: a second asked while the first rolls: it waits its turn */
	f = ask("r1");
	g = ask("f1");
	CHECK(g->f_state == SH_F_WAITING &&
	    strstr(status(), "foundation f1 waiting\n") != NULL);
	pump(f);
	CHECK(g->f_state == SH_F_ROLLING);
	pump(g);
	CHECK(g->f_state == SH_F_IDLE && g->f_newest != NULL);

	/* F10: stopped in a child of an update; started again */
	f = ask("r1");
	(void) snprintf(first, sizeof (first), "%s", f->f_newest);
	pump_to(f, "src: fetch");
	CHECK(zfake_put(ds("r1"), "junk") == 0);
	restart();
	f = sh_foundation_find(&fs, &nfs, FROOT, "r1");
	CHECK(f->f_state == SH_F_IDLE && strcmp(f->f_newest, first) == 0);
	CHECK(sh_zfs_mount(z, ds("r1"), &c.c_err) == 0 &&
	    zfake_has(ds("r1"), "junk") == 0 &&
	    sh_zfs_unmount(z, ds("r1"), &c.c_err) == 0);
	f = roll("r1");
	CHECK(f->f_state == SH_F_IDLE && strcmp(f->f_newest, first) != 0);

	/* K16: stopped in the build; started again */
	CHECK(setenv("FAKE_SLEEP", "buildworld", 1) == 0);
	f = ask("r1");
	(void) snprintf(first, sizeof (first), "%s", f->f_newest);
	pump_to(f, "build");
	CHECK(zfake_put(ds("r1"), "junk") == 0);
	restart();
	CHECK(unsetenv("FAKE_SLEEP") == 0);
	f = sh_foundation_find(&fs, &nfs, FROOT, "r1");
	CHECK(f->f_state == SH_F_IDLE && strcmp(f->f_newest, first) == 0);
	CHECK(sh_zfs_mount(z, ds("r1"), &c.c_err) == 0 &&
	    zfake_has(ds("r1"), "junk") == 0 &&
	    sh_zfs_unmount(z, ds("r1"), &c.c_err) == 0);

	/* F11: stopped in a child of a first roll; started again */
	f = ask("f6");
	pump_to(f, "src: fetch");
	restart();
	CHECK(has("foundation f6: a first build undone"));
	CHECK(strstr(status(), "f6") == NULL);

	restart();
	sh_foundations_free(fs, nfs);
	(void) close(env.re_kq);
	sh_zfs_close(z);
	(void) fclose(logf);
	free(logbuf);
	free(statusbuf);
	cap_close(&c);
	scratch_clean();
	return (0);
}
