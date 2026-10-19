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
	return (zcells(stdout, argv[1]) != 0);
}
