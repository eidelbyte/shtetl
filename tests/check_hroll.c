/*
 * Family W of tests/MATRIX.md: a container's roll onto the next version
 * of its foundation the run holds (src/core/house.h, sh_house_roll),
 * over the fake ZFS, the fake zfs_rebase (tests/rbfake.h), and the
 * chroot's programs and jail(8) tests/bin's, which log their words. The
 * house is made as tests/check_house.c makes one, the beam's sends the
 * test's. The fake keeps no files in a clone or a receive, so what the
 * roll reads is laid where it reads it: the next version's
 * foundation.conf in the copy's snapshot view, and the tree the replay
 * makes where the new version mounts at its own path.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/stat.h>

#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "check.h"
#include "core/house.h"
#include "core/job.h"
#include "core/rebase.h"
#include "core/spaces.h"
#include "core/zfs.h"
#include "rbfake.h"
#include "zfake.h"

#define	LAKE	"tank/lake"
#define	RUN	"fast/run"
#define	LF	LAKE "/foundations"
#define	RF	RUN "/foundations"
#define	RC	RUN "/containers"
#define	V	"15.1-releng-T-create"
#define	V2	"15.1-releng-U-create"
#define	OLD	RC "/15.1-releng-T-web"
#define	NEW	RC "/15.1-releng-U-web"

/* A foundation version's shaken recipe, on the branch B. */
#define	FCONF(b)							\
	"src_branch = \"" b "\";\n"					\
	"pkg { tmux = true; }\n"					\
	"datasets { \"usr/local\" {} }\n"				\
	"commands { base = \"echo base\"; }\n"

#define	WEB								\
	"ver = \"<16\";\npkg = [ \"nginx\" ];\n"			\
	"datasets { www { compression = \"lz4\"; } \"srv/db\" {} }\n"	\
	"commands { site = \"echo web\"; }\n"				\
	"jailconf = \"allow.raw_sockets;\";\n"

static struct cap c;
static struct cap sys;		/* the houses' error sink, syslog's */
static struct sh_zfs *z;
static struct sh_houseenv env;
static struct sh_house **hs;
static size_t nh;
static char config[1024], conts[1024], copies[1024], logdir[1024];
static char resolv[1024], jails[1024], store[1024];

/* How often the beam was told a version or a snapshot was made. */
static int beamed;

static void
told(void *arg)
{
	(void) arg;
	beamed++;
}

/* The pull a create asks, for the test to deliver. */
static struct {
	void	(*done)(void *, const char *, const char *);
	void	*arg;
} pull;

static void
asked(void *arg, const char *snap,
    void (*done)(void *, const char *, const char *), void *darg)
{
	(void) arg;
	(void) snap;
	pull.done = done;
	pull.arg = darg;
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
is(const char *a, const char *b)
{
	return (a != NULL && strcmp(a, b) == 0);
}

/* Whether TEXT ends with END. */
static int
ends(const char *text, const char *end)
{
	return (text != NULL && strlen(text) >= strlen(end) &&
	    strcmp(text + strlen(text) - strlen(end), end) == 0);
}

/* Whether TEXT holds each of the NULL-ended LINES, in their order. */
static int
in_order(const char *text, const char *const *lines)
{
	const char *at = text;

	for (; at != NULL && *lines != NULL; lines++) {
		if ((at = strstr(at, *lines)) != NULL)
			at += strlen(*lines);
	}
	return (at != NULL);
}

/* Names, as a walk finds them. */
struct names {
	char	*v[64];
	size_t	n;
};

static int
name_add(void *arg, const char *name)
{
	struct names *n = arg;

	CHECK(n->n < 64);
	n->v[n->n++] = strdup(name);
	return (0);
}

static void
names_free(struct names *n)
{
	while (n->n > 0)
		free(n->v[--n->n]);
}

static void
tree(const char *root, struct names *n)
{
	size_t i;

	n->n = 0;
	(void) name_add(n, root);
	for (i = 0; i < n->n; i++)
		CHECK(sh_zfs_children(z, n->v[i], name_add, n, &c.c_err) == 0);
}

/* TOP's tree snapshotted at once as SNAP, shtetl:kind KIND. */
static void
take(const char *top, const char *snap, const char *kind)
{
	struct sh_zprop p = { "shtetl:kind", kind };
	char buf[64][256];
	const char *v[64];
	struct names t;
	size_t i;

	tree(top, &t);
	for (i = 0; i < t.n; i++) {
		(void) snprintf(buf[i], sizeof (buf[i]), "%s@%s", t.v[i], snap);
		v[i] = buf[i];
	}
	CHECK(sh_zfs_snapshot(z, v, t.n, &p, 1, &c.c_err) == 0);
	names_free(&t);
}

static int
there(const char *name)
{
	uint64_t g, x;
	struct names t;
	int r;

	if (strchr(name, '@') != NULL) {
		r = sh_zfs_snapinfo(z, name, &g, &x, &c.c_err) == 0;
	} else {
		t.n = 0;
		r = sh_zfs_children(z, name, name_add, &t, &c.c_err) == 0;
		names_free(&t);
	}
	cap_reset(&c);
	return (r);
}

/*
 * g's tree at VER sent to the run as the beam sends it: incremental
 * from PREV where the copy holds a dataset at PREV, whole where it holds
 * none; the copy's top told its kind.
 */
static void
beam(const char *ver, const char *prev)
{
	char from[300], base[300], to[300], had[300];
	const char *rel;
	struct names t;
	size_t i;
	int incr;

	tree(LF "/g", &t);
	for (i = 0; i < t.n; i++) {
		rel = t.v[i] + strlen(LF);
		(void) snprintf(from, sizeof (from), "%s@%s", t.v[i], ver);
		(void) snprintf(to, sizeof (to), RF "%s@%s", rel, ver);
		(void) snprintf(base, sizeof (base), "%s@%s", t.v[i],
		    prev != NULL ? prev : "");
		(void) snprintf(had, sizeof (had), RF "%s@%s", rel,
		    prev != NULL ? prev : "");
		incr = prev != NULL && there(had);
		CHECK(sh_zfs_send(z, from, incr ? base : NULL, to, NULL,
		    &c.c_err) == 0);
	}
	names_free(&t);
	(void) snprintf(to, sizeof (to), RF "/g@%s", ver);
	CHECK(sh_zfs_set(z, to, "shtetl:kind", "create", &c.c_err) == 0);
}

/* One event of the kqueue handed on, within MS: 1, or 0 for none. */
static int
one(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
	struct kevent ev;

	if (kevent(env.he_kq, NULL, 0, &ev, 1, &ts) != 1)
		return (0);
	sh_job_event(ev.udata, env.he_kq, &ev);
	return (1);
}

static const char *
status(void)
{
	static char *buf;
	size_t i, len = 0;
	FILE *f;

	free(buf);
	buf = NULL;
	CHECK((f = open_memstream(&buf, &len)) != NULL);
	for (i = 0; i < nh; i++)
		sh_house_status(hs[i], f);
	(void) fclose(f);
	return (buf != NULL ? buf : "");
}

/* Every act run to its end: the houses' status. */
static const char *
settle(void)
{
	while (strstr(status(), " creating ") != NULL ||
	    strstr(status(), " starting ") != NULL ||
	    strstr(status(), " upgrading ") != NULL ||
	    strstr(status(), " rolling ") != NULL)
		CHECK(one(20000));
	return (status());
}

/* The house NAME. */
static struct sh_house *
house(const char *name)
{
	size_t i;

	for (i = 0; i < nh && strcmp(hs[i]->h_name, name) != 0; i++)
		;
	CHECK(i < nh);
	return (hs[i]);
}

/* The houses started again from ZFS, as the daemon's start does. */
static void
restart(void)
{
	sh_houses_free(hs, nh);
	cap_reset(&c);
	CHECK(sh_houses_start(&env, &hs, &nh, &c.c_err) == 0);
}

static const char *
native(const char *ds, const char *prop)
{
	static char v[1024];

	if (sh_zfs_native(z, ds, prop, v, sizeof (v), &c.c_err) != 0)
		return ("(refused)");
	return (v);
}

static const char *
origin(const char *ds)
{
	static char buf[300];

	buf[0] = '\0';
	CHECK(sh_zfs_origin(z, ds, buf, sizeof (buf), &c.c_err) >= 0);
	return (buf);
}

static const char *
user(const char *name, const char *prop)
{
	static char v[256];

	if (sh_zfs_user(z, name, prop, v, sizeof (v), &c.c_err) != 0)
		return ("(unset)");
	return (v);
}

/* Where DS is mounted: its path below the containers', or "". */
static const char *
where(const char *ds)
{
	static char p[1024];

	if (sh_zfs_mounted(z, ds, p, sizeof (p), &c.c_err) != 1)
		return ("");
	if (strncmp(p, conts, strlen(conts)) != 0)
		return (p);
	return (p + strlen(conts));
}

/* The names after the @ of DS's snapshots, oldest first, each ended. */
static const char *
snaps(const char *ds)
{
	static char buf[1024];
	struct names s;
	size_t i;

	s.n = 0;
	buf[0] = '\0';
	CHECK(sh_zfs_snapshots(z, ds, name_add, &s, &c.c_err) == 0);
	for (i = 0; i < s.n; i++) {
		(void) strlcat(buf, s.v[i], sizeof (buf));
		(void) strlcat(buf, " ", sizeof (buf));
	}
	names_free(&s);
	return (buf);
}

/* The name after the @ of DS's Kth snapshot, oldest first, into BUF. */
static void
snap_at(const char *ds, size_t k, char *buf, size_t len)
{
	struct names s;

	s.n = 0;
	CHECK(sh_zfs_snapshots(z, ds, name_add, &s, &c.c_err) == 0 &&
	    s.n > k);
	(void) snprintf(buf, len, "%s", s.v[k]);
	names_free(&s);
}

/* A file under the scratch directory, laid anew with TEXT. */
static void
relay(const char *rel, const char *text)
{
	(void) unlink(scratch_path(rel));
	(void) scratch_write(rel, text);
}

/*
 * What the version VN's tree holds, laid where it mounts at its own
 * path: foundation.conf on the branch B, and the etc a chroot lends
 * resolv.conf to.
 */
static void
lay(const char *vn, const char *b)
{
	char rel[512];

	(void) snprintf(rel, sizeof (rel), "containers/%s/var/db/shtetl/"
	    "foundation.conf", vn);
	if (strcmp(b, "releng/15.1") == 0)
		relay(rel, FCONF("releng/15.1"));
	else
		relay(rel, FCONF("releng/16.0"));
	(void) snprintf(rel, sizeof (rel), "containers/%s/etc/motd", vn);
	relay(rel, "\n");
}

/*
 * The lake and the run, their skeleton, the run's containers and
 * copies below the scratch directory, emptied with the jails' blocks
 * and what tests/bin/jail keeps; g on the lake at V, its usr a
 * placeholder, var a dataset of its own; web's recipe as made; no
 * shtetl.conf; the fakes' failures and the programs' log anew.
 */
static void
fresh(void)
{
	static const struct sh_zprop lake = { "shtetl:space", "lake" };
	static const struct sh_zprop run = { "shtetl:space", "run" };
	static const struct sh_zprop off = { "canmount", "off" };
	struct sh_spaces sp;
	char cmd[6000];

	sh_houses_free(hs, nh);
	hs = NULL;
	nh = 0;
	(void) snprintf(cmd, sizeof (cmd), "rm -rf '%s' '%s' '%s' '%s' '%s'",
	    conts, copies, jails, store, scratch_path("fake.log.jails"));
	CHECK(system(cmd) == 0 && mkdir(jails, 0755) == 0);
	zfake_reset();
	rbfake_reset();
	(void) unsetenv("FAKE_FAIL");
	zfake_pool("tank");
	zfake_pool("fast");
	cap_reset(&c);
	cap_reset(&sys);
	CHECK(sh_zfs_create(z, LAKE, &lake, 1, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, RUN, &run, 1, &c.c_err) == 0);
	CHECK(sh_spaces_find(z, &sp, &c.c_err) == 0);
	CHECK(sh_spaces_skeleton(z, &sp, &c.c_err) == 0);
	sh_spaces_free(&sp);
	CHECK(sh_zfs_set(z, RC, "mountpoint", conts, &c.c_err) == 0);
	CHECK(sh_zfs_set(z, RF, "mountpoint", copies, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, LF "/g", NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, LF "/g/usr", &off, 1, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, LF "/g/usr/local", NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, LF "/g/var", NULL, 0, &c.c_err) == 0);
	take(LF "/g", V, "create");
	relay("config/containers/web.conf", WEB);
	(void) unlink(scratch_path("config/shtetl.conf"));
	(void) unlink(scratch_path("fake.log"));
	memset(&pull, 0, sizeof (pull));
	beamed = 0;
}

/* web made on g at V, the run holding none of it: active. */
static void
made(void)
{
	fresh();
	CHECK(sh_house_create(&hs, &nh, "web", LF "/g", V, &env, config,
	    &c.c_err) == 0);
	lay("15.1-releng-T-web", "releng/15.1");
	beam(V, NULL);
	cap_reset(&c);
	pull.done(pull.arg, RF "/g@" V, NULL);
	CHECK(is(settle(), "house web active 15.1-releng-T-web\n"));
	CHECK(is(cap_text(&sys), ""));
}

/*
 * V2 made on the lake, a dataset new in it, opt, and sent to the run;
 * its foundation.conf laid in the copy's view of it; the tree the
 * replay makes laid with a foundation.conf of its own, which W6 holds
 * the roll never reads: its branch is one web's ver refuses.
 */
static void
next(void)
{
	CHECK(sh_zfs_create(z, LF "/g/opt", NULL, 0, &c.c_err) == 0);
	take(LF "/g", V2, "create");
	beam(V2, V);
	relay("copies/g/var/.zfs/snapshot/" V2 "/db/shtetl/foundation.conf",
	    FCONF("releng/15.1"));
	lay("15.1-releng-U-web", "releng/16.0");
	(void) unlink(scratch_path("fake.log"));
	cap_reset(&c);
	cap_reset(&sys);
	beamed = 0;
}

static int
roll(void)
{
	cap_reset(&c);
	return (sh_house_roll(&hs, &nh, house("web"), config, &c.c_err));
}

/*
 * W3: web made, saved by hand twice, s1 then s2, with shtetl.conf's
 * roll.depth DEPTH (NULL none), its create snapshot and s1 made AGE
 * seconds older; rolled: the new version's snapshots, the update's
 * left off.
 */
static const char *
depth(const char *depth, int64_t age)
{
	static char got[1024];
	char text[128], snap[300], name[128];

	made();
	if (depth != NULL) {
		(void) snprintf(text, sizeof (text), "roll { depth = %s; }\n",
		    depth);
		relay("config/shtetl.conf", text);
	}
	take(OLD, "s1", "save");
	take(OLD, "s2", "save");
	if (age > 0) {
		snap_at(OLD, 0, name, sizeof (name));
		(void) snprintf(snap, sizeof (snap), OLD "@%s", name);
		zfake_age(snap, age);
		zfake_age(OLD "@s1", age);
	}
	next();
	CHECK(roll() == 0);
	CHECK(is(settle(), "house web active 15.1-releng-U-web\n"));
	(void) snprintf(got, sizeof (got), "%s", snaps(NEW));
	CHECK(strlen(got) > 0);
	got[strlen(got) - 1] = '\0';
	*(strrchr(got, ' ') != NULL ? strrchr(got, ' ') : got) = '\0';
	return (got);
}

/*
 * Until the fake rebaser's record stands on DS, the kqueue handed on:
 * no more than 20 s, however many events come meanwhile.
 */
static void
until_open(const char *ds)
{
	time_t end = time(NULL) + 20;

	while (zfake_local(ds, SH_REBASE_TAG) == NULL && time(NULL) < end)
		(void) one(50);
	CHECK(zfake_local(ds, SH_REBASE_TAG) != NULL);
}

int
main(void)
{
	char path[1024], cwd[1024], want[4096], cr[128], fin[128], up[128];
	char l[8][1024];
	const char *v[10], *text;
	int i;

	cap_open(&c);
	cap_open(&sys);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	(void) snprintf(path, sizeof (path), "%s/tests/bin:%s",
	    getcwd(cwd, sizeof (cwd)), getenv("PATH"));
	CHECK(setenv("PATH", path, 1) == 0);
	CHECK(setenv("FAKE_LOG", scratch_path("fake.log"), 1) == 0);
	(void) scratch_write("resolv.conf", "nameserver 127.0.0.1\n");
	(void) snprintf(config, sizeof (config), "%s", scratch_path("config"));
	(void) snprintf(conts, sizeof (conts), "%s",
	    scratch_path("containers"));
	(void) snprintf(copies, sizeof (copies), "%s", scratch_path("copies"));
	(void) snprintf(jails, sizeof (jails), "%s", scratch_path("jails"));
	CHECK(mkdir(scratch_path("logs"), 0755) == 0);
	CHECK((env.he_kq = kqueue()) >= 0);
	env.he_z = z;
	env.he_lake = LAKE;
	env.he_run = RUN;
	(void) snprintf(resolv, sizeof (resolv), "%s",
	    scratch_path("resolv.conf"));
	(void) snprintf(logdir, sizeof (logdir), "%s", scratch_path("logs"));
	env.he_resolv = resolv;
	env.he_logdir = logdir;
	env.he_jails = jails;
	env.he_err = &sys.c_err;
	env.he_pull = asked;
	env.he_beam = told;
	(void) snprintf(store, sizeof (store), "%s", scratch_path("zfake"));
	zfake_files(store);

	/*
	 * W1, W5, W6: web rolled onto V2, its recipe having gained a piece
	 * and a package: "rolling jail" at once; the jail stopped; the
	 * final save; the replay per dataset, parent first, the clone form
	 * then the dataset form; the piece the old house.conf holds not run
	 * again, the new one run, its own packages installed, the
	 * foundation's not; the jail started last; the beam told once
	 */
	made();
	next();
	relay("config/containers/web.conf", "ver = \"<16\";\n"
	    "pkg = [ \"nginx\", \"curl\" ];\n"
	    "datasets { www { compression = \"lz4\"; } \"srv/db\" {} }\n"
	    "commands { site = \"echo web\"; more = \"echo more\"; }\n"
	    "jailconf = \"allow.raw_sockets;\";\n");
	CHECK(roll() == 0 && is(status(), "house web rolling jail\n"));
	CHECK(is(settle(), "house web active 15.1-releng-U-web\n"));
	CHECK(is(cap_text(&sys), "") && beamed == 1);
	snap_at(OLD, 0, cr, sizeof (cr));
	snap_at(OLD, 1, fin, sizeof (fin));
	(void) snprintf(l[0], sizeof (l[0]), "%s@%s", OLD, fin);
	CHECK(is(user(l[0], "shtetl:kind"), "save"));
	(void) snprintf(l[0], sizeof (l[0]), "jail -f %s/web.conf -r web\n",
	    jails);
	(void) snprintf(l[1], sizeof (l[1]), "zfs_rebase -m --from " OLD
	    "@%s --onto " RF "/g@" V2 " --result " NEW "\n", cr);
	(void) snprintf(l[2], sizeof (l[2]), "zfs_rebase -m -u -b " OLD
	    "@%s --from " OLD "@%s --onto " NEW " --result %s\n", cr, fin, cr);
	(void) snprintf(l[3], sizeof (l[3]), "zfs_rebase -m --from " OLD
	    "/usr/local@%s --onto " RF "/g/usr/local@" V2 " --result " NEW
	    "/usr/local\n", cr);
	(void) snprintf(l[4], sizeof (l[4]), "pkg -c %s/15.1-releng-U-web "
	    "install -y -- nginx curl (resolv.conf)\n", conts);
	(void) snprintf(l[5], sizeof (l[5]), "jail -f %s/web.conf -cm web\n",
	    jails);
	v[0] = "jls -j web\n";
	v[1] = l[0];
	v[2] = l[1];
	v[3] = l[2];
	v[4] = l[3];
	v[5] = l[4];
	v[6] = "-c echo more\n";
	v[7] = NULL;
	CHECK((text = slurp("fake.log")) != NULL && in_order(text, v));
	CHECK(ends(text, l[5]));
	CHECK(strstr(text, "echo web") == NULL &&
	    strstr(text, "echo base") == NULL &&
	    strstr(text, "tmux") == NULL && strstr(text, "--abort") == NULL);
	/*
	 * W2: the new version: named for V2 less its kind, then web; a
	 * clone of the copy at V2, each dataset of it, opt new in V2 too;
	 * web's own, www and srv/db, clones of the final save, their
	 * placeholder srv made again; shtetl:house; canmount noauto, the
	 * placeholders off; nothing of the clone form's left set; its
	 * snapshots the replayed saves' but the last, then the update,
	 * kind update
	 */
	CHECK(is(origin(NEW), RF "/g@" V2) &&
	    is(origin(NEW "/usr"), RF "/g/usr@" V2) &&
	    is(origin(NEW "/usr/local"), RF "/g/usr/local@" V2) &&
	    is(origin(NEW "/var"), RF "/g/var@" V2) &&
	    is(origin(NEW "/opt"), RF "/g/opt@" V2));
	(void) snprintf(l[0], sizeof (l[0]), "%s/www@%s", OLD, fin);
	CHECK(is(origin(NEW "/www"), l[0]));
	(void) snprintf(l[0], sizeof (l[0]), "%s/srv/db@%s", OLD, fin);
	CHECK(is(origin(NEW "/srv/db"), l[0]) && is(origin(NEW "/srv"), "") &&
	    is(native(NEW "/srv", "canmount"), "off") &&
	    is(where(NEW "/srv/db"), "/web/srv/db"));
	CHECK(is(user(NEW, "shtetl:house"), "web"));
	CHECK(is(native(NEW, "canmount"), "noauto") &&
	    is(native(NEW "/usr", "canmount"), "off") &&
	    is(native(NEW "/usr/local", "canmount"), "noauto") &&
	    is(native(NEW "/opt", "canmount"), "noauto") &&
	    is(native(NEW "/www", "canmount"), "noauto"));
	CHECK(zfake_local(NEW, "readonly") == NULL &&
	    zfake_local(NEW, "mountpoint") != NULL &&
	    zfake_local(NEW "/var", "mountpoint") == NULL &&
	    zfake_local(NEW, SH_REBASE_TAG) == NULL &&
	    zfake_local(NEW "/var", SH_REBASE_TAG) == NULL);
	snap_at(NEW, 1, up, sizeof (up));
	(void) snprintf(want, sizeof (want), "%s %s ", cr, up);
	CHECK(is(snaps(NEW), want) && is(snaps(NEW "/var"), want));
	(void) snprintf(want, sizeof (want), "%s ", up);
	CHECK(is(snaps(NEW "/usr"), want) && is(snaps(NEW "/opt"), want) &&
	    is(snaps(NEW "/www"), want) && is(snaps(NEW "/srv"), want));
	(void) snprintf(l[0], sizeof (l[0]), "%s@%s", NEW, up);
	CHECK(is(user(l[0], "shtetl:kind"), "update"));
	/* W1: the swap: the new at /web, the old back at its own path */
	(void) snprintf(want, sizeof (want), "%s/15.1-releng-T-web", conts);
	CHECK(is(where(NEW), "/web") && is(where(NEW "/www"), "/web/www") &&
	    is(where(NEW "/opt"), "/web/opt") && is(where(OLD), "") &&
	    is(native(OLD, "mountpoint"), want));
	/* the copy unmounted; house.conf the new recipe, its block read */
	CHECK(is(where(RF "/g"), "") && is(where(RF "/g/var"), ""));
	CHECK((text = slurp("containers/web/var/db/shtetl/house.conf")) !=
	    NULL && strstr(text, "curl") != NULL &&
	    strstr(text, "echo more") != NULL);
	(void) snprintf(want, sizeof (want), "\tpath = \"%s/web\";\n", conts);
	CHECK((text = slurp("jails/web.conf")) != NULL &&
	    strstr(text, want) != NULL);

	/*
	 * W13: started again, two complete versions: the newest active,
	 * the other left unmounted at its own path
	 */
	restart();
	CHECK(is(settle(), "house web active 15.1-releng-U-web\n") &&
	    is(where(OLD), "") && is(where(NEW), "/web"));
	/*
	 * W13, H14: a swap a crash cut short, the old version placed again
	 * and the new at its own path, both unmounted: the newest placed
	 * and started, the other given back its own path
	 */
	sh_houses_free(hs, nh);
	hs = NULL;
	nh = 0;
	{
		struct names t;
		size_t k;

		tree(NEW, &t);
		for (k = t.n; k > 0; k--)
			CHECK(sh_zfs_unmount(z, t.v[k - 1], &c.c_err) == 0);
		names_free(&t);
	}
	CHECK(sh_zfs_inherit(z, NEW, "mountpoint", &c.c_err) == 0);
	(void) snprintf(want, sizeof (want), "%s/web", conts);
	CHECK(sh_zfs_set(z, OLD, "mountpoint", want, &c.c_err) == 0);
	restart();
	CHECK(is(settle(), "house web active 15.1-releng-U-web\n") &&
	    is(where(NEW), "/web") && is(where(OLD), "") &&
	    zfake_local(OLD, "mountpoint") == NULL);
	CHECK(is(cap_text(&sys), ""));

	/*
	 * W3: roll.depth: every snapshot (0, the built-in); the newest
	 * three; those younger than a day
	 */
	text = depth(NULL, 0);
	snap_at(OLD, 0, cr, sizeof (cr));
	(void) snprintf(want, sizeof (want), "%s s1 s2", cr);
	CHECK(is(text, want));
	CHECK(is(depth("3", 0), "s1 s2"));
	CHECK(is(depth("\"1d\"", 2 * 86400), "s2"));

	/*
	 * W4: a dataset web took from g that V2 lacks, var: stalled before
	 * anything is made, said once; the copy unmounted, the jail started
	 * again; nothing rebased
	 */
	made();
	CHECK(sh_zfs_destroy(z, LF "/g/var@" V, &c.c_err) == 0 &&
	    sh_zfs_destroy(z, LF "/g/var", &c.c_err) == 0);
	next();
	CHECK(roll() == 0);
	CHECK(is(settle(), "house web stalled replay: var: not in " RF "/g@"
	    V2 "\n"));
	CHECK(is(cap_text(&sys), "house web: replay: var: not in " RF "/g@"
	    V2 "\n"));
	CHECK(is(user(OLD, "shtetl:stall"), "replay: var: not in " RF "/g@"
	    V2) && !there(NEW));
	CHECK(is(where(RF "/g"), "") && is(where(OLD), "/web"));
	(void) snprintf(want, sizeof (want), "jail -f %s/web.conf -cm web\n",
	    jails);
	CHECK((text = slurp("fake.log")) != NULL && ends(text, want) &&
	    strstr(text, "zfs_rebase") == NULL);

	/*
	 * W7: a conflict in the root's dataset form: its run aborted, the
	 * pre-apply snapshot rolled back to; the new version destroyed, the
	 * copy unmounted, the old version's jail started again; stalled,
	 * "replay: exited 1", said once
	 */
	made();
	next();
	snap_at(OLD, 0, cr, sizeof (cr));
	(void) snprintf(l[0], sizeof (l[0]), NEW "@%s", cr);
	rbfake_end(l[0], 1);
	CHECK(roll() == 0);
	CHECK(is(settle(), "house web stalled replay: exited 1\n"));
	CHECK(is(cap_text(&sys), "house web: replay: exited 1\n"));
	CHECK(is(user(OLD, "shtetl:stall"), "replay: exited 1") &&
	    !there(NEW) && is(where(RF "/g"), "") && is(where(OLD), "/web"));
	v[0] = "zfs_rebase --abort " NEW "\n";
	v[1] = want;
	v[2] = NULL;
	CHECK((text = slurp("fake.log")) != NULL && in_order(text, v) &&
	    ends(text, want));
	/*
	 * W10: stalled: active, its jail running; a save as ever; the
	 * stall read again at a start; shtetl roll H clears it and rolls
	 */
	(void) sleep(1);
	CHECK(sh_house_save(house("web"), &c.c_err) == 0);
	CHECK(is(status(), "house web stalled replay: exited 1\n"));
	restart();
	CHECK(is(settle(), "house web stalled replay: exited 1\n"));
	rbfake_reset();
	(void) sleep(1);
	/* the tree the replay makes, gone with the version W7 destroyed */
	lay("15.1-releng-U-web", "releng/16.0");
	CHECK(roll() == 0 && is(status(), "house web rolling jail\n"));
	CHECK(is(settle(), "house web active 15.1-releng-U-web\n") &&
	    is(user(OLD, "shtetl:stall"), "(unset)"));

	/*
	 * W8: a failure at another step, as W7, its step's words, nothing
	 * to abort: the final save; pkg; house.conf; the update snapshot,
	 * the fifth snapshot of the roll after the final save and the
	 * three dataset forms' pre-apply snapshots
	 */
	for (i = 0; i < 4; i++) {
		static const char *const why[] = {
			"save: zfake: snapshot failed as asked",
			"pkg: exited 1",
			"house.conf: ",
			"update: zfake: snapshot failed as asked"
		};

		made();
		next();
		if (i == 1)
			CHECK(setenv("FAKE_FAIL", "pkg", 1) == 0);
		if (i == 2)
			CHECK(mkdir(scratch_path("containers/15.1-releng-U-web/"
			    "var/db/shtetl/house.conf"), 0755) == 0);
		CHECK(roll() == 0);
		if (i == 0)
			zfake_fail("snapshot");
		if (i == 3)
			zfake_fail_nth("snapshot", 5);
		(void) snprintf(want, sizeof (want), "house web stalled %s",
		    why[i]);
		CHECK(strncmp(settle(), want, strlen(want)) == 0);
		(void) snprintf(want, sizeof (want), "house web: %s", why[i]);
		CHECK(strncmp(cap_text(&sys), want, strlen(want)) == 0 &&
		    strchr(cap_text(&sys), '\n') ==
		    cap_text(&sys) + strlen(cap_text(&sys)) - 1);
		CHECK(!there(NEW) && is(where(RF "/g"), "") &&
		    is(where(OLD), "/web"));
		(void) snprintf(want, sizeof (want),
		    "jail -f %s/web.conf -cm web\n", jails);
		CHECK((text = slurp("fake.log")) != NULL && ends(text, want) &&
		    strstr(text, "--abort") == NULL);
		(void) unsetenv("FAKE_FAIL");
	}

	/*
	 * W9: refusals: no newer version on the run; a roll under way,
	 * which a save is refused as; a start under way
	 */
	made();
	CHECK(roll() == -1 && is(cap_text(&c), "web: up to date\n"));
	next();
	CHECK(roll() == 0 && roll() == -1 &&
	    is(cap_text(&c), "web: rolling\n"));
	cap_reset(&c);
	CHECK(sh_house_save(house("web"), &c.c_err) == -1 &&
	    is(cap_text(&c), "web: rolling\n"));
	CHECK(is(settle(), "house web active 15.1-releng-U-web\n"));
	restart();
	CHECK(roll() == -1 && is(cap_text(&c), "web: starting\n"));
	CHECK(is(settle(), "house web active 15.1-releng-U-web\n"));
	CHECK(roll() == -1 && is(cap_text(&c), "web: up to date\n"));

	/*
	 * W11, W15: the daemon stopped in the replay, a clone form's run
	 * open: its result read-only, mountpoint none, the record on it;
	 * at the next start the run aborted, the new version destroyed,
	 * the old one placed and started, no stall, nothing said
	 */
	made();
	next();
	rbfake_end(NEW "/usr/local", -1);
	CHECK(roll() == 0);
	until_open(NEW "/usr/local");
	CHECK(is(status(), "house web rolling replay\n"));
	CHECK(is(zfake_local(NEW "/usr/local", "readonly"), "on") &&
	    is(zfake_local(NEW "/usr/local", "mountpoint"), "none"));
	(void) unlink(scratch_path("fake.log"));
	restart();
	CHECK(is(settle(), "house web active 15.1-releng-T-web\n") &&
	    is(cap_text(&sys), ""));
	CHECK(!there(NEW) && is(user(OLD, "shtetl:stall"), "(unset)") &&
	    is(where(OLD), "/web") && is(where(RF "/g"), ""));
	(void) snprintf(want, sizeof (want), "jail -f %s/web.conf -cm web\n",
	    jails);
	v[0] = want;
	v[1] = "zfs_rebase --abort " NEW "/usr/local\n";
	v[2] = NULL;
	CHECK((text = slurp("fake.log")) != NULL && in_order(text, v));
	/*
	 * W11, W15: and in a dataset form, its run open, the pre-apply
	 * snapshot taken, the record on the version's root: the root
	 * rolled back to it at the next start, then destroyed
	 */
	made();
	next();
	snap_at(OLD, 0, cr, sizeof (cr));
	(void) snprintf(l[0], sizeof (l[0]), NEW "@%s", cr);
	rbfake_end(l[0], -1);
	CHECK(roll() == 0);
	until_open(NEW);
	CHECK(there(l[0]) && is(where(NEW), ""));
	restart();
	CHECK(is(settle(), "house web active 15.1-releng-T-web\n") &&
	    is(cap_text(&sys), "") && !there(NEW));
	CHECK((text = slurp("fake.log")) != NULL &&
	    strstr(text, "zfs_rebase --abort " NEW "\n") != NULL);

	/*
	 * W12: the swap refused part way, after the update snapshot, the
	 * old version not unmounted (the fourth unmount, after the three
	 * dataset forms'): failed, said once; the next start finishes it,
	 * the new version placed and started
	 */
	made();
	next();
	CHECK(roll() == 0);
	zfake_fail_nth("unmount", 4);
	CHECK(is(settle(), "house web failed\n"));
	CHECK(is(cap_text(&sys), "house web: swap: zfake: unmount failed as "
	    "asked\n"));
	CHECK(roll() == -1 && is(cap_text(&c), "web: failed\n"));
	cap_reset(&sys);
	restart();
	CHECK(is(settle(), "house web active 15.1-releng-U-web\n") &&
	    is(cap_text(&sys), ""));
	CHECK(is(where(NEW), "/web") && is(where(OLD), "") &&
	    zfake_local(OLD, "mountpoint") == NULL);

	sh_houses_free(hs, nh);
	(void) close(env.he_kq);
	sh_zfs_close(z);
	cap_close(&c);
	cap_close(&sys);
	scratch_clean();
	return (0);
}
