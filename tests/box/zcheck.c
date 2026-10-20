/*
 * Family Z on the box: tests/zcells.c's cells over libzfs, on the pool
 * tests/box/run.sh makes. A word is a file in the mounted filesystem.
 *
 * usage: box_zcheck POOL/DATASET   (empty, with no mountpoint to
 *        inherit; run.sh makes it)
 */

#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "core/err.h"
#include "core/zfs.h"
#include "zcells.h"

static struct sh_zfs *z;
static struct sh_err e;

/* WORD's path in DS where it is mounted, into PATH: 0, or -1. */
static int
wpath(const char *ds, const char *word, char *path, size_t len)
{
	char at[1024];

	if (sh_zfs_mounted(z, ds, at, sizeof (at), &e) != 1)
		return (-1);
	(void) snprintf(path, len, "%s/%s", at, word);
	return (0);
}

static int
put(const char *ds, const char *word)
{
	char p[1100];
	int fd;

	if (wpath(ds, word, p, sizeof (p)) != 0 ||
	    (fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
		return (-1);
	return (close(fd));
}

static int
has(const char *ds, const char *word)
{
	struct stat st;
	char p[1100];

	if (wpath(ds, word, p, sizeof (p)) != 0)
		return (-1);
	return (stat(p, &st) == 0);
}

/* 8 MiB of /dev/random in a file of DS: more than a cut lets through. */
static int
fill(const char *ds)
{
	static int n;
	char p[1100], name[32], buf[65536];
	int in, out, i, r = 0;

	(void) snprintf(name, sizeof (name), "fill-%d", n++);
	if (wpath(ds, name, p, sizeof (p)) != 0 ||
	    (in = open("/dev/random", O_RDONLY)) < 0)
		return (-1);
	if ((out = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
		(void) close(in);
		return (-1);
	}
	for (i = 0; r == 0 && i < 128; i++)
		r = read(in, buf, sizeof (buf)) == (ssize_t)sizeof (buf) &&
		    write(out, buf, sizeof (buf)) == (ssize_t)sizeof (buf) ?
		    0 : -1;
	(void) close(in);
	return (close(out) != 0 ? -1 : r);
}

/*
 * The stream cut as probe P1 cuts it: zfs send into head -c, a
 * mebibyte let through, into zfs receive -s. Its failure is the cut.
 */
static int
cut(const char *snap, const char *from, const char *to, const char *origin)
{
	char cmd[2048];

	(void) snprintf(cmd, sizeof (cmd), "zfs send %s%s %s | head -c 1048576 "
	    "| zfs receive -s -u %s%s %s 2>/dev/null", from != NULL ? "-i " :
	    "", from != NULL ? from : "", snap, origin != NULL ? "-o origin=" :
	    "", origin != NULL ? origin : "", to);
	(void) system(cmd);
	return (0);
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		(void) fprintf(stderr, "usage: box_zcheck POOL/DATASET\n");
		return (2);
	}
	sh_err_init(&e, stderr, "box_zcheck: ");
	if (sh_zfs_open(&z, &e) != 0)
		return (2);
	zc_put = put;
	zc_has = has;
	zc_fill = fill;
	zc_cut = cut;
	return (zcells(stdout, argv[1]) != 0);
}
