/*
 * Family H of tests/MATRIX.md: a house made (src/core/house.h) over the
 * fake ZFS, its chroot's programs tests/bin's, which log their words.
 * The beam's pull is the test's to deliver, as the beam would: the
 * copy made whole and arrived, then the asker told. The fake keeps no
 * files in a clone, so the foundation.conf a version holds is laid
 * where the version mounts at its own path, as a clone would show it.
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

/* Every create run to its end: the houses' status. */
static const char *
settle(void)
{
	while (strstr(status(), " creating ") != NULL)
		CHECK(one(20000));
	return (status());
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
	(void) scratch_write(rel, "src_branch = \"releng/15.1\";\n"
	    "pkg = [ \"tmux\" ];\n"
	    "datasets { \"usr/local\" {} }\n"
	    "commands { base = \"echo base\"; }\n");
	/* and the tree it was made in: what the chroot lends resolv.conf to */
	(void) snprintf(rel, sizeof (rel), "containers/%s/etc/motd", vn);
	(void) scratch_write(rel, "\n");
}

/*
 * The lake and the run, their skeleton, the run's containers below the
 * scratch directory; g on the lake at V, its usr a placeholder; the
 * houses none; the fake programs' log anew.
 */
static void
fresh(void)
{
	static const struct sh_zprop lake = { "shtetl:space", "lake" };
	static const struct sh_zprop run = { "shtetl:space", "run" };
	static const struct sh_zprop off = { "canmount", "off" };
	struct sh_spaces sp;

	sh_houses_free(hs, nh);
	hs = NULL;
	nh = 0;
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
	char path[1024], cwd[1024];
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
	    "commands { site = \"echo web\"; }\n");
	(void) scratch_write("config/containers/old.conf", "ver = \"<15\";\n");
	(void) scratch_write("resolv.conf", "nameserver 127.0.0.1\n");
	(void) snprintf(config, sizeof (config), "%s", scratch_path("config"));
	(void) snprintf(conts, sizeof (conts), "%s",
	    scratch_path("containers"));
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
	env.he_err = &sys.c_err;
	env.he_pull = asked;

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
	(void) snprintf(path, sizeof (path), "containers/%s/var/db/shtetl/"
	    "house.conf", VWEB);
	CHECK(stat(scratch_path(path), &st) == 0 &&
	    (st.st_mode & 0777) == 0444);
	CHECK((text = slurp(path)) != NULL &&
	    strstr(text, "nginx") != NULL);
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
			names_free(&s);
		}
		CHECK(kinds == (int)t.n && t.n == 4);
		names_free(&t);
	}
	/* placed: mounted at <containers>/web, its children below */
	CHECK(is(where(RC "/" VWEB), "/web") &&
	    is(where(RC "/" VWEB "/usr/local"), "/web/usr/local") &&
	    is(where(RC "/" VWEB "/usr"), ""));

	/* H10: web a house already; a recipe not there */
	CHECK(create("web") == -1 && is(cap_text(&c), "web: exists\n"));
	CHECK(create("nope") == -1 && strstr(cap_text(&c), "nope") != NULL &&
	    nh == 1);

	/* H12: started again: web active, mounted; unmounted, mounted */
	sh_houses_free(hs, nh);
	CHECK(sh_houses_start(z, RUN, &hs, &nh, &c.c_err) == 0 && nh == 1 &&
	    is(status(), "house web active " VWEB "\n"));
	CHECK(sh_zfs_unmount(z, RC "/" VWEB "/usr/local", &c.c_err) == 0 &&
	    sh_zfs_unmount(z, RC "/" VWEB "/www", &c.c_err) == 0 &&
	    sh_zfs_unmount(z, RC "/" VWEB, &c.c_err) == 0);
	sh_houses_free(hs, nh);
	CHECK(sh_houses_start(z, RUN, &hs, &nh, &c.c_err) == 0 && nh == 1 &&
	    is(where(RC "/" VWEB), "/web") &&
	    is(where(RC "/" VWEB "/www"), "/web/www"));

	/*
	 * H13: an incomplete version, a create a crash cut short:
	 * destroyed at the start; web active still
	 */
	CHECK(sh_zfs_clone(z, RF "/g@" V, RC "/cut-web", NULL, 0,
	    &c.c_err) == 0 &&
	    sh_zfs_set(z, RC "/cut-web", "shtetl:house", "web",
	    &c.c_err) == 0);
	sh_houses_free(hs, nh);
	CHECK(sh_houses_start(z, RUN, &hs, &nh, &c.c_err) == 0 && nh == 1 &&
	    !there(RC "/cut-web") &&
	    is(status(), "house web active " VWEB "\n"));

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
	CHECK(strstr(cap_text(&sys), "house web: pkg: ") != NULL);
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
