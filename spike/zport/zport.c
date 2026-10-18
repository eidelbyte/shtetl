/*
 * zport: the runtime spike's ZFS port (sprints/sprint-1/
 * implementation-plan.md section 14, S1).
 *
 * An Erlang port opened {packet, 4}: each request on stdin is a
 * four-byte big-endian length and one term in the external term
 * format, and each reply on stdout is the same. Names are binaries.
 * Nothing is printed and nothing but terms is read.
 *
 *	ping			pong
 *	{sync, Pool}		ok | {error, Errno}
 *	{exists, Name}		true | false
 *	{snapshot, Snap}	ok | {error, Errno}
 *	{clone, Fs, Origin}	ok | {error, Errno}
 *	{beam, From, Snap, To}	ok | {error, Errno}: Snap sent, as a full
 *				stream when From is <<>>, received as To
 *	{guid, Name}		{ok, Guid} | {error, Errno}
 *	{written, Fs, Short}	{ok, Bytes}: Fs's written@Short
 *	{snapshots, Fs}		{ok, [Snap]}, oldest first
 *
 * A request it cannot read ends it with exit 2; its owner sees the
 * exit and is started again with it. End of input ends it with 0.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libzfs.h>
#include <libzfs_core.h>

#include <ei.h>

#define	ZP_NAME	ZFS_MAX_DATASET_NAME_LEN

static libzfs_handle_t *zp_zfs;

static int
zp_read(void *buf, size_t len)
{
	char *p = buf;
	ssize_t n;

	while (len > 0) {
		n = read(STDIN_FILENO, p, len);
		if (n == -1 && errno == EINTR)
			continue;
		if (n <= 0)
			return (-1);
		p += n;
		len -= (size_t)n;
	}
	return (0);
}

static int
zp_write(const void *buf, size_t len)
{
	const char *p = buf;
	ssize_t n;

	while (len > 0) {
		n = write(STDOUT_FILENO, p, len);
		if (n == -1 && errno == EINTR)
			continue;
		if (n <= 0)
			return (-1);
		p += n;
		len -= (size_t)n;
	}
	return (0);
}

/* The binary term at *IDX into NAME, NUL-terminated. */
static int
zp_name(const char *buf, int *idx, char *name)
{
	int type, size;
	long len;

	if (ei_get_type(buf, idx, &type, &size) != 0 ||
	    type != ERL_BINARY_EXT || size >= ZP_NAME ||
	    ei_decode_binary(buf, idx, name, &len) != 0)
		return (-1);
	name[len] = '\0';
	return (0);
}

static void
zp_status(ei_x_buff *x, int err)
{
	if (err == 0) {
		(void) ei_x_encode_atom(x, "ok");
		return;
	}
	(void) ei_x_encode_tuple_header(x, 2);
	(void) ei_x_encode_atom(x, "error");
	(void) ei_x_encode_long(x, err);
}

static void
zp_value(ei_x_buff *x, int err, uint64_t v)
{
	if (err != 0) {
		zp_status(x, err);
		return;
	}
	(void) ei_x_encode_tuple_header(x, 2);
	(void) ei_x_encode_atom(x, "ok");
	(void) ei_x_encode_ulonglong(x, (unsigned long long)v);
}

/* zpool sync, force off. */
static int
zp_sync(const char *pool)
{
	nvlist_t *nv = fnvlist_alloc();
	int r;

	fnvlist_add_boolean_value(nv, "force", B_FALSE);
	r = lzc_sync(pool, nv, NULL);
	fnvlist_free(nv);
	return (r);
}

static int
zp_snapshot(const char *snap)
{
	nvlist_t *snaps = fnvlist_alloc();
	nvlist_t *errs = NULL;
	int r;

	fnvlist_add_boolean(snaps, snap);
	r = lzc_snapshot(snaps, NULL, &errs);
	fnvlist_free(snaps);
	nvlist_free(errs);
	return (r);
}

/* SNAP sent from FROM (a full stream if empty) and received as TO. */
static int
zp_beam(const char *from, const char *snap, const char *to)
{
	int fds[2], st, sr, rr;
	pid_t pid;

	if (pipe(fds) != 0)
		return (errno);
	if ((pid = fork()) == -1) {
		sr = errno;
		(void) close(fds[0]);
		(void) close(fds[1]);
		return (sr);
	}
	if (pid == 0) {
		/* a receiver that stops reads EPIPE, not a signal */
		(void) close(fds[0]);
		(void) signal(SIGPIPE, SIG_IGN);
		sr = lzc_send(snap, *from != '\0' ? from : NULL, fds[1],
		    LZC_SEND_FLAG_LARGE_BLOCK | LZC_SEND_FLAG_EMBED_DATA |
		    LZC_SEND_FLAG_COMPRESS);
		_exit(sr & 0xff);
	}
	(void) close(fds[1]);
	rr = lzc_receive(to, NULL, NULL, B_FALSE, B_FALSE, fds[0]);
	(void) close(fds[0]);
	while (waitpid(pid, &st, 0) == -1 && errno == EINTR)
		;
	sr = WIFEXITED(st) ? WEXITSTATUS(st) : EINTR;
	if (sr != 0 && sr != EPIPE)
		return (sr);
	return (rr);
}

/* Any failure to open NAME is ENOENT: to the actor it is not there. */
static int
zp_guid(const char *name, uint64_t *v)
{
	zfs_handle_t *zh;

	if ((zh = zfs_open(zp_zfs, name, ZFS_TYPE_DATASET)) == NULL)
		return (ENOENT);
	*v = zfs_prop_get_int(zh, ZFS_PROP_GUID);
	zfs_close(zh);
	return (0);
}

static int
zp_written(const char *fs, const char *shortname, uint64_t *v)
{
	char prop[ZP_NAME + 8];
	zfs_handle_t *zh;
	int r;

	(void) snprintf(prop, sizeof (prop), "written@%s", shortname);
	if ((zh = zfs_open(zp_zfs, fs, ZFS_TYPE_FILESYSTEM)) == NULL)
		return (ENOENT);
	r = zfs_prop_get_written_int(zh, prop, v);
	zfs_close(zh);
	return (r != 0 ? EIO : 0);
}

struct zp_list {
	char	**zl_names;
	size_t	zl_n;
	size_t	zl_cap;
};

static int
zp_snap_cb(zfs_handle_t *zh, void *arg)
{
	struct zp_list *l = arg;
	char **p;

	if (l->zl_n == l->zl_cap) {
		l->zl_cap = l->zl_cap != 0 ? l->zl_cap * 2 : 16;
		if ((p = realloc(l->zl_names,
		    l->zl_cap * sizeof (*p))) == NULL) {
			zfs_close(zh);
			return (-1);
		}
		l->zl_names = p;
	}
	if ((l->zl_names[l->zl_n] = strdup(zfs_get_name(zh))) == NULL) {
		zfs_close(zh);
		return (-1);
	}
	l->zl_n++;
	zfs_close(zh);
	return (0);
}

/* FS's snapshots in createtxg order, as {ok, [Snap]}. */
static void
zp_snapshots(const char *fs, ei_x_buff *x)
{
	struct zp_list l = { NULL, 0, 0 };
	zfs_handle_t *zh;
	size_t i;
	int r;

	if ((zh = zfs_open(zp_zfs, fs, ZFS_TYPE_FILESYSTEM)) == NULL) {
		zp_status(x, ENOENT);
		return;
	}
	r = zfs_iter_snapshots_sorted(zh, zp_snap_cb, &l, 0, 0);
	zfs_close(zh);
	if (r != 0) {
		zp_status(x, ENOMEM);
	} else {
		(void) ei_x_encode_tuple_header(x, 2);
		(void) ei_x_encode_atom(x, "ok");
		if (l.zl_n > 0)
			(void) ei_x_encode_list_header(x, (long)l.zl_n);
		for (i = 0; i < l.zl_n; i++)
			(void) ei_x_encode_binary(x, l.zl_names[i],
			    (int)strlen(l.zl_names[i]));
		(void) ei_x_encode_empty_list(x);
	}
	for (i = 0; i < l.zl_n; i++)
		free(l.zl_names[i]);
	free(l.zl_names);
}

static void
zp_handle(const char *buf, ei_x_buff *x)
{
	char op[MAXATOMLEN], a[ZP_NAME], b[ZP_NAME], c[ZP_NAME];
	int idx = 0, ver, start, arity;
	uint64_t v = 0;

	if (ei_decode_version(buf, &idx, &ver) != 0)
		exit(2);
	start = idx;
	if (ei_decode_atom(buf, &idx, op) == 0) {
		if (strcmp(op, "ping") != 0)
			exit(2);
		(void) ei_x_encode_atom(x, "pong");
		return;
	}
	idx = start;
	if (ei_decode_tuple_header(buf, &idx, &arity) != 0 || arity < 2 ||
	    arity > 4 || ei_decode_atom(buf, &idx, op) != 0 ||
	    zp_name(buf, &idx, a) != 0 ||
	    (arity > 2 && zp_name(buf, &idx, b) != 0) ||
	    (arity > 3 && zp_name(buf, &idx, c) != 0))
		exit(2);

	if (arity == 2 && strcmp(op, "sync") == 0)
		zp_status(x, zp_sync(a));
	else if (arity == 2 && strcmp(op, "exists") == 0)
		(void) ei_x_encode_boolean(x, lzc_exists(a));
	else if (arity == 2 && strcmp(op, "snapshot") == 0)
		zp_status(x, zp_snapshot(a));
	else if (arity == 3 && strcmp(op, "clone") == 0)
		zp_status(x, lzc_clone(a, b, NULL));
	else if (arity == 4 && strcmp(op, "beam") == 0)
		zp_status(x, zp_beam(a, b, c));
	else if (arity == 2 && strcmp(op, "guid") == 0)
		zp_value(x, zp_guid(a, &v), v);
	else if (arity == 3 && strcmp(op, "written") == 0)
		zp_value(x, zp_written(a, b, &v), v);
	else if (arity == 2 && strcmp(op, "snapshots") == 0)
		zp_snapshots(a, x);
	else
		exit(2);
}

int
main(void)
{
	unsigned char hdr[4];
	uint32_t len;
	ei_x_buff x;
	char *buf;

	if (ei_init() != 0 || libzfs_core_init() != 0 ||
	    (zp_zfs = libzfs_init()) == NULL)
		return (2);
	while (zp_read(hdr, sizeof (hdr)) == 0) {
		len = (uint32_t)hdr[0] << 24 | (uint32_t)hdr[1] << 16 |
		    (uint32_t)hdr[2] << 8 | hdr[3];
		if (len == 0 || (buf = malloc(len)) == NULL ||
		    zp_read(buf, len) != 0 || ei_x_new_with_version(&x) != 0)
			return (2);
		zp_handle(buf, &x);
		free(buf);
		len = (uint32_t)x.index;
		hdr[0] = (unsigned char)(len >> 24);
		hdr[1] = (unsigned char)(len >> 16);
		hdr[2] = (unsigned char)(len >> 8);
		hdr[3] = (unsigned char)len;
		if (zp_write(hdr, sizeof (hdr)) != 0 ||
		    zp_write(x.buff, len) != 0)
			return (2);
		(void) ei_x_free(&x);
	}
	libzfs_fini(zp_zfs);
	libzfs_core_fini();
	return (0);
}
