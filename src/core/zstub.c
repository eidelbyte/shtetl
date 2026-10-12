/*
 * The ZFS of the portable build: none. Every call is refused, so that
 * the portable shtetl is one program with the FreeBSD one and says
 * plainly why a command that needs ZFS cannot run. The tests link
 * tests/zfake.c instead.
 */

#include <stddef.h>

#include "core/err.h"
#include "core/zfs.h"

#define	ZS_NONE	"this shtetl was built without ZFS: make freebsd"

int
sh_zfs_open(struct sh_zfs **zp, struct sh_err *e)
{
	*zp = NULL;
	sh_errf(e, "%s", ZS_NONE);
	return (-1);
}

void
sh_zfs_close(struct sh_zfs *z)
{
	(void) z;
}

int
sh_zfs_local(struct sh_zfs *z, const char *prop,
    int (*fn)(void *, const char *, const char *), void *arg,
    struct sh_err *e)
{
	(void) z;
	(void) prop;
	(void) fn;
	(void) arg;
	sh_errf(e, "%s", ZS_NONE);
	return (-1);
}

int
sh_zfs_get(struct sh_zfs *z, const char *ds, const char *prop, char *val,
    size_t len, enum sh_zsrc *src, struct sh_err *e)
{
	(void) z;
	(void) ds;
	(void) prop;
	(void) val;
	(void) len;
	(void) src;
	sh_errf(e, "%s", ZS_NONE);
	return (-1);
}

int
sh_zfs_children(struct sh_zfs *z, const char *ds,
    int (*fn)(void *, const char *), void *arg, struct sh_err *e)
{
	(void) z;
	(void) ds;
	(void) fn;
	(void) arg;
	sh_errf(e, "%s", ZS_NONE);
	return (-1);
}

int
sh_zfs_create(struct sh_zfs *z, const char *ds,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	(void) z;
	(void) ds;
	(void) props;
	(void) nprops;
	sh_errf(e, "%s", ZS_NONE);
	return (-1);
}

int
sh_zfs_set(struct sh_zfs *z, const char *ds, const char *prop,
    const char *value, struct sh_err *e)
{
	(void) z;
	(void) ds;
	(void) prop;
	(void) value;
	sh_errf(e, "%s", ZS_NONE);
	return (-1);
}

int
sh_zpool_get(struct sh_zfs *z, const char *pool, const char *prop,
    char *val, size_t len, struct sh_err *e)
{
	(void) z;
	(void) pool;
	(void) prop;
	(void) val;
	(void) len;
	sh_errf(e, "%s", ZS_NONE);
	return (-1);
}

int
sh_zpool_set(struct sh_zfs *z, const char *pool, const char *prop,
    const char *value, struct sh_err *e)
{
	(void) z;
	(void) pool;
	(void) prop;
	(void) value;
	sh_errf(e, "%s", ZS_NONE);
	return (-1);
}
