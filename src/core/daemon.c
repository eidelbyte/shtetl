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

#include "core/beam.h"
#include "core/builder.h"
#include "core/daemon.h"
#include "core/err.h"
#include "core/foundation.h"
#include "core/house.h"
#include "core/job.h"
#include "core/mem.h"
#include "core/prune.h"
#include "core/roll.h"
#include "core/show.h"
#include "core/site.h"
#include "core/spaces.h"
#include "core/syntax.h"
#include "core/wire.h"
#include "core/zfs.h"

struct daemon;

/*
 * One person's command, from its request to the last of its reply: the
 * descriptors that came with it and their terminal's type, and the
 * house whose attended roll holds its reply until the roll ends
 * (m5-queue).
 */
struct conn {
	enum sh_ev	c_ev;		/* SH_EV_CONN, first, for the udata */
	int		c_fd;
	struct sh_buf	c_in;
	struct sh_buf	c_out;
	int		c_fds[3];
	int		c_nfds;
	char		*c_term;	/* their TERM, or NULL */
	struct sh_house	*c_held;
	struct daemon	*c_d;
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
	struct sh_beamenv	d_benv;
	struct sh_beam		*d_beam;	/* with the lake and the run */
	struct sh_house		**d_houses;
	size_t			d_nhouses;
	struct sh_houseenv	d_henv;
	struct conn		*d_conns;
	struct conn		*d_conn;	/* whose request is acted on */
};

static const char usage[] =
	"usage: shtetl daemon\n"
	"       shtetl create CONTAINER --on FOUNDATION\n"
	"       shtetl freeze CONTAINER\n"
	"       shtetl pkg CONTAINER|FOUNDATION\n"
	"       shtetl roll CONTAINER|FOUNDATION\n"
	"       shtetl save CONTAINER\n"
	"       shtetl show [RECIPE]\n"
	"       shtetl status\n";

/* The house NAME, or NULL. */
static struct sh_house *
house_named(struct daemon *d, const char *name)
{
	size_t i;

	for (i = 0; i < d->d_nhouses; i++) {
		if (strcmp(d->d_houses[i]->h_name, name) == 0)
			return (d->d_houses[i]);
	}
	return (NULL);
}

/* The foundation NAME, or NULL. */
static struct sh_foundation *
foundation_named(struct daemon *d, const char *name)
{
	size_t i;

	for (i = 0; i < d->d_nfound; i++) {
		if (strcmp(d->d_found[i]->f_name, name) == 0)
			return (d->d_found[i]);
	}
	return (NULL);
}

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
	for (f = 0; f < d->d_nhouses; f++)
		sh_house_status(d->d_houses[f], out);
	if (d->d_beam != NULL)
		sh_beam_status(d->d_beam, out);
	return (0);
}

static void watch(struct daemon *, struct conn *, int, int);
static void held_done(void *, int, const char *);

/* H's roll asked, attended where the request brought a terminal. */
static int
house_roll(struct daemon *d, struct sh_house *h, const char *config,
    struct sh_err *e)
{
	struct conn *c = d->d_conn;
	struct sh_hattend *a = NULL;

	if (c->c_nfds == 3) {
		a = sh_calloc(1, sizeof (*a));
		(void) memcpy(a->ha_fds, c->c_fds, sizeof (a->ha_fds));
		a->ha_term = c->c_term;
		a->ha_done = held_done;
		a->ha_arg = c;
	}
	if (sh_house_roll(&d->d_houses, &d->d_nhouses, h, config, a, e) != 0) {
		free(a);
		return (1);
	}
	if (a == NULL)
		return (0);
	/* the descriptors and their type the roll's now */
	c->c_nfds = 0;
	c->c_term = NULL;
	c->c_held = h;
	return (-1);
}

/*
 * An attended roll over: its reply, held until now, framed and sent;
 * the connection read no more.
 */
static void
held_done(void *arg, int status, const char *text)
{
	struct conn *c = arg;
	struct sh_err e;
	char *ebuf = NULL, st = (char)status;
	size_t elen = 0;
	FILE *err;

	err = sh_memstream(&ebuf, &elen);
	sh_err_init(&e, err, "shtetl: ");
	if (text[0] != '\0')
		sh_errf(&e, "%s", text);
	(void) fclose(err);
	sh_wire_put(&c->c_out, SH_WIRE_OUT, "", 0);
	sh_wire_put(&c->c_out, SH_WIRE_ERR, ebuf, elen);
	sh_wire_put(&c->c_out, SH_WIRE_EXIT, &st, 1);
	free(ebuf);
	c->c_held = NULL;
	watch(c->c_d, c, EVFILT_READ, EV_DELETE);
	watch(c->c_d, c, EVFILT_WRITE, EV_ADD);
}

/*
 * shtetl roll CONTAINER|FOUNDATION: the house's roll onto the next
 * version of its foundation the run holds, or the foundation's roll,
 * asked for, the reply at once; the roll is the daemon's, and status
 * shows it. Houses and foundations share one namespace (ruled
 * 2026-11-05). A house's, asked from a terminal, its descriptors with
 * the request, is attended, and its reply held until it ends: -1.
 */
static int
act_roll(struct daemon *d, int argc, char **argv, FILE *out,
    struct sh_err *e)
{
	struct sh_foundation *f;
	struct sh_house *h;
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
	if (mounted(d, SH_SP_CONFIG, config, sizeof (config), e) != 0)
		return (1);
	if ((h = house_named(d, argv[1])) != NULL)
		return (house_roll(d, h, config, e));
	if (mounted(d, SH_SP_BUILD, build, sizeof (build), e) != 0)
		return (1);
	f = sh_foundation_find(&d->d_found, &d->d_nfound, d->d_froot,
	    argv[1]);
	return (sh_roll_ask(f, &d->d_env, config, build, 0, e) != 0);
}

/*
 * shtetl create CONTAINER --on FOUNDATION: the house asked for, on the
 * foundation's newest version, the reply at once; the create is the
 * daemon's, and status shows it. The person picks the foundation, and
 * the container's requirements are checked against it, its ver for now
 * (ruled 2026-11-03; sprints/future-work.md).
 */
static int
act_create(struct daemon *d, int argc, char **argv, FILE *out,
    struct sh_err *e)
{
	const struct sh_foundation *f;
	char config[1024];
	const char *why;

	(void) out;
	if (argc != 4 || strcmp(argv[2], "--on") != 0) {
		(void) fputs(usage, e->se_out);
		return (2);
	}
	if ((why = sh_syn_name(argv[1])) != NULL) {
		sh_errf(e, "%s: invalid name: %s", argv[1], why);
		return (1);
	}
	/* one namespace for houses and foundations (ruled 2026-11-05) */
	if (foundation_named(d, argv[1]) != NULL) {
		sh_errf(e, "%s: a foundation", argv[1]);
		return (1);
	}
	if (d->d_beam == NULL) {
		sh_errf(e, "no %s space", d->d_froot == NULL ? "lake" : "run");
		return (1);
	}
	if (mounted(d, SH_SP_CONFIG, config, sizeof (config), e) != 0)
		return (1);
	f = foundation_named(d, argv[3]);
	if (f == NULL || f->f_newest == NULL) {
		sh_errf(e, "%s: %s", argv[3], f == NULL ?
		    "no such foundation" : "no version");
		return (1);
	}
	return (sh_house_create(&d->d_houses, &d->d_nhouses, argv[1],
	    f->f_ds, f->f_newest, &d->d_henv, config, e) != 0);
}

/*
 * shtetl pkg CONTAINER|FOUNDATION: the house's own packages upgraded,
 * or the foundation's pkg roll, whichever the name is (one namespace,
 * ruled 2026-11-05); the reply at once.
 */
static int
act_pkg(struct daemon *d, int argc, char **argv, FILE *out,
    struct sh_err *e)
{
	struct sh_foundation *f;
	struct sh_house *h;

	(void) out;
	if (argc != 2) {
		(void) fputs(usage, e->se_out);
		return (2);
	}
	if ((h = house_named(d, argv[1])) != NULL)
		return (sh_house_pkg(h, e) != 0);
	if ((f = foundation_named(d, argv[1])) != NULL)
		return (sh_roll_ask(f, &d->d_env, NULL, NULL, 1, e) != 0);
	sh_errf(e, "%s: no such house or foundation", argv[1]);
	return (1);
}

/* shtetl freeze CONTAINER: the house pinned where it is (plan 7.2) */
static int
act_freeze(struct daemon *d, int argc, char **argv, FILE *out,
    struct sh_err *e)
{
	struct sh_house *h;

	(void) out;
	if (argc != 2) {
		(void) fputs(usage, e->se_out);
		return (2);
	}
	if ((h = house_named(d, argv[1])) == NULL) {
		sh_errf(e, "%s: no such house", argv[1]);
		return (1);
	}
	return (sh_house_freeze(h, e) != 0);
}

/* shtetl save CONTAINER: the house's active version saved */
static int
act_save(struct daemon *d, int argc, char **argv, FILE *out,
    struct sh_err *e)
{
	struct sh_house *h;

	(void) out;
	if (argc != 2) {
		(void) fputs(usage, e->se_out);
		return (2);
	}
	if ((h = house_named(d, argv[1])) == NULL) {
		sh_errf(e, "%s: no such house", argv[1]);
		return (1);
	}
	return (sh_house_save(h, e) != 0);
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
	{ "create", act_create },
	{ "freeze", act_freeze },
	{ "pkg", act_pkg },
	{ "roll", act_roll },
	{ "save", act_save },
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
	int argc = 0, st;
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
		d->d_conn = c;
		st = acts[i].a_fn(d, argc, argv, out, &e);
		d->d_conn = NULL;
		status = (char)st;
	}
	(void) fclose(out);
	(void) fclose(err);
	/* held: an attended roll frames it at its end (held_done) */
	if (c->c_held != NULL) {
		free(obuf);
		free(ebuf);
		free(argv);
		free(a);
		return;
	}
	sh_wire_put(&c->c_out, SH_WIRE_OUT, obuf, olen);
	sh_wire_put(&c->c_out, SH_WIRE_ERR, ebuf, elen);
	sh_wire_put(&c->c_out, SH_WIRE_EXIT, &status, 1);
	free(obuf);
	free(ebuf);
	free(argv);
	free(a);
}

/*
 * A convergence of the beam over: what neither space needs pruned, by
 * the keeps shtetl.conf gives now (plan 8.2), each version pruned in
 * beam.log; then every house asked to take the next version waiting
 * (plan 7.2: after a roll, the prune, then the next).
 */
static int
pruned(void *arg, struct sh_err *e)
{
	struct daemon *d = arg;
	struct sh_site s;
	char path[1024];
	size_t i;
	int r;

	if (mounted(d, SH_SP_CONFIG, path, sizeof (path), e) != 0 ||
	    sh_site_load(path, &s, e) != 0)
		return (-1);
	r = sh_prune(d->d_zfs, d->d_benv.be_lake, d->d_benv.be_run,
	    sh_site_keep(&s, "run"), sh_site_keep(&s, "lake"),
	    d->d_benv.be_log, e);
	sh_site_free(&s);
	for (i = 0; i < d->d_nhouses; i++)
		sh_house_next(&d->d_houses, &d->d_nhouses, d->d_houses[i],
		    path);
	return (r);
}

/*
 * A house's act over: every house asked to take the next version
 * waiting, that one and any that waited its turn on its copy, where
 * zconfig is mounted; where it is not, the convergence's prune says so.
 */
static void
house_idle(void *arg)
{
	struct daemon *d = arg;
	char path[1024], *buf = NULL;
	struct sh_err q;
	size_t len = 0, i;
	FILE *f;
	int r;

	f = sh_memstream(&buf, &len);
	sh_err_init(&q, f, "");
	r = mounted(d, SH_SP_CONFIG, path, sizeof (path), &q);
	(void) fclose(f);
	free(buf);
	for (i = 0; r == 0 && i < d->d_nhouses; i++)
		sh_house_next(&d->d_houses, &d->d_nhouses, d->d_houses[i],
		    path);
}

/* A house's pull: the beam's (m5-create). */
static void
pull(void *arg, const char *snap,
    void (*done)(void *, const char *, const char *), void *darg)
{
	struct daemon *d = arg;

	sh_beam_pull(d->d_beam, snap, done, darg);
}

/* A foundation's version made: the beam carries it to the run. */
static void
made(void *arg)
{
	struct daemon *d = arg;

	if (d->d_beam != NULL)
		sh_beam_converge(d->d_beam);
}

/* C's descriptor watched for FILTER, with C told when it fires. */
static void
watch(struct daemon *d, struct conn *c, int filter, int flags)
{
	struct kevent ev;

	EV_SET(&ev, c->c_fd, filter, flags, 0, 0, c);
	(void) kevent(d->d_kq, &ev, 1, NULL, 0, NULL);
}

/* The descriptors that came with C's request let go. */
static void
let_go(struct conn *c)
{
	while (c->c_nfds > 0)
		(void) close(c->c_fds[--c->c_nfds]);
}

static void
hang_up(struct daemon *d, struct conn *c)
{
	struct conn **p;

	for (p = &d->d_conns; *p != c; p = &(*p)->c_next)
		;
	*p = c->c_next;
	let_go(c);
	(void) close(c->c_fd);
	free(c->c_term);
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
	c->c_d = d;
	c->c_next = d->d_conns;
	d->d_conns = c;
	watch(d, c, EVFILT_READ, EV_ADD);
}

/*
 * Descriptors that came with C's bytes, SCM_RIGHTS in M: three, a
 * terminal's, kept with C; any others closed.
 */
static void
taken(struct conn *c, struct msghdr *m)
{
	struct cmsghdr *cm;
	int fds[8];
	size_t i, n;

	for (cm = CMSG_FIRSTHDR(m); cm != NULL; cm = CMSG_NXTHDR(m, cm)) {
		if (cm->cmsg_level != SOL_SOCKET ||
		    cm->cmsg_type != SCM_RIGHTS)
			continue;
		n = (cm->cmsg_len - CMSG_LEN(0)) / sizeof (int);
		(void) memcpy(fds, CMSG_DATA(cm), n * sizeof (int));
		for (i = 0; i < n; i++) {
			(void) fcntl(fds[i], F_SETFD, FD_CLOEXEC);
			if (n == 3 && c->c_nfds < 3)
				c->c_fds[c->c_nfds++] = fds[i];
			else
				(void) close(fds[i]);
		}
	}
}

/*
 * More of C's request: its terminal's type, once, and its arguments.
 * Whole, it is answered, and C is read no more; a person's command
 * sends nothing else, so anything else hangs up. A reply held, an
 * attended roll's, C is read on for the command's half-close or its
 * going, which is the roll's hang-up (Q15, Q16).
 */
static void
readable(struct daemon *d, struct conn *c)
{
	char buf[4096], cbuf[CMSG_SPACE(8 * sizeof (int))];
	struct msghdr m;
	struct iovec iov;
	const char *p;
	size_t len;
	ssize_t n;
	int kind;

	(void) memset(&m, 0, sizeof (m));
	iov.iov_base = buf;
	iov.iov_len = sizeof (buf);
	m.msg_iov = &iov;
	m.msg_iovlen = 1;
	m.msg_control = cbuf;
	m.msg_controllen = sizeof (cbuf);
	if ((n = recvmsg(c->c_fd, &m, 0)) < 0 && errno == EAGAIN)
		return;
	taken(c, &m);
	if (c->c_held != NULL) {
		if (n <= 0) {
			sh_house_hangup(c->c_held);
			watch(d, c, EVFILT_READ, EV_DELETE);
		}
		return;
	}
	if (n <= 0) {
		hang_up(d, c);
		return;
	}
	sh_buf_add(&c->c_in, buf, (size_t)n);
	for (;;) {
		if (!sh_wire_get(&c->c_in, &kind, &p, &len))
			return;
		if (kind == SH_WIRE_ARGS)
			break;
		if (kind != SH_WIRE_TERM || c->c_term != NULL) {
			hang_up(d, c);
			return;
		}
		c->c_term = sh_calloc(1, len + 1);
		(void) memcpy(c->c_term, p, len);
	}
	act(d, c, p, len);
	if (c->c_held != NULL)
		return;
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
    const char *logdir, const char *jaildir, struct sh_err *e)
{
	struct daemon d;
	struct kevent ev[3];
	char path[1024];
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
	(void) snprintf(path, sizeof (path), "%s/beam.log", logdir);
	if (d.d_sp.ss_root[SH_SP_LAKE] != NULL &&
	    d.d_sp.ss_root[SH_SP_RUN] != NULL &&
	    (d.d_benv.be_log = fopen(path, "a")) == NULL) {
		sh_errf(e, "%s: %s", path, strerror(errno));
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
	d.d_env.re_made = made;
	d.d_env.re_marg = &d;
	if (d.d_sp.ss_root[SH_SP_RUN] != NULL) {
		d.d_henv.he_z = z;
		d.d_henv.he_kq = d.d_kq;
		d.d_henv.he_lake = d.d_sp.ss_root[SH_SP_LAKE];
		d.d_henv.he_run = d.d_sp.ss_root[SH_SP_RUN];
		d.d_henv.he_resolv = "/etc/resolv.conf";
		d.d_henv.he_logdir = logdir;
		d.d_henv.he_jails = jaildir;
		d.d_henv.he_err = e;
		d.d_henv.he_flags = SF_IMMUTABLE;
		d.d_henv.he_pull = pull;
		d.d_henv.he_beam = made;
		d.d_henv.he_idle = house_idle;
		d.d_henv.he_parg = &d;
		if (sh_houses_start(&d.d_henv, &d.d_houses, &d.d_nhouses,
		    e) != 0)
			goto out;
	}
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
	if (d.d_benv.be_log != NULL) {
		/* a line at a time, as a roll's */
		(void) setvbuf(d.d_benv.be_log, NULL, _IOLBF, 0);
		d.d_benv.be_z = z;
		d.d_benv.be_err = e;
		d.d_benv.be_kq = d.d_kq;
		d.d_benv.be_lake = d.d_sp.ss_root[SH_SP_LAKE];
		d.d_benv.be_run = d.d_sp.ss_root[SH_SP_RUN];
		d.d_benv.be_period = SH_BEAM_PERIOD;
		d.d_benv.be_retry = SH_BEAM_RETRY;
		d.d_benv.be_done = pruned;
		d.d_benv.be_darg = &d;
		d.d_beam = sh_beam_start(&d.d_benv);
	}
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
		else if (*(enum sh_ev *)u == SH_EV_BEAM)
			sh_beam_timer(u);
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
	/*
	 * and a stopped create: its version, not complete, destroyed; a
	 * stopped start's jail made or left by -cm; no jail stopped (Q-J1)
	 */
	sh_houses_free(d.d_houses, d.d_nhouses);
	if (d.d_beam != NULL)
		sh_beam_stop(d.d_beam);
	if (d.d_benv.be_log != NULL)
		(void) fclose(d.d_benv.be_log);
	if (d.d_kq >= 0)
		(void) close(d.d_kq);
	(void) unlink(sock);
	sh_foundations_free(d.d_found, d.d_nfound);
	free(d.d_froot);
	free(d.d_oroot);
	sh_spaces_free(&d.d_sp);
	return (r);
}
