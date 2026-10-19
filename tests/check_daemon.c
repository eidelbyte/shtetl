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
	"       shtetl status\n";

#define	BIG	(256 * 1024)	/* more than any socket takes at once */

static struct sh_zfs *z;
static struct cap c;
static char lockp[1024], sockp[1024];
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
		_exit(sh_daemon_run(sk, sockp, z, &e) != 0);
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
	    "space run fast/run\n"
	    "foundation generic idle v1\n";
	static const char *const v1[] = { "tank/lake/foundations/generic@v1" };
	static const struct sh_zprop lake = { "shtetl:space", "lake" };
	static const struct sh_zprop run = { "shtetl:space", "run" };
	struct sh_buf b = { NULL, 0, 0 };
	struct stat st;
	const char *p;
	char *noargs[] = { NULL };
	char *frob[] = { "frob" };
	char *extra[] = { "status", "x" };
	char *big;
	char buf[64], wantpid[32];
	size_t len;
	int fd, lk, sk, kind, got;

	(void) atexit(reap);
	cap_open(&c);
	(void) snprintf(lockp, sizeof (lockp), "%s", scratch_path("pid"));
	(void) snprintf(sockp, sizeof (sockp), "%s", scratch_path("sock"));
	zfake_reset();
	zfake_pool("tank");
	zfake_pool("fast");
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, "tank/lake", &lake, 1, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, "fast/run", &run, 1, &c.c_err) == 0);
	/* a foundation with a version: D1 shows its actor started */
	CHECK(sh_zfs_create(z, "tank/lake/foundations", NULL, 0,
	    &c.c_err) == 0);
	CHECK(sh_zfs_create(z, "tank/lake/foundations/generic", NULL, 0,
	    &c.c_err) == 0);
	CHECK(sh_zfs_snapshot(z, v1, 1, NULL, 0, &c.c_err) == 0);

	/* D12: a socket's path holding what a dead daemon left */
	scratch_write("sock", "left\n");
	up();

	/* D1: status, on standard output, nothing on standard error */
	CHECK(status() == 0);
	CHECK(strcmp(obuf, want) == 0 && strcmp(ebuf, "") == 0);

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

	/* D10: SIGTERM: exit 0, the socket gone */
	CHECK(down() == 0);
	CHECK(stat(sockp, &st) != 0);

	/* D11: no daemon: the command says what connect said, exit 1 */
	CHECK(status() == 1);
	CHECK(strncmp(ebuf, "shtetl: ", 8) == 0 && strstr(ebuf, sockp) != NULL);
	CHECK(strcmp(obuf, "") == 0);

	/* D13: spaces refused: the daemon never starts, the socket gone */
	CHECK(sh_zfs_create(z, "fast/lake", &lake, 1, &c.c_err) == 0);
	cap_reset(&c);
	CHECK(sh_daemon_listen(lockp, sockp, &lk, &sk, &c.c_err) == 0);
	CHECK(sh_daemon_run(sk, sockp, z, &c.c_err) == -1);
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
