/*
 * The daemon and the command, src/core/daemon.c and command.c: family
 * D of tests/MATRIX.md. A daemon is forked over the fake as it stands,
 * on a socket listened on here first, so a command never races its
 * start; and it is sent SIGTERM only after it has answered one, so its
 * signals are set by then.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <signal.h>

#include "check.h"
#include "core/daemon.h"
#include "core/mem.h"
#include "core/wire.h"
#include "core/zfs.h"
#include "zfake.h"

static const char usage[] =
	"usage: shtetl daemon\n"
	"       shtetl create CONTAINER --on FOUNDATION\n"
	"       shtetl freeze CONTAINER\n"
	"       shtetl pkg CONTAINER|FOUNDATION\n"
	"       shtetl roll CONTAINER|FOUNDATION\n"
	"       shtetl save CONTAINER\n"
	"       shtetl show [RECIPE]\n"
	"       shtetl status\n";

#define	BIG	(256 * 1024)	/* more than any socket takes at once */

static struct sh_zfs *z;
static struct cap c;
static char lockp[1024], sockp[1024], logp[1024], jailp[1024];
static char kindbuf[256];
static pid_t pid;

/* What the last command wrote. */
static char *obuf, *ebuf;
static size_t olen, elen;

/*
 * A failed CHECK exits: the daemon goes with it, or it would hold the
 * test's output open and make check would wait on it for ever.
 */
static void
reap(void)
{
	if (pid > 0)
		(void) kill(pid, SIGKILL);
}

static void
up(void)
{
	struct sh_err e;
	int lk, sk;

	CHECK(sh_daemon_listen(lockp, sockp, &lk, &sk, &c.c_err) == 0);
	CHECK((pid = fork()) >= 0);
	if (pid == 0) {
		sh_err_init(&e, stderr, "daemon: ");
		_exit(sh_daemon_run(sk, sockp, z, logp, jailp, &e) != 0);
	}
	(void) close(sk);
	(void) close(lk);
}

/* SIGTERM, and how the daemon ended. */
static int
down(void)
{
	int st;

	CHECK(kill(pid, SIGTERM) == 0);
	CHECK(waitpid(pid, &st, 0) == pid);
	pid = 0;
	return (WIFEXITED(st) ? WEXITSTATUS(st) : -1);
}

/* shtetl ARGV: its exit status, what it wrote in obuf and ebuf. */
static int
sht(int argc, char **argv)
{
	FILE *o, *e;
	int r;

	free(obuf);
	free(ebuf);
	obuf = ebuf = NULL;
	CHECK((o = open_memstream(&obuf, &olen)) != NULL);
	CHECK((e = open_memstream(&ebuf, &elen)) != NULL);
	r = sh_command(sockp, argc, argv, o, e);
	(void) fclose(o);
	(void) fclose(e);
	return (r);
}

static int
status(void)
{
	char *argv[] = { "status" };

	return (sht(1, argv));
}

/* One snapshot's kind, the snapshot of the filesystem ARG. */
static int
kind_add(void *arg, const char *snap)
{
	char name[512], v[64];

	(void) snprintf(name, sizeof (name), "%s@%s", (const char *)arg, snap);
	if (sh_zfs_user(z, name, "shtetl:kind", v, sizeof (v), &c.c_err) != 0)
		(void) snprintf(v, sizeof (v), "-");
	(void) strlcat(kindbuf, v, sizeof (kindbuf));
	(void) strlcat(kindbuf, " ", sizeof (kindbuf));
	return (0);
}

/* The kinds of DS's snapshots, oldest first, a space after each. */
static const char *
kinds(const char *ds)
{
	kindbuf[0] = '\0';
	CHECK(sh_zfs_snapshots(z, ds, kind_add, (void *)(uintptr_t)ds,
	    &c.c_err) == 0);
	return (kindbuf);
}

/* The file PATH whole, until the next call; NULL if none. */
static const char *
slurp(const char *path)
{
	static char *text;
	size_t n = 0;
	FILE *in;

	free(text);
	text = NULL;
	if ((in = fopen(path, "r")) == NULL)
		return (NULL);
	if (getdelim(&text, &n, '\0', in) < 0) {
		free(text);
		text = NULL;
	}
	(void) fclose(in);
	return (text);
}

/*
 * Status, once the beam's convergence and the houses' starts under way
 * are over: in 10 s.
 */
static int
settled(void)
{
	int i, r = -1;

	for (i = 0; i < 100; i++) {
		if ((r = status()) != 0 ||
		    (strstr(obuf, "beam converging") == NULL &&
		    strstr(obuf, " starting ") == NULL &&
		    strstr(obuf, " upgrading ") == NULL))
			break;
		(void) usleep(100000);
	}
	return (r);
}

/* A client of our own, connected. */
static int
raw(void)
{
	struct sockaddr_un sun;
	int fd;

	(void) memset(&sun, 0, sizeof (sun));
	sun.sun_family = AF_UNIX;
	(void) strlcpy(sun.sun_path, sockp, sizeof (sun.sun_path));
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK(connect(fd, (struct sockaddr *)&sun, sizeof (sun)) == 0);
	return (fd);
}

/* Everything FD is sent until the daemon hangs up, into B. */
static void
drain(int fd, struct sh_buf *b)
{
	char buf[4096];
	ssize_t n;

	while ((n = read(fd, buf, sizeof (buf))) > 0)
		sh_buf_add(b, buf, (size_t)n);
	(void) close(fd);
}

int
main(void)
{
	static const char want[] =
	    "space lake tank/lake\n"
	    "space config tank/config\n"
	    "space run fast/run\n"
	    "foundation generic idle v1\n"
	    "house db active v2-db\n"
	    "house web active v1-web\n"
	    "beam converged\n";
	static const char *const v0[] = { "tank/lake/foundations/generic@v0" };
	static const char *const v1[] = { "tank/lake/foundations/generic@v1" };
	static const struct sh_zprop lake = { "shtetl:space", "lake" };
	static const struct sh_zprop run = { "shtetl:space", "run" };
	struct sh_zprop config[] = {
		{ "shtetl:space", "config" }, { "mountpoint", NULL }
	};
	struct sh_buf b = { NULL, 0, 0 };
	struct stat st;
	const char *p;
	char *noargs[] = { NULL };
	char *frob[] = { "frob" };
	char *extra[] = { "status", "x" };
	char *site[] = { "show" };
	char *web[] = { "show", "containers/web" };
	char *broken[] = { "show", "containers/broken" };
	char *two[] = { "show", "containers/web", "x" };
	char *roll[] = { "roll" };
	char *rollbad[] = { "roll", "bad/name" };
	char *rollgen[] = { "roll", "generic" };
	char *pkg[] = { "pkg" };
	char *pkgnope[] = { "pkg", "nope" };
	char *create[] = { "create", "web" };
	char *createbad[] = { "create", "bad/name", "--on", "generic" };
	char *create123[] = { "create", "123", "--on", "generic" };
	char *createnope[] = { "create", "web2", "--on", "nope" };
	char *createweb[] = { "create", "web", "--on", "generic" };
	char *creategen[] = { "create", "generic", "--on", "generic" };
	char *rollweb[] = { "roll", "web" };
	char *save[] = { "save" };
	char *savenope[] = { "save", "nope" };
	char *saveweb[] = { "save", "web" };
	char *pkgweb[] = { "pkg", "web" };
	char *freeze[] = { "freeze" };
	char *freezegen[] = { "freeze", "generic" };
	char *freezeweb[] = { "freeze", "web" };
	static const struct sh_zprop house[] = {
		{ "shtetl:house", "web" }, { "canmount", "noauto" }
	};
	static const struct sh_zprop dbhouse[] = {
		{ "shtetl:house", "db" }, { "canmount", "noauto" }
	};
	static const struct sh_zprop created = { "shtetl:kind", "create" };
	static const struct sh_zprop updated = { "shtetl:kind", "update" };
	static const char *const made[] = { "fast/run/containers/v1-web@c" };
	static const char *const rolled[] = { "fast/run/containers/v2-db@u" };
	struct sh_zprop conts = { "mountpoint", NULL };
	char *big;
	char buf[64], wantpid[32], zconfig[1024], path[2048], cwd[1024];
	char other[1024];
	size_t len;
	int fd, lk, sk, kind, got;

	(void) atexit(reap);
	cap_open(&c);
	(void) snprintf(lockp, sizeof (lockp), "%s", scratch_path("pid"));
	(void) snprintf(sockp, sizeof (sockp), "%s", scratch_path("sock"));
	(void) snprintf(logp, sizeof (logp), "%s", scratch_path("logs"));
	(void) snprintf(jailp, sizeof (jailp), "%s", scratch_path("jails"));
	CHECK(mkdir(jailp, 0755) == 0);
	/* jail(8) the fake's, which logs its words, never the host's */
	(void) snprintf(path, sizeof (path), "%s/tests/bin:%s",
	    getcwd(cwd, sizeof (cwd)), getenv("PATH"));
	CHECK(setenv("PATH", path, 1) == 0);
	CHECK(setenv("FAKE_LOG", scratch_path("fake.log"), 1) == 0);
	/* zconfig, the files show reads, where the fake mounts it */
	(void) scratch_write("zconfig/shtetl.conf", "run { keep = 3; }\n");
	(void) scratch_write("zconfig/containers/web.conf",
	    "pkg = [ \"nginx\" ];\n");
	(void) scratch_write("zconfig/containers/broken.conf", "pkgs = 1;\n");
	(void) snprintf(zconfig, sizeof (zconfig), "%s",
	    scratch_path("zconfig"));
	config[1].zp_value = zconfig;
	zfake_reset();
	zfake_pool("tank");
	zfake_pool("fast");
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, "tank/lake", &lake, 1, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, "fast/run", &run, 1, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, "tank/config", config, 2, &c.c_err) == 0);
	CHECK(sh_zfs_mount(z, "tank/config", &c.c_err) == 0);
	/* a foundation with a version: D1 shows its actor started */
	CHECK(sh_zfs_create(z, "tank/lake/foundations", NULL, 0,
	    &c.c_err) == 0);
	CHECK(sh_zfs_create(z, "tank/lake/foundations/generic", NULL, 0,
	    &c.c_err) == 0);
	CHECK(sh_zfs_snapshot(z, v0, 1, NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_snapshot(z, v1, 1, NULL, 0, &c.c_err) == 0);
	/*
	 * H16, N15: a house with a complete version, the run's containers
	 * below the scratch directory: started with the daemon, active,
	 * placed, its jail started; its create snapshot's house.conf laid
	 * where the fake keeps none, var/db/shtetl in its root
	 */
	conts.zp_value = scratch_path("containers");
	CHECK(sh_zfs_create(z, "fast/run/containers", &conts, 1,
	    &c.c_err) == 0);
	CHECK(sh_zfs_create(z, "fast/run/containers/v1-web", house, 2,
	    &c.c_err) == 0);
	CHECK(sh_zfs_snapshot(z, made, 1, &created, 1, &c.c_err) == 0);
	(void) scratch_write("containers/web/.zfs/snapshot/c/var/db/shtetl/"
	    "house.conf", "pkg { nginx = true; tree = true; }\n");
	(void) scratch_write("containers/web/.zfs/snapshot/c/var/db/shtetl/"
	    "foundation.conf", "pkg { tree = true; }\n");
	/*
	 * N7: a house whose version holds an update snapshot: the block
	 * from it, not from the live tree's house.conf
	 */
	CHECK(sh_zfs_create(z, "fast/run/containers/v2-db", dbhouse, 2,
	    &c.c_err) == 0);
	CHECK(sh_zfs_snapshot(z, rolled, 1, &updated, 1, &c.c_err) == 0);
	(void) scratch_write("containers/db/.zfs/snapshot/u/var/db/shtetl/"
	    "house.conf", "jailconf { \"containers/db.conf\" = "
	    "\"allow.sysvipc;\"; }\n");
	(void) scratch_write("containers/db/var/db/shtetl/house.conf",
	    "jailconf { x = \"allow.mount;\"; }\n");

	/* D12: a socket's path holding what a dead daemon left */
	scratch_write("sock", "left\n");
	up();

	/* D1, D25: status, the beam's line last, nothing on standard error */
	CHECK(settled() == 0);
	CHECK(strcmp(obuf, want) == 0 && strcmp(ebuf, "") == 0);
	/*
	 * N15, N7: both jails started, their blocks in the daemon's place;
	 * the two children run at once, so in either order
	 */
	(void) snprintf(path, sizeof (path), "jail -f %s/db.conf -cm db\n",
	    jailp);
	(void) snprintf(other, sizeof (other), "jail -f %s/web.conf -cm web\n",
	    jailp);
	CHECK((p = slurp(scratch_path("fake.log"))) != NULL &&
	    strstr(p, path) != NULL && strstr(p, other) != NULL &&
	    strlen(p) == strlen(path) + strlen(other));
	(void) snprintf(path, sizeof (path), "\tpath = \"%s\";\n",
	    scratch_path("containers/web"));
	CHECK((p = slurp(scratch_path("jails/web.conf"))) != NULL &&
	    strstr(p, path) != NULL);
	CHECK((p = slurp(scratch_path("jails/db.conf"))) != NULL &&
	    strstr(p, "\tallow.sysvipc;\n}\n") != NULL &&
	    strstr(p, "allow.mount") == NULL);
	/* L6, D25: the log directory made at the start, beam.log in it */
	CHECK(stat(logp, &st) == 0 && S_ISDIR(st.st_mode));
	CHECK(stat(scratch_path("logs/beam.log"), &st) == 0 &&
	    S_ISREG(st.st_mode));
	/*
	 * P21: the convergence over, the prune: v0, nothing's and older
	 * than the newest, gone, and said in beam.log
	 */
	{
		char text[4096];
		uint64_t g, t;
		size_t n;
		FILE *f;

		CHECK(sh_zfs_snapinfo(z, v0[0], &g, &t, &c.c_err) == -1);
		cap_reset(&c);
		CHECK((f = fopen(scratch_path("logs/beam.log"), "r")) != NULL);
		n = fread(text, 1, sizeof (text) - 1, f);
		text[n] = '\0';
		(void) fclose(f);
		CHECK(strstr(text, "# prune tank/lake/foundations/"
		    "generic@v0\n") != NULL);
	}

	/* D14, D15: the pidfile holds the pid; the socket is root's alone */
	(void) snprintf(wantpid, sizeof (wantpid), "%ld\n", (long)getpid());
	CHECK((fd = open(lockp, O_RDONLY)) >= 0);
	CHECK((got = (int)read(fd, buf, sizeof (buf) - 1)) > 0);
	buf[got] = '\0';
	(void) close(fd);
	CHECK(strcmp(buf, wantpid) == 0);
	CHECK(stat(sockp, &st) == 0 && (st.st_mode & 0777) == 0600);

	/* D2, D3, D4: no verb, an unknown one, too many words */
	CHECK(sht(0, noargs) == 2);
	CHECK(strcmp(obuf, "") == 0 && strcmp(ebuf, usage) == 0);
	CHECK(sht(1, frob) == 2);
	CHECK(strncmp(ebuf, "shtetl: frob: unknown command\n", 30) == 0 &&
	    strcmp(ebuf + 30, usage) == 0);
	CHECK(sht(2, extra) == 2);
	CHECK(strcmp(ebuf, usage) == 0);

	/* D5: a second daemon while this one runs */
	cap_reset(&c);
	CHECK(sh_daemon_listen(lockp, scratch_path("sock2"), &lk, &sk,
	    &c.c_err) == -1);
	CHECK(cap_has(&c, "already running"));

	/* D6: a request in two writes, the header cut: answered whole */
	sh_wire_put(&b, SH_WIRE_ARGS, "status", 7);
	fd = raw();
	CHECK(write(fd, b.b_data, 3) == 3);
	(void) usleep(20000);
	CHECK(write(fd, b.b_data + 3, b.b_len - 3) == (ssize_t)b.b_len - 3);
	free(b.b_data);
	(void) memset(&b, 0, sizeof (b));
	drain(fd, &b);
	CHECK(sh_wire_get(&b, &kind, &p, &len) && kind == SH_WIRE_OUT &&
	    len == strlen(want) && memcmp(p, want, len) == 0);
	CHECK(sh_wire_get(&b, &kind, &p, &len) && kind == SH_WIRE_ERR &&
	    len == 0);
	CHECK(sh_wire_get(&b, &kind, &p, &len) && kind == SH_WIRE_EXIT &&
	    len == 1 && p[0] == 0);
	CHECK(!sh_wire_get(&b, &kind, &p, &len));
	free(b.b_data);
	(void) memset(&b, 0, sizeof (b));

	/* D7: a client that asks and leaves before the reply */
	sh_wire_put(&b, SH_WIRE_ARGS, "status", 7);
	fd = raw();
	CHECK(write(fd, b.b_data, b.b_len) == (ssize_t)b.b_len);
	(void) close(fd);
	free(b.b_data);
	(void) memset(&b, 0, sizeof (b));
	CHECK(status() == 0 && strcmp(obuf, want) == 0);

	/* D8: a client that says nothing and leaves */
	(void) close(raw());
	CHECK(status() == 0 && strcmp(obuf, want) == 0);

	/* D9: a record that is no request: hung up on, nothing sent */
	sh_wire_put(&b, SH_WIRE_OUT, "hello", 5);
	fd = raw();
	CHECK(write(fd, b.b_data, b.b_len) == (ssize_t)b.b_len);
	free(b.b_data);
	(void) memset(&b, 0, sizeof (b));
	drain(fd, &b);
	CHECK(b.b_len == 0);
	CHECK(status() == 0 && strcmp(obuf, want) == 0);

	/*
	 * D16: a request and a reply far larger than the socket takes in
	 * one write: an unknown verb of 256 KiB, said back in its refusal
	 */
	big = sh_calloc(1, BIG + 1);
	(void) memset(big, 'x', BIG);
	CHECK(sht(1, &big) == 2);
	CHECK(elen == 8 + BIG + 18 + strlen(usage) &&
	    strncmp(ebuf + 8 + BIG, ": unknown command\n", 18) == 0);

	/* D17: a reader that does not read holds up nobody */
	sh_wire_put(&b, SH_WIRE_ARGS, big, BIG + 1);
	fd = raw();
	for (len = 0; len < b.b_len; len += (size_t)got)
		CHECK((got = (int)write(fd, b.b_data + len, b.b_len - len)) >
		    0);
	free(b.b_data);
	(void) memset(&b, 0, sizeof (b));
	CHECK(status() == 0 && strcmp(obuf, want) == 0);
	drain(fd, &b);
	CHECK(sh_wire_get(&b, &kind, &p, &len) && kind == SH_WIRE_OUT &&
	    len == 0);
	CHECK(sh_wire_get(&b, &kind, &p, &len) && kind == SH_WIRE_ERR &&
	    len == 8 + BIG + 18 + strlen(usage));
	CHECK(sh_wire_get(&b, &kind, &p, &len) && kind == SH_WIRE_EXIT &&
	    p[0] == 2);
	free(b.b_data);
	(void) memset(&b, 0, sizeof (b));
	free(big);

	/* D19: show: the site's settings, from zconfig where it is mounted */
	CHECK(sht(1, site) == 0 && strcmp(ebuf, "") == 0);
	CHECK(strstr(obuf, "# the site's settings: shtetl.conf over the "
	    "built-in ones\n") == obuf);
	CHECK(strstr(obuf, "    keep = 3;  # shtetl.conf, over "
	    "(built-in)\n") != NULL);

	/* D20: show RECIPE: it merged */
	CHECK(sht(2, web) == 0 && strcmp(ebuf, "") == 0);
	CHECK(strstr(obuf, "# containers/web.conf: a container's recipe, "
	    "its layers merged\n") == obuf);
	CHECK(strstr(obuf, "    nginx = true;  # containers/web.conf\n") !=
	    NULL);

	/* D21: a refused recipe: its words, nothing shown, 1 */
	CHECK(sht(2, broken) == 1 && strcmp(obuf, "") == 0);
	CHECK(strcmp(ebuf, "shtetl: containers/broken.conf: pkgs: invalid "
	    "key\n") == 0);

	/* D22: two recipes: the usage, 2 */
	CHECK(sht(3, two) == 2 && strcmp(ebuf, usage) == 0);

	/* F16: roll with no foundation, a bad name, no build space */
	CHECK(sht(1, roll) == 2 && strcmp(ebuf, usage) == 0);
	CHECK(sht(2, rollbad) == 1 && strncmp(ebuf, "shtetl: bad/name: "
	    "invalid name: ", sizeof ("shtetl: bad/name: invalid name: ") -
	    1) == 0);
	CHECK(sht(2, rollgen) == 1 &&
	    strcmp(ebuf, "shtetl: no build space\n") == 0);
	/* U6: pkg with no foundation, one that is not there */
	CHECK(sht(1, pkg) == 2 && strcmp(ebuf, usage) == 0);
	CHECK(sht(2, pkgnope) == 1 &&
	    strcmp(ebuf, "shtetl: nope: no such house or foundation\n") == 0);
	CHECK(status() == 0 && strcmp(obuf, want) == 0);
	/* H16: create's words; a name, a foundation, a house there */
	CHECK(sht(2, create) == 2 && strcmp(ebuf, usage) == 0);
	CHECK(sht(4, createbad) == 1 && strncmp(ebuf, "shtetl: bad/name: "
	    "invalid name: ", sizeof ("shtetl: bad/name: invalid name: ") -
	    1) == 0);
	/* N16: a name jail(8) would take for a jail's id */
	CHECK(sht(4, create123) == 1 &&
	    strcmp(ebuf, "shtetl: 123: invalid name: digits only\n") == 0);
	CHECK(sht(4, createnope) == 1 &&
	    strcmp(ebuf, "shtetl: nope: no such foundation\n") == 0);
	CHECK(sht(4, createweb) == 1 &&
	    strcmp(ebuf, "shtetl: web: exists\n") == 0);
	CHECK(status() == 0 && strcmp(obuf, want) == 0);
	/*
	 * V7, W14: one namespace: a foundation's name created refused; a
	 * house's name rolled the house's roll, refused at once, v1-web,
	 * made by hand, being a clone of no foundation version, which
	 * nothing waits for (and so the queue never stalled it, Q9)
	 */
	CHECK(sht(4, creategen) == 1 &&
	    strcmp(ebuf, "shtetl: generic: a foundation\n") == 0);
	CHECK(sht(2, rollweb) == 1 &&
	    strcmp(ebuf, "shtetl: web: up to date\n") == 0);
	/*
	 * V8, V2, V1, V4: save's words; web saved and upgraded, the reply
	 * at once and empty, nginx its own, tree the foundation's; the
	 * snapshots after its create snapshot
	 */
	CHECK(sht(1, save) == 2 && strcmp(ebuf, usage) == 0);
	CHECK(sht(2, savenope) == 1 &&
	    strcmp(ebuf, "shtetl: nope: no such house\n") == 0);
	CHECK(sht(2, saveweb) == 0 && strcmp(obuf, "") == 0 &&
	    strcmp(ebuf, "") == 0);
	CHECK(sht(2, pkgweb) == 0 && strcmp(obuf, "") == 0 &&
	    strcmp(ebuf, "") == 0);
	CHECK(settled() == 0 && strcmp(obuf, want) == 0);
	CHECK(strcmp(kinds("fast/run/containers/v1-web"), "create save pkg ") ==
	    0);
	CHECK((p = slurp(scratch_path("fake.log"))) != NULL &&
	    strstr(p, "pkg -j web upgrade -y -- nginx\n") != NULL);

	/*
	 * Q12, Q11: freeze's words, a foundation's name no house's; web
	 * frozen, the reply at once and empty, stalled in status; shtetl
	 * roll web lifts it though nothing waits for web, "up to date"
	 */
	CHECK(sht(1, freeze) == 2 && strcmp(ebuf, usage) == 0);
	CHECK(sht(2, freezegen) == 1 &&
	    strcmp(ebuf, "shtetl: generic: no such house\n") == 0);
	CHECK(sht(2, freezeweb) == 0 && strcmp(obuf, "") == 0 &&
	    strcmp(ebuf, "") == 0);
	CHECK(status() == 0 &&
	    strstr(obuf, "house web stalled frozen\n") != NULL);
	CHECK(sht(2, rollweb) == 1 &&
	    strcmp(ebuf, "shtetl: web: up to date\n") == 0);
	CHECK(status() == 0 && strcmp(obuf, want) == 0);

	/* D10: SIGTERM: exit 0, the socket gone */
	CHECK(down() == 0);
	CHECK(stat(sockp, &st) != 0);
	/* N15: and no jail stopped (Q-J1) */
	CHECK((p = slurp(scratch_path("fake.log"))) != NULL &&
	    strstr(p, " -r ") == NULL);
	/* D25: the lake's foundation not beamed: no house on the run */
	{
		uint64_t g, t;

		CHECK(sh_zfs_snapinfo(z, "fast/run/foundations/generic@v1",
		    &g, &t, &c.c_err) == -1);
		cap_reset(&c);
	}

	/* D23: zconfig not mounted: said, 1 */
	CHECK(sh_zfs_unmount(z, "tank/config", &c.c_err) == 0);
	up();
	CHECK(sht(1, site) == 1 &&
	    strcmp(ebuf, "shtetl: tank/config: not mounted\n") == 0);
	CHECK(down() == 0);

	/* D24: no config space at all: said, 1 */
	zfake_reset();
	zfake_pool("tank");
	zfake_pool("fast");
	CHECK(sh_zfs_create(z, "tank/lake", &lake, 1, &c.c_err) == 0);
	up();
	CHECK(sht(1, site) == 1 &&
	    strcmp(ebuf, "shtetl: no config space\n") == 0);
	/* D25: no run: no beam, and no line of it */
	CHECK(status() == 0 && strcmp(obuf, "space lake tank/lake\n") == 0);
	CHECK(down() == 0);

	/* D11: no daemon: the command says what connect said, exit 1 */
	CHECK(status() == 1);
	CHECK(strncmp(ebuf, "shtetl: ", 8) == 0 && strstr(ebuf, sockp) != NULL);
	CHECK(strcmp(obuf, "") == 0);

	/* D13: spaces refused: the daemon never starts, the socket gone */
	CHECK(sh_zfs_create(z, "fast/lake", &lake, 1, &c.c_err) == 0);
	cap_reset(&c);
	CHECK(sh_daemon_listen(lockp, sockp, &lk, &sk, &c.c_err) == 0);
	CHECK(sh_daemon_run(sk, sockp, z, logp, jailp, &c.c_err) == -1);
	CHECK(cap_has(&c, "two lake roots: fast/lake and tank/lake"));
	CHECK(stat(sockp, &st) != 0);
	(void) close(sk);
	(void) close(lk);

	free(obuf);
	free(ebuf);
	sh_zfs_close(z);
	cap_close(&c);
	scratch_clean();
	return (0);
}
