/*
 * Family I on the box, as root on ZFS: the shaken recipe made schg,
 * and read back through a real snapshot's .zfs view. tests/box/run.sh
 * makes the filesystem, mounted at DIR, and its snapshot between the
 * two runs.
 *
 * usage: box_shake write DIR
 *        box_shake read DIR SNAP
 */

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "core/err.h"
#include "core/shake.h"
#include "core/tree.h"

static const char text[] = "pkg {\n    nginx = true;\n}\n";
static const char later[] = "pkg {\n    vim = true;\n}\n";
static int failed;

static void
cell(const char *id, const char *what, int good)
{
	(void) printf("%s %s %s\n", good ? "ok  " : "FAIL", id, what);
	if (!good)
		failed++;
}

static int
write_cells(const char *dir, struct sh_err *e)
{
	char path[1024], link[1024];
	struct stat st;
	int fd;

	(void) snprintf(path, sizeof (path), "%s/%s", dir, SH_SHAKEN_HOUSE);
	cell("I9", "written schg", sh_shaken_write(dir, SH_SHAKEN_HOUSE, text,
	    sizeof (text) - 1, SF_IMMUTABLE, e) == 0 &&
	    lstat(path, &st) == 0 && (st.st_flags & SF_IMMUTABLE) != 0 &&
	    (st.st_mode & 07777) == 0444);
	fd = open(path, O_WRONLY);
	cell("I10", "root may not write it", fd < 0 && errno == EPERM);
	if (fd >= 0)
		(void) close(fd);
	cell("I10", "nor unlink it", unlink(path) != 0 && errno == EPERM);
	cell("I11", "written again: replaced, schg again",
	    sh_shaken_write(dir, SH_SHAKEN_HOUSE, text, sizeof (text) - 1,
	    SF_IMMUTABLE, e) == 0 && lstat(path, &st) == 0 &&
	    (st.st_flags & SF_IMMUTABLE) != 0);
	/* for the read: a link in the tree the snapshot will hold */
	(void) snprintf(link, sizeof (link), "%s/var/lnk", dir);
	cell("I13", "a link planted in the tree", symlink("/etc", link) == 0);
	return (failed != 0);
}

static int
read_cells(const char *dir, const char *snap, struct sh_err *e)
{
	struct sh_node *n;
	char *buf = NULL;
	size_t len = 0;
	FILE *f;
	struct sh_err q;

	n = sh_shaken_read(dir, snap, SH_SHAKEN_HOUSE, "web@s1", e);
	cell("I12", "read back through the snapshot", n != NULL &&
	    sh_obj_get(sh_obj_get(n, "pkg"), "nginx") != NULL);
	sh_node_free(n);
	if ((f = open_memstream(&buf, &len)) == NULL)
		return (2);
	sh_err_init(&q, f, "");
	n = sh_shaken_read(dir, snap, "var/lnk/hosts", "x", &q);
	(void) fflush(f);
	cell("I13", "a link on the way in the snapshot: refused", n == NULL &&
	    strstr(buf, "/var/lnk: a link") != NULL);
	(void) fclose(f);
	/* I14: what the live tree says after the snapshot changes nothing */
	n = NULL;
	if (sh_shaken_write(dir, SH_SHAKEN_HOUSE, later, sizeof (later) - 1,
	    SF_IMMUTABLE, e) == 0)
		n = sh_shaken_read(dir, snap, SH_SHAKEN_HOUSE, "web@s1", e);
	cell("I14", "the live file rewritten: the snapshot's read the same",
	    n != NULL && sh_obj_get(sh_obj_get(n, "pkg"), "nginx") != NULL &&
	    sh_obj_get(sh_obj_get(n, "pkg"), "vim") == NULL);
	sh_node_free(n);
	return (failed != 0);
}

int
main(int argc, char **argv)
{
	struct sh_err e;

	sh_err_init(&e, stdout, "     heard: ");
	if (argc == 3 && strcmp(argv[1], "write") == 0)
		return (write_cells(argv[2], &e));
	if (argc == 4 && strcmp(argv[1], "read") == 0)
		return (read_cells(argv[2], argv[3], &e));
	(void) fprintf(stderr, "usage: box_shake write DIR\n"
	    "       box_shake read DIR SNAP\n");
	return (2);
}
