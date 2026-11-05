/*
 * Families H and N of tests/MATRIX.md: a house made and its jail
 * started (src/core/house.h) over the fake ZFS, the chroot's programs
 * and jail(8) tests/bin's, which log their words. The beam's pull is
 * the test's to deliver, as the beam would: the copy made whole and
 * arrived, then the asker told. The fake keeps no files in a clone, so
 * the foundation.conf a version holds is laid where the version mounts
 * at its own path, as a clone would show it; it keeps a mounted
 * filesystem's files for a snapshot's view and a move (zfake_files),
 * which the start reads house.conf through.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/stat.h>

#include <stdint.h>

#include "check.h"
#include "core/house.h"
#include "core/job.h"
#include "core/spaces.h"
#include "core/zfs.h"
#include "zfake.h"

#define	LAKE	"tank/lake"
#define	RUN	"fast/run"
#define	LF	LAKE "/foundations"
#define	RF	RUN "/foundations"
#define	RC	RUN "/containers"
#define	V	"15.1-releng-T-create"
#define	VWEB	"15.1-releng-T-web"

static struct cap c;
static struct cap sys;		/* the houses' error sink, syslog's */
static struct sh_zfs *z;
static struct sh_houseenv env;
static struct sh_house **hs;
static size_t nh;
static char config[1024], conts[1024], logdir[1024], resolv[1024];
static char jails[1024];

/* How often the beam was told a version or a snapshot was made. */
static int beamed;

static void
told(void *arg)
{
	(void) arg;
	beamed++;
}

/* The pull asked, for the test to deliver. */
static struct {
	char	snap[300];
	void	(*done)(void *, const char *, const char *);
	void	*arg;
	int	asked;
} pull;

static void
asked(void *arg, const char *snap,
    void (*done)(void *, const char *, const char *), void *darg)
{
	(void) arg;
	(void) snprintf(pull.snap, sizeof (pull.snap), "%s", snap);
	pull.done = done;
	pull.arg = darg;
	pull.asked++;
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

/*
 * The pull delivered: g's tree at the version sent whole to the run
 * where the copy is not there, its top's snapshot arrived, the asker
 * told; with WHY, told it failed.
 */
static void
deliver(const char *why)
{
	char from[300], to[300];
	struct names t;
	uint64_t g, x;
	size_t i;

	if (why != NULL) {
		pull.done(pull.arg, NULL, why);
		return;
	}
	tree(LF "/g", &t);
	for (i = 0; i < t.n; i++) {
		(void) snprintf(from, sizeof (from), "%s@%s", t.v[i], V);
		(void) snprintf(to, sizeof (to), "%s%s@%s", RF,
		    t.v[i] + strlen(LF), V);
		if (sh_zfs_snapinfo(z, to, &g, &x, &c.c_err) != 0)
			CHECK(sh_zfs_send(z, from, NULL, to, NULL,
			    &c.c_err) == 0);
	}
	names_free(&t);
	cap_reset(&c);
	CHECK(sh_zfs_set(z, RF "/g@" V, "shtetl:kind", "create",
	    &c.c_err) == 0);
	pull.done(pull.arg, RF "/g@" V, NULL);
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

/* Every create, start and upgrade run to its end: the houses' status. */
static const char *
settle(void)
{
	while (strstr(status(), " creating ") != NULL ||
	    strstr(status(), " starting ") != NULL ||
	    strstr(status(), " upgrading ") != NULL)
		CHECK(one(20000));
	return (status());
}

/* The kinds of DS's snapshots, oldest first, a space after each. */
static const char *
kinds(const char *ds)
{
	static char buf[256];
	char name[512], v[64];
	struct names s;
	size_t i;

	s.n = 0;
	buf[0] = '\0';
	CHECK(sh_zfs_snapshots(z, ds, name_add, &s, &c.c_err) == 0);
	for (i = 0; i < s.n; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", ds, s.v[i]);
		CHECK(sh_zfs_user(z, name, "shtetl:kind", v, sizeof (v),
		    &c.c_err) == 0);
		(void) strlcat(buf, v, sizeof (buf));
		(void) strlcat(buf, " ", sizeof (buf));
	}
	names_free(&s);
	return (buf);
}

/* Whether every dataset of ROOT's tree has snapshots of kinds WANT. */
static int
kinds_all(const char *root, const char *want)
{
	struct names t;
	size_t i;
	int r = 1;

	tree(root, &t);
	for (i = 0; i < t.n; i++)
		r = r && is(kinds(t.v[i]), want);
	names_free(&t);
	return (r);
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

/* The name after the @ of DS's oldest snapshot, its create's, into SNAP. */
static void
create_snap(const char *ds, char *snap, size_t len)
{
	struct names s;

	s.n = 0;
	CHECK(sh_zfs_snapshots(z, ds, name_add, &s, &c.c_err) == 0 &&
	    s.n > 0);
	(void) snprintf(snap, len, "%s", s.v[0]);
	names_free(&s);
}

/* The houses started again from ZFS, as the daemon's start does. */
static void
restart(void)
{
	sh_houses_free(hs, nh);
	cap_reset(&c);
	CHECK(sh_houses_start(&env, &hs, &nh, &c.c_err) == 0);
}

/* The jail's block of the house H, whole; NULL if none. */
static const char *
block(const char *h)
{
	char rel[300];

	(void) snprintf(rel, sizeof (rel), "jails/%s.conf", h);
	return (slurp(rel));
}

/* A file under the scratch containers, laid anew with TEXT. */
static void
relay(const char *rel, const char *text)
{
	char path[1024];

	(void) snprintf(path, sizeof (path), "containers/%s", rel);
	(void) unlink(scratch_path(path));
	(void) scratch_write(path, text);
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

/* The house's version VN mounted where: its path below CONTS, or "". */
static const char *
where(const char *ds)
{
	static char p[1024];

	if (sh_zfs_mounted(z, ds, p, sizeof (p), &c.c_err) != 1)
		return ("");
	return (p + strlen(conts));
}

/* What the version VN's clone holds, laid where it mounts. */
static void
lay(const char *vn)
{
	char rel[512];

	(void) snprintf(rel, sizeof (rel), "containers/%s/var/db/shtetl/"
	    "foundation.conf", vn);
	/* pkg as show writes it in a shaken recipe, an object */
	(void) scratch_write(rel, "src_branch = \"releng/15.1\";\n"
	    "pkg { tmux = true; }\n"
	    "datasets { \"usr/local\" {} }\n"
	    "commands { base = \"echo base\"; }\n");
	/* and the tree it was made in: what the chroot lends resolv.conf to */
	(void) snprintf(rel, sizeof (rel), "containers/%s/etc/motd", vn);
	(void) scratch_write(rel, "\n");
}

/*
 * The lake and the run, their skeleton, the run's containers below the
 * scratch directory, emptied with the jails' blocks; g on the lake at
 * V, its usr a placeholder, its var a dataset of its own, which holds
 * var/db/shtetl (N8); the houses none; the fake programs' log anew.
 */
static void
fresh(void)
{
	static const struct sh_zprop lake = { "shtetl:space", "lake" };
	static const struct sh_zprop run = { "shtetl:space", "run" };
	static const struct sh_zprop off = { "canmount", "off" };
	struct sh_spaces sp;
	char cmd[2200];

	sh_houses_free(hs, nh);
	hs = NULL;
	nh = 0;
	(void) snprintf(cmd, sizeof (cmd), "rm -rf '%s' '%s'", conts, jails);
	CHECK(system(cmd) == 0 && mkdir(jails, 0755) == 0);
	zfake_reset();
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
	CHECK(sh_zfs_create(z, LF "/g", NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, LF "/g/usr", &off, 1, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, LF "/g/usr/local", NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, LF "/g/var", NULL, 0, &c.c_err) == 0);
	take(LF "/g", V, "create");
	(void) unlink(scratch_path("fake.log"));
	memset(&pull, 0, sizeof (pull));
}

static int
create(const char *name)
{
	cap_reset(&c);
	return (sh_house_create(&hs, &nh, name, LF "/g", V, &env, config,
	    &c.c_err));
}

int
main(void)
{
	char path[1024], cwd[1024], want[2048], snap[128];
	const char *text;
	struct stat st;

	cap_open(&c);
	cap_open(&sys);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	(void) snprintf(path, sizeof (path), "%s/tests/bin:%s",
	    getcwd(cwd, sizeof (cwd)), getenv("PATH"));
	CHECK(setenv("PATH", path, 1) == 0);
	CHECK(setenv("FAKE_LOG", scratch_path("fake.log"), 1) == 0);
	(void) scratch_write("config/containers/web.conf",
	    "ver = \"<16\";\npkg = [ \"nginx\" ];\n"
	    "datasets { www { compression = \"lz4\"; } }\n"
	    "commands { site = \"echo web\"; }\n"
	    "jailconf = \"allow.raw_sockets;\";\n");
	(void) scratch_write("config/containers/app.conf",
	    "pkg = [ \"x\" ];\n");
	/* V5: no package of its own: tmux, its only one, the foundation's */
	(void) scratch_write("config/containers/bare.conf",
	    "pkg = [ \"tmux\" ];\n");
	(void) scratch_write("config/containers/old.conf", "ver = \"<15\";\n");
	(void) scratch_write("resolv.conf", "nameserver 127.0.0.1\n");
	(void) snprintf(config, sizeof (config), "%s", scratch_path("config"));
	(void) snprintf(conts, sizeof (conts), "%s",
	    scratch_path("containers"));
	(void) snprintf(jails, sizeof (jails), "%s", scratch_path("jails"));
	CHECK(mkdir(scratch_path("logs"), 0755) == 0);
	CHECK((env.he_kq = kqueue()) >= 0);
	env.he_z = z;
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
	/* N18: the snapshot's view the start reads house.conf through */
	zfake_files();

	/*
	 * H2, H1, H5 - H7, H15: web made on g, the run holding none of
	 * it: the pull asked; told, the version cloned, its datasets the
	 * copy's and its own, the recipe's packages and pieces run in a
	 * chroot of it, the foundation's not again, house.conf, the create
	 * snapshot, placed at <containers>/web
	 */
	fresh();
	CHECK(create("web") == 0 && nh == 1 && pull.asked == 1 &&
	    is(pull.snap, LF "/g@" V));
	CHECK(is(status(), "house web creating pull\n"));
	lay(VWEB);
	deliver(NULL);
	CHECK(is(settle(), "house web active " VWEB "\n"));
	CHECK(is(cap_text(&sys), ""));
	CHECK(is(origin(RC "/" VWEB), RF "/g@" V) &&
	    is(origin(RC "/" VWEB "/usr"), RF "/g/usr@" V) &&
	    is(origin(RC "/" VWEB "/usr/local"), RF "/g/usr/local@" V));
	CHECK(is(user(RC "/" VWEB, "shtetl:house"), "web"));
	CHECK(is(native(RC "/" VWEB, "canmount"), "noauto") &&
	    is(native(RC "/" VWEB "/usr", "canmount"), "off") &&
	    is(native(RC "/" VWEB "/usr/local", "canmount"), "noauto"));
	/* H5: its own dataset, the recipe's properties, canmount noauto */
	CHECK(is(zfake_local(RC "/" VWEB "/www", "compression"), "lz4") &&
	    is(native(RC "/" VWEB "/www", "canmount"), "noauto"));
	/* H6: nginx installed, resolv.conf lent, no tmux; site run, no base */
	CHECK((text = slurp("fake.log")) != NULL);
	CHECK(strstr(text, "install -y -- nginx (resolv.conf)\n") != NULL &&
	    strstr(text, "tmux") == NULL);
	CHECK(strstr(text, "echo web") != NULL &&
	    strstr(text, "echo base") == NULL);
	/* H7: house.conf in the version, the shaken house recipe, 0444 */
	CHECK(stat(scratch_path("containers/web/var/db/shtetl/house.conf"),
	    &st) == 0 && (st.st_mode & 0777) == 0444);
	CHECK((text = slurp("containers/web/var/db/shtetl/house.conf")) !=
	    NULL && strstr(text, "nginx") != NULL);
	/* H1: the create snapshot on every dataset, kind create */
	{
		struct names t, s;
		size_t i;
		int kinds = 0;

		tree(RC "/" VWEB, &t);
		for (i = 0; i < t.n; i++) {
			s.n = 0;
			CHECK(sh_zfs_snapshots(z, t.v[i], name_add, &s,
			    &c.c_err) == 0 && s.n == 1);
			(void) snprintf(path, sizeof (path), "%s@%s", t.v[i],
			    s.v[0]);
			kinds += is(user(path, "shtetl:kind"), "create");
			(void) snprintf(snap, sizeof (snap), "%s", s.v[0]);
			names_free(&s);
		}
		CHECK(kinds == (int)t.n && t.n == 5);
		names_free(&t);
	}
	/* placed: mounted at <containers>/web, its children below */
	CHECK(is(where(RC "/" VWEB), "/web") &&
	    is(where(RC "/" VWEB "/usr/local"), "/web/usr/local") &&
	    is(where(RC "/" VWEB "/usr"), ""));
	/*
	 * N10, N1, N8: the block written after the place, its path the
	 * version's mount, the recipe's jailconf in it, read through the
	 * create snapshot of var, which holds var/db/shtetl, the root's
	 * holding none of it; jail -cm the last step
	 */
	(void) snprintf(want, sizeof (want), "\tpath = \"%s/web\";\n", conts);
	CHECK((text = block("web")) != NULL && strstr(text, want) != NULL &&
	    strstr(text, "\tallow.raw_sockets;\n}\n") != NULL);
	(void) snprintf(want, sizeof (want), "jail -f %s/web.conf -cm web\n",
	    jails);
	CHECK((text = slurp("fake.log")) != NULL &&
	    strlen(text) > strlen(want) &&
	    strcmp(text + strlen(text) - strlen(want), want) == 0);
	(void) snprintf(path, sizeof (path), "containers/web/.zfs/snapshot/"
	    "%s/var/db", snap);
	CHECK(stat(scratch_path(path), &st) != 0);
	(void) snprintf(path, sizeof (path), "containers/web/var/.zfs/"
	    "snapshot/%s/db/shtetl/house.conf", snap);
	CHECK(stat(scratch_path(path), &st) == 0);

	/* H10: web a house already; a recipe not there */
	CHECK(create("web") == -1 && is(cap_text(&c), "web: exists\n"));
	CHECK(create("nope") == -1 && strstr(cap_text(&c), "nope") != NULL &&
	    nh == 1);

	/*
	 * H12, N12, N7: started again: web active, mounted, starting until
	 * its jail is; the block from the create snapshot, not the live
	 * tree, whose house.conf's directory was renamed and another
	 * planted in its place
	 */
	(void) snprintf(path, sizeof (path), "%s",
	    scratch_path("containers/web/var/db/shtetl"));
	(void) snprintf(want, sizeof (want), "%s.old", path);
	CHECK(rename(path, want) == 0);
	relay("web/var/db/shtetl/house.conf",
	    "jailconf { x = \"allow.mount;\"; }\n");
	(void) unlink(scratch_path("fake.log"));
	restart();
	CHECK(nh == 1 && is(status(), "house web starting jail\n"));
	CHECK(is(settle(), "house web active " VWEB "\n") &&
	    is(cap_text(&sys), ""));
	CHECK((text = block("web")) != NULL &&
	    strstr(text, "\tallow.raw_sockets;\n") != NULL &&
	    strstr(text, "allow.mount") == NULL);
	(void) snprintf(want, sizeof (want), "jail -f %s/web.conf -cm web\n",
	    jails);
	CHECK(is(slurp("fake.log"), want));
	/* H12: unmounted, mounted again at the start */
	CHECK(sh_zfs_unmount(z, RC "/" VWEB "/usr/local", &c.c_err) == 0 &&
	    sh_zfs_unmount(z, RC "/" VWEB "/www", &c.c_err) == 0 &&
	    sh_zfs_unmount(z, RC "/" VWEB "/var", &c.c_err) == 0 &&
	    sh_zfs_unmount(z, RC "/" VWEB, &c.c_err) == 0);
	restart();
	CHECK(nh == 1 && is(where(RC "/" VWEB), "/web") &&
	    is(where(RC "/" VWEB "/www"), "/web/www"));
	CHECK(is(settle(), "house web active " VWEB "\n"));

	/*
	 * H13: an incomplete version, a create a crash cut short:
	 * destroyed at the start; web active still
	 */
	CHECK(sh_zfs_clone(z, RF "/g@" V, RC "/cut-web", NULL, 0,
	    &c.c_err) == 0 &&
	    sh_zfs_set(z, RC "/cut-web", "shtetl:house", "web",
	    &c.c_err) == 0);
	restart();
	CHECK(nh == 1 && !there(RC "/cut-web") &&
	    is(settle(), "house web active " VWEB "\n"));

	/*
	 * N13, N9: two houses started again, web's house.conf gone from
	 * its create snapshot: web's start refused, said, and in its log;
	 * web active still, and app's jail started
	 */
	fresh();
	CHECK(create("web") == 0);
	lay(VWEB);
	deliver(NULL);
	CHECK(is(settle(), "house web active " VWEB "\n"));
	CHECK(create("app") == 0);
	lay("15.1-releng-T-app");
	deliver(NULL);
	CHECK(is(settle(), "house app active 15.1-releng-T-app\n"
	    "house web active " VWEB "\n"));
	create_snap(RC "/" VWEB "/var", snap, sizeof (snap));
	(void) snprintf(path, sizeof (path), "containers/web/var/.zfs/"
	    "snapshot/%s/db/shtetl/house.conf", snap);
	CHECK(unlink(scratch_path(path)) == 0);
	(void) unlink(scratch_path("fake.log"));
	cap_reset(&sys);
	restart();
	CHECK(is(settle(), "house app active 15.1-releng-T-app\n"
	    "house web active " VWEB "\n"));
	CHECK(strncmp(cap_text(&sys), "house web: block: ", 18) == 0 &&
	    strstr(cap_text(&sys), "house.conf") != NULL);
	(void) snprintf(want, sizeof (want), "jail -f %s/app.conf -cm app\n",
	    jails);
	CHECK(is(slurp("fake.log"), want));
	CHECK((text = slurp("logs/web.log")) != NULL &&
	    strstr(text, "# failed: block: ") != NULL);

	/*
	 * H18: the actor's start, ZFS refusing one house's mount: said
	 * once, that house failed and left out, the other active and its
	 * jail started; a create of the failed one refused (ruled
	 * 2026-11-05, Q-H1)
	 */
	fresh();
	CHECK(create("web") == 0);
	lay(VWEB);
	deliver(NULL);
	CHECK(is(settle(), "house web active " VWEB "\n"));
	CHECK(create("app") == 0);
	lay("15.1-releng-T-app");
	deliver(NULL);
	CHECK(is(settle(), "house app active 15.1-releng-T-app\n"
	    "house web active " VWEB "\n"));
	(void) unlink(scratch_path("fake.log"));
	cap_reset(&sys);
	zfake_fail("mount");
	restart();
	CHECK(is(settle(), "house app failed\nhouse web active " VWEB "\n"));
	CHECK(is(cap_text(&sys), "house app: zfake: mount failed as asked\n"));
	(void) snprintf(want, sizeof (want), "jail -f %s/web.conf -cm web\n",
	    jails);
	CHECK(is(slurp("fake.log"), want));
	CHECK(create("app") == -1 && is(cap_text(&c), "app: exists\n"));
	/*
	 * H19: an incomplete version ZFS will not destroy: the house
	 * failed, said once, its complete version left as it is; the other
	 * started; the next start destroys it and starts both
	 */
	CHECK(sh_zfs_clone(z, RF "/g@" V, RC "/cut-web", NULL, 0,
	    &c.c_err) == 0 &&
	    sh_zfs_set(z, RC "/cut-web", "shtetl:house", "web",
	    &c.c_err) == 0);
	(void) unlink(scratch_path("fake.log"));
	cap_reset(&sys);
	zfake_fail("destroy");
	restart();
	CHECK(is(settle(), "house app active 15.1-releng-T-app\n"
	    "house web failed\n") && there(RC "/cut-web"));
	CHECK(is(cap_text(&sys), "house web: zfake: destroy failed as "
	    "asked\n"));
	(void) snprintf(want, sizeof (want), "jail -f %s/app.conf -cm app\n",
	    jails);
	CHECK(is(slurp("fake.log"), want));
	cap_reset(&sys);
	restart();
	CHECK(is(settle(), "house app active 15.1-releng-T-app\n"
	    "house web active " VWEB "\n") && !there(RC "/cut-web") &&
	    is(cap_text(&sys), ""));

	/* V2: a save of a house being made: refused, nothing taken */
	fresh();
	beamed = 0;
	CHECK(create("web") == 0);
	cap_reset(&c);
	CHECK(sh_house_save(house("web"), &c.c_err) == -1 &&
	    is(cap_text(&c), "web: creating\n"));
	/* V9: a create's end told to the beam */
	lay(VWEB);
	deliver(NULL);
	CHECK(is(settle(), "house web active " VWEB "\n") && beamed == 1);
	/* V1: saved: every dataset at once, kind save; the beam told */
	CHECK(sh_house_save(house("web"), &c.c_err) == 0 && beamed == 2 &&
	    kinds_all(RC "/" VWEB, "create save "));
	/*
	 * V4: upgraded: house.conf and foundation.conf read from the
	 * create snapshot; nginx its own, tmux the foundation's; the
	 * snapshot after, kind pkg; upgrading meanwhile; the beam told.
	 * V2: a save while it upgrades refused.
	 */
	(void) unlink(scratch_path("fake.log"));
	CHECK(sh_house_pkg(house("web"), &c.c_err) == 0 &&
	    is(status(), "house web upgrading pkg\n"));
	cap_reset(&c);
	CHECK(sh_house_save(house("web"), &c.c_err) == -1 &&
	    is(cap_text(&c), "web: upgrading\n"));
	CHECK(is(settle(), "house web active " VWEB "\n") && beamed == 3 &&
	    is(cap_text(&sys), ""));
	CHECK(is(slurp("fake.log"), "pkg -j web upgrade -y -- nginx\n") &&
	    kinds_all(RC "/" VWEB, "create save pkg "));
	/* V6: pkg failing: said once, no snapshot, active; nothing told */
	CHECK(setenv("FAKE_FAIL", "pkg", 1) == 0);
	CHECK(sh_house_pkg(house("web"), &c.c_err) == 0);
	CHECK(is(settle(), "house web active " VWEB "\n") &&
	    is(cap_text(&sys), "house web: pkg: exited 1\n") && beamed == 3 &&
	    kinds_all(RC "/" VWEB, "create save pkg "));
	CHECK(unsetenv("FAKE_FAIL") == 0);
	/* V3: a save ZFS refuses: its words, nothing told */
	zfake_fail("snapshot");
	cap_reset(&c);
	CHECK(sh_house_save(house("web"), &c.c_err) == -1 &&
	    is(cap_text(&c), "zfake: snapshot failed as asked\n") &&
	    beamed == 3);
	/* V5: none of its own, tmux the foundation's: no pkg, the snapshot */
	CHECK(create("bare") == 0);
	lay("15.1-releng-T-bare");
	deliver(NULL);
	CHECK(is(settle(), "house bare active 15.1-releng-T-bare\n"
	    "house web active " VWEB "\n"));
	(void) unlink(scratch_path("fake.log"));
	CHECK(sh_house_pkg(house("bare"), &c.c_err) == 0);
	CHECK(is(settle(), "house bare active 15.1-releng-T-bare\n"
	    "house web active " VWEB "\n") && slurp("fake.log") == NULL &&
	    kinds_all(RC "/15.1-releng-T-bare", "create pkg "));
	/* V6: foundation.conf not in the create snapshot: said, no pkg */
	create_snap(RC "/" VWEB "/var", snap, sizeof (snap));
	(void) snprintf(path, sizeof (path), "containers/web/var/.zfs/"
	    "snapshot/%s/db/shtetl/foundation.conf", snap);
	CHECK(unlink(scratch_path(path)) == 0);
	cap_reset(&sys);
	CHECK(sh_house_pkg(house("web"), &c.c_err) == 0);
	CHECK(is(settle(), "house bare active 15.1-releng-T-bare\n"
	    "house web active " VWEB "\n") &&
	    strncmp(cap_text(&sys), "house web: packages: ", 21) == 0 &&
	    strstr(cap_text(&sys), "foundation.conf") != NULL);
	/* V2: started again, and one failed: refused as their state */
	restart();
	cap_reset(&c);
	CHECK(sh_house_save(house("web"), &c.c_err) == -1 &&
	    is(cap_text(&c), "web: starting\n"));
	CHECK(is(settle(), "house bare active 15.1-releng-T-bare\n"
	    "house web active " VWEB "\n"));
	zfake_fail("mount");
	restart();
	(void) settle();
	cap_reset(&c);
	CHECK(sh_house_pkg(house("bare"), &c.c_err) == -1 &&
	    is(cap_text(&c), "bare: failed\n"));

	/*
	 * N11: a create whose jail does not start: said; the version and
	 * the block it wrote destroyed, the house gone
	 */
	fresh();
	CHECK(setenv("FAKE_FAIL", "jail", 1) == 0);
	CHECK(create("web") == 0);
	lay(VWEB);
	deliver(NULL);
	CHECK(is(settle(), "") && nh == 0 && !there(RC "/" VWEB));
	CHECK(is(cap_text(&sys), "house web: jail: exited 1\n") &&
	    block("web") == NULL);
	CHECK(unsetenv("FAKE_FAIL") == 0);

	/* H11: a second create of a house being made */
	fresh();
	CHECK(create("web") == 0);
	CHECK(create("web") == -1 && is(cap_text(&c), "web: creating\n"));
	lay(VWEB);
	deliver(NULL);
	CHECK(is(settle(), "house web active " VWEB "\n"));

	/* H4: the pull failing: said, the house gone, nothing made */
	fresh();
	CHECK(create("web") == 0);
	deliver("send " LF "/g@" V ": refused");
	CHECK(nh == 0 && is(status(), "") && !there(RC "/" VWEB));
	CHECK(is(cap_text(&sys), "house web: pull: send " LF "/g@" V
	    ": refused\n"));

	/* H8: ver not held: refused, the version destroyed */
	fresh();
	CHECK(create("old") == 0);
	lay("15.1-releng-T-old");
	deliver(NULL);
	CHECK(is(settle(), "") && nh == 0 && !there(RC "/15.1-releng-T-old"));
	CHECK(strstr(cap_text(&sys), "house old: recipe: ") != NULL &&
	    strstr(cap_text(&sys), "ver: releng/15.1 is not <15") != NULL);

	/* H9: a step failing, pkg, then the snapshot: the version gone */
	fresh();
	CHECK(setenv("FAKE_FAIL", "pkg", 1) == 0);
	CHECK(create("web") == 0);
	lay(VWEB);
	deliver(NULL);
	CHECK(is(settle(), "") && nh == 0 && !there(RC "/" VWEB));
	CHECK(strstr(cap_text(&sys), "house web: pkg: ") != NULL &&
	    block("web") == NULL);
	CHECK(unsetenv("FAKE_FAIL") == 0);
	fresh();
	CHECK(create("web") == 0);
	lay(VWEB);
	zfake_fail("snapshot");
	deliver(NULL);
	CHECK(is(settle(), "") && nh == 0 && !there(RC "/" VWEB));
	CHECK(strstr(cap_text(&sys), "house web: snapshot: ") != NULL);

	sh_houses_free(hs, nh);
	(void) close(env.he_kq);
	sh_zfs_close(z);
	cap_close(&c);
	cap_close(&sys);
	scratch_clean();
	return (0);
}
