/*
 * Writing a shaken recipe into a tree, and reading one from a
 * snapshot.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "core/err.h"
#include "core/parse.h"
#include "core/shake.h"
#include "core/tree.h"

/*
 * Why COMP of DFD did not open, the errno WHY: a link, which a
 * component opened without following one meets as ELOOP, EMLINK or
 * ENOTDIR as the system has it, is said so.
 */
static const char *
whynot(int dfd, const char *comp, int why)
{
	struct stat st;

	if (fstatat(dfd, comp, &st, AT_SYMLINK_NOFOLLOW) == 0 &&
	    S_ISLNK(st.st_mode))
		return ("a link");
	return (strerror(why));
}

/*
 * The directory holding REL's last component, opened from DFD (which
 * is closed) one component at a time, never through a link, each made
 * 0755 first when MAKE; its descriptor, with *LAST REL's last
 * component. -1 when refused, said: "PATH: a link", or the system's
 * words.
 */
static int
beneath(int dfd, const char *rel, int make, const char **last,
    struct sh_err *e)
{
	const char *p = rel, *slash;
	char comp[256];
	int fd;

	while ((slash = strchr(p, '/')) != NULL) {
		(void) snprintf(comp, sizeof (comp), "%.*s", (int)(slash - p),
		    p);
		if (make && mkdirat(dfd, comp, 0755) != 0 && errno != EEXIST)
			fd = -1;
		else
			fd = openat(dfd, comp, O_RDONLY | O_DIRECTORY |
			    O_NOFOLLOW | O_CLOEXEC);
		if (fd < 0)
			sh_errf(e, "%.*s: %s", (int)(slash - rel), rel,
			    whynot(dfd, comp, errno));
		(void) close(dfd);
		if (fd < 0)
			return (-1);
		dfd = fd;
		p = slash + 1;
	}
	*last = p;
	return (dfd);
}

int
sh_shaken_write(const char *dir, const char *rel, const char *text,
    size_t len, unsigned long flags, struct sh_err *e)
{
	const char *last;
	ssize_t n;
	size_t off;
	int dfd, fd, r = -1;

	if ((dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) < 0) {
		sh_errf(e, "%s: %s", dir, strerror(errno));
		return (-1);
	}
	if ((dfd = beneath(dfd, rel, 1, &last, e)) < 0)
		return (-1);
	/* what is there goes: a file's flags cleared, a link only a name */
	if ((fd = openat(dfd, last, O_RDONLY | O_NOFOLLOW | O_CLOEXEC)) >= 0) {
		(void) fchflags(fd, 0);
		(void) close(fd);
	}
	fd = -1;
	if (unlinkat(dfd, last, 0) != 0 && errno != ENOENT)
		goto fail;
	if ((fd = openat(dfd, last, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW |
	    O_CLOEXEC, 0444)) < 0)
		goto fail;
	for (off = 0; off < len; off += (size_t)n) {
		if ((n = write(fd, text + off, len - off)) <= 0)
			goto fail;
	}
	if (fchmod(fd, 0444) != 0 || (flags != 0 && fchflags(fd, flags) != 0))
		goto fail;
	r = 0;
	goto out;
fail:
	sh_errf(e, "%s: %s", rel, strerror(errno));
out:
	if (fd >= 0)
		(void) close(fd);
	(void) close(dfd);
	return (r);
}

struct sh_node *
sh_shaken_read(const char *dir, const char *snap, const char *rel,
    const char *name, struct sh_err *e)
{
	const char *last;
	char path[1024];
	int dfd, fd;

	(void) snprintf(path, sizeof (path), ".zfs/snapshot/%s/%s", snap, rel);
	if ((dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) < 0) {
		sh_errf(e, "%s: %s", dir, strerror(errno));
		return (NULL);
	}
	if ((dfd = beneath(dfd, path, 0, &last, e)) < 0)
		return (NULL);
	if ((fd = openat(dfd, last, O_RDONLY | O_NOFOLLOW | O_CLOEXEC)) < 0)
		sh_errf(e, "%s: %s", path, whynot(dfd, last, errno));
	(void) close(dfd);
	if (fd < 0)
		return (NULL);
	return (sh_parse_fd(fd, name, e));
}
