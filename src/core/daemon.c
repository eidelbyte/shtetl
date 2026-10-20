/*
 * The daemon's lock, its socket and its loop.
 *
 * A connection is read until its request is whole, the act runs, and
 * its reply goes out as the person's command reads it: the loop never
 * waits on one person, so every reply is written only when kqueue says
 * the socket takes more, and a slow reader holds up nobody.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/builder.h"
#include "core/daemon.h"
#include "core/err.h"
#include "core/foundation.h"
#include "core/job.h"
#include "core/mem.h"
#include "core/roll.h"
#include "core/show.h"
#include "core/spaces.h"
#include "core/syntax.h"
#include "core/wire.h"
#include "core/zfs.h"

/* One person's command, from its request to the last of its reply. */
struct conn {
	enum sh_ev	c_ev;		/* SH_EV_CONN, first, for the udata */
	int		c_fd;
	struct sh_buf	c_in;
	struct sh_buf	c_out;
	struct conn	*c_next;
};

struct daemon {
	int			d_kq;
	struct sh_zfs		*d_zfs;
	struct sh_spaces	d_sp;
	char			*d_froot;	/* <lake>/foundations */
	char			*d_oroot;	/* <lake>/foundation-builds */
	struct sh_foundation	**d_found;
	size_t			d_nfound;
	struct sh_builder	d_builder;
	struct sh_rollenv	d_env;
	struct conn		*d_conns;
};

static const char usage[] =
	"usage: shtetl daemon\n"
	"       shtetl pkg FOUNDATION\n"
	"       shtetl roll FOUNDATION\n"
	"       shtetl show [RECIPE]\n"
	"       shtetl status\n";

/* Where the space SP's root is mounted, into PATH: 0, or -1 said to E. */
static int
mounted(struct daemon *d, enum sh_space sp, char *path, size_t len,
    struct sh_err *e)
{
	const char *root = d->d_sp.ss_root[sp];
	int r;

	if (root == NULL) {
		sh_errf(e, "no %s space", sh_space_name(sp));
		return (-1);
	}
	if ((r = sh_zfs_mounted(d->d_zfs, root, path, len, e)) == 0)
		sh_errf(e, "%s: not mounted", root);
	return (r == 1 ? 0 : -1);
}

/* shtetl status */
static int
act_status(struct daemon *d, int argc, char **argv, FILE *out,
    struct sh_err *e)
{
	size_t f;
	int i;

	(void) argv;
	if (argc != 1) {
		(void) fputs(usage, e->se_out);
		return (2);
	}
	for (i = 0; i < SH_SP_N; i++) {
		if (d->d_sp.ss_root[i] != NULL)
			(void) fprintf(out, "space %s %s\n",
			    sh_space_name((enum sh_space)i),
			    d->d_sp.ss_root[i]);
	}
	for (f = 0; f < d->d_nfound; f++)
		sh_foundation_status(d->d_found[f], out);
	return (0);
}

/*
 * shtetl roll FOUNDATION: the roll asked for, the reply at once; the
 * roll is the daemon's, and status shows it.
 */
static int
act_roll(struct daemon *d, int argc, char **argv, FILE *out,
    struct sh_err *e)
{
	struct sh_foundation *f;
	char config[1024], build[1024];
	const char *why;

	(void) out;
	if (argc != 2) {
		(void) fputs(usage, e->se_out);
		return (2);
	}
	if ((why = sh_syn_name(argv[1])) != NULL) {
		sh_errf(e, "%s: invalid name: %s", argv[1], why);
		return (1);
	}
	if (d->d_froot == NULL) {
		sh_errf(e, "no lake space");
		return (1);
	}
	if (mounted(d, SH_SP_CONFIG, config, sizeof (config), e) != 0 ||
	    mounted(d, SH_SP_BUILD, build, sizeof (build), e) != 0)
		return (1);
	f = sh_foundation_find(&d->d_found, &d->d_nfound, d->d_froot,
	    argv[1]);
	return (sh_roll_ask(f, &d->d_env, config, build, 0, e) != 0);
}

/* shtetl pkg FOUNDATION: a pkg roll asked for, the reply at once */
static int
act_pkg(struct daemon *d, int argc, char **argv, FILE *out,
    struct sh_err *e)
{
	size_t i;

	(void) out;
	if (argc != 2) {
		(void) fputs(usage, e->se_out);
		return (2);
	}
	for (i = 0; i < d->d_nfound; i++) {
		if (strcmp(d->d_found[i]->f_name, argv[1]) == 0)
			return (sh_roll_ask(d->d_found[i], &d->d_env, NULL,
			    NULL, 1, e) != 0);
	}
	sh_errf(e, "%s: no such foundation", argv[1]);
	return (1);
}

/* shtetl show [RECIPE]: read from zconfig, where its root is mounted */
static int
act_show(struct daemon *d, int argc, char **argv, FILE *out,
    struct sh_err *e)
{
	char root[1024];

	if (argc > 2) {
		(void) fputs(usage, e->se_out);
		return (2);
	}
	if (mounted(d, SH_SP_CONFIG, root, sizeof (root), e) != 0)
		return (1);
	return (sh_show(root, argc == 2 ? argv[1] : NULL, out, e->se_out));
}

static const struct {
	const char	*a_verb;
	int		(*a_fn)(struct daemon *, int, char **, FILE *,
			    struct sh_err *);
} acts[] = {
	{ "pkg", act_pkg },
	{ "roll", act_roll },
	{ "show", act_show },
	{ "status", act_status }
};

/*
 * The act the request ARGS names, LEN bytes of arguments each ended by
 * a NUL, run; what it wrote and its exit status framed into C's reply.
 */
static void
act(struct daemon *d, struct conn *c, const char *args, size_t len)
{
	struct sh_err e;
	char **argv, *a, *obuf = NULL, *ebuf = NULL;
	size_t olen = 0, elen = 0, i;
	FILE *out, *err;
	int argc = 0;
	char status;

	/* ended by a NUL each, and the last by one of our own */
	a = sh_calloc(1, len + 1);
	(void) memcpy(a, args, len);
	argv = sh_calloc(len + 1, sizeof (char *));
	for (i = 0; i < len; i += strlen(a + i) + 1)
		argv[argc++] = a + i;
	out = sh_memstream(&obuf, &olen);
	err = sh_memstream(&ebuf, &elen);
	sh_err_init(&e, err, "shtetl: ");
	for (i = 0; argc > 0 && i < sizeof (acts) / sizeof (acts[0]) &&
	    strcmp(acts[i].a_verb, argv[0]) != 0; i++)
		;
	if (argc == 0) {
		(void) fputs(usage, err);
		status = 2;
	} else if (i == sizeof (acts) / sizeof (acts[0])) {
		sh_errf(&e, "%s: unknown command", argv[0]);
		(void) fputs(usage, err);
		status = 2;
	} else {
		status = (char)acts[i].a_fn(d, argc, argv, out, &e);
	}
	(void) fclose(out);
	(void) fclose(err);
	sh_wire_put(&c->c_out, SH_WIRE_OUT, obuf, olen);
	sh_wire_put(&c->c_out, SH_WIRE_ERR, ebuf, elen);
	sh_wire_put(&c->c_out, SH_WIRE_EXIT, &status, 1);
	free(obuf);
	free(ebuf);
	free(argv);
	free(a);
}

/* C's descriptor watched for FILTER, with C told when it fires. */
static void
watch(struct daemon *d, struct conn *c, int filter, int flags)
{
	struct kevent ev;

	EV_SET(&ev, c->c_fd, filter, flags, 0, 0, c);
	(void) kevent(d->d_kq, &ev, 1, NULL, 0, NULL);
}

static void
hang_up(struct daemon *d, struct conn *c)
{
	struct conn **p;

	for (p = &d->d_conns; *p != c; p = &(*p)->c_next)
		;
	*p = c->c_next;
	(void) close(c->c_fd);
	free(c->c_in.b_data);
	free(c->c_out.b_data);
	free(c);
}

static void
accepted(struct daemon *d, int sk)
{
	struct conn *c;
	int fd;

	if ((fd = accept(sk, NULL, NULL)) < 0)
		return;
	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);
	(void) fcntl(fd, F_SETFL, O_NONBLOCK);
	c = sh_calloc(1, sizeof (*c));
	c->c_ev = SH_EV_CONN;
	c->c_fd = fd;
	c->c_next = d->d_conns;
	d->d_conns = c;
	watch(d, c, EVFILT_READ, EV_ADD);
}

/*
 * More of C's request. Whole, it is answered, and C is read no more; a
 * person's command sends nothing else, so anything else hangs up.
 */
static void
readable(struct daemon *d, struct conn *c)
{
	char buf[4096];
	const char *p;
	size_t len;
	ssize_t n;
	int kind;

	if ((n = read(c->c_fd, buf, sizeof (buf))) < 0 && errno == EAGAIN)
		return;
	if (n <= 0) {
		hang_up(d, c);
		return;
	}
	sh_buf_add(&c->c_in, buf, (size_t)n);
	if (!sh_wire_get(&c->c_in, &kind, &p, &len))
		return;
	if (kind != SH_WIRE_ARGS) {
		hang_up(d, c);
		return;
	}
	act(d, c, p, len);
	watch(d, c, EVFILT_READ, EV_DELETE);
	watch(d, c, EVFILT_WRITE, EV_ADD);
}

/* As much of C's reply as the socket takes; all of it, and C is done. */
static void
writable(struct daemon *d, struct conn *c)
{
	ssize_t n;

	n = write(c->c_fd, c->c_out.b_data + c->c_out.b_off,
	    c->c_out.b_len - c->c_out.b_off);
	if (n < 0 && errno == EAGAIN)
		return;
	if (n > 0)
		c->c_out.b_off += (size_t)n;
	if (n < 0 || c->c_out.b_off == c->c_out.b_len)
		hang_up(d, c);
}

int
sh_daemon_listen(const char *lock, const char *sock, int *lk, int *sk,
    struct sh_err *e)
{
	struct sockaddr_un sun;
	char pid[32];
	mode_t um;
	int r;

	*sk = -1;
	if ((*lk = open(lock, O_RDWR | O_CREAT, 0644)) < 0) {
		sh_errf(e, "%s: %s", lock, strerror(errno));
		return (-1);
	}
	(void) fcntl(*lk, F_SETFD, FD_CLOEXEC);
	if (flock(*lk, LOCK_EX | LOCK_NB) != 0) {
		if (errno == EWOULDBLOCK)
			sh_errf(e, "already running");
		else
			sh_errf(e, "%s: %s", lock, strerror(errno));
		goto fail;
	}
	(void) snprintf(pid, sizeof (pid), "%ld\n", (long)getpid());
	(void) ftruncate(*lk, 0);
	(void) pwrite(*lk, pid, strlen(pid), 0);
	(void) memset(&sun, 0, sizeof (sun));
	sun.sun_family = AF_UNIX;
	if (strlcpy(sun.sun_path, sock, sizeof (sun.sun_path)) >=
	    sizeof (sun.sun_path)) {
		sh_errf(e, "%s: path too long", sock);
		goto fail;
	}
	if (unlink(sock) != 0 && errno != ENOENT) {
		sh_errf(e, "%s: %s", sock, strerror(errno));
		goto fail;
	}
	if ((*sk = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		sh_errf(e, "%s: %s", sock, strerror(errno));
		goto fail;
	}
	/* a socket's mode is 0777 less the umask; connect needs only w */
	um = umask(0177);
	r = bind(*sk, (struct sockaddr *)&sun, sizeof (sun));
	(void) umask(um);
	if (r != 0 || listen(*sk, 16) != 0) {
		sh_errf(e, "%s: %s", sock, strerror(errno));
		goto fail;
	}
	(void) fcntl(*sk, F_SETFD, FD_CLOEXEC);
	(void) fcntl(*sk, F_SETFL, O_NONBLOCK);
	return (0);
fail:
	if (*sk >= 0)
		(void) close(*sk);
	(void) close(*lk);
	*sk = *lk = -1;
	return (-1);
}

int
sh_daemon_run(int sk, const char *sock, struct sh_zfs *z,
    const char *logdir, struct sh_err *e)
{
	struct daemon d;
	struct kevent ev[3];
	size_t f, n;
	int done = 0, r = -1;
	void *u;

	(void) memset(&d, 0, sizeof (d));
	d.d_zfs = z;
	d.d_kq = -1;
	sh_builder_init(&d.d_builder);
	if (sh_spaces_find(z, &d.d_sp, e) != 0 ||
	    sh_spaces_skeleton(z, &d.d_sp, e) != 0)
		goto out;
	if (d.d_sp.ss_root[SH_SP_LAKE] != NULL) {
		n = strlen(d.d_sp.ss_root[SH_SP_LAKE]) +
		    sizeof ("/foundation-builds");
		d.d_froot = sh_calloc(1, n);
		d.d_oroot = sh_calloc(1, n);
		(void) snprintf(d.d_froot, n, "%s/foundations",
		    d.d_sp.ss_root[SH_SP_LAKE]);
		(void) snprintf(d.d_oroot, n, "%s/foundation-builds",
		    d.d_sp.ss_root[SH_SP_LAKE]);
		if (sh_foundations_start(z, d.d_froot, &d.d_found,
		    &d.d_nfound, e) != 0)
			goto out;
	}
	if (mkdir(logdir, 0755) != 0 && errno != EEXIST) {
		sh_errf(e, "%s: %s", logdir, strerror(errno));
		goto out;
	}
	if ((d.d_kq = kqueue()) < 0) {
		sh_errf(e, "kqueue: %s", strerror(errno));
		goto out;
	}
	d.d_env.re_z = z;
	d.d_env.re_builder = &d.d_builder;
	d.d_env.re_obj = d.d_oroot;
	d.d_env.re_resolv = "/etc/resolv.conf";
	d.d_env.re_kq = d.d_kq;
	d.d_env.re_logdir = logdir;
	d.d_env.re_err = e;
	d.d_env.re_flags = SF_IMMUTABLE;
	/* a signal kqueue watches is recorded even while it is ignored */
	(void) signal(SIGTERM, SIG_IGN);
	(void) signal(SIGINT, SIG_IGN);
	(void) signal(SIGPIPE, SIG_IGN);
	EV_SET(&ev[0], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	EV_SET(&ev[1], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	EV_SET(&ev[2], sk, EVFILT_READ, EV_ADD, 0, 0, NULL);
	if (kevent(d.d_kq, ev, 3, NULL, 0, NULL) != 0) {
		sh_errf(e, "kevent: %s", strerror(errno));
		goto out;
	}
	r = 0;
	/*
	 * One event at a time: an event that ends a job or a connection
	 * frees it, and no other already taken can name it then.
	 */
	while (!done) {
		if (kevent(d.d_kq, NULL, 0, ev, 1, NULL) != 1) {
			if (errno == EINTR)
				continue;
			sh_errf(e, "kevent: %s", strerror(errno));
			r = -1;
			break;
		}
		u = ev[0].udata;
		if (ev[0].filter == EVFILT_SIGNAL)
			done = 1;
		else if (u == NULL)
			accepted(&d, sk);
		else if (*(enum sh_ev *)u == SH_EV_JOB)
			sh_job_event(u, d.d_kq, &ev[0]);
		else if (ev[0].filter == EVFILT_READ)
			readable(&d, u);
		else
			writable(&d, u);
	}
out:
	while (d.d_conns != NULL)
		hang_up(&d, d.d_conns);
	/* what a stopped roll leaves, the next start puts back */
	for (f = 0; f < d.d_nfound; f++)
		sh_roll_stop(d.d_found[f]);
	if (d.d_kq >= 0)
		(void) close(d.d_kq);
	(void) unlink(sock);
	sh_foundations_free(d.d_found, d.d_nfound);
	free(d.d_froot);
	free(d.d_oroot);
	sh_spaces_free(&d.d_sp);
	return (r);
}
