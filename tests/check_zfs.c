/*
 * Family Z over the fake, in make check: tests/zcells.c's cells, the
 * box's own, held to the fake before a box holds them to libzfs. What
 * the cells print is shown only when one fails.
 */

#include "check.h"
#include "core/zfs.h"
#include "zcells.h"
#include "zfake.h"

int
main(void)
{
	static const struct sh_zprop none = { "mountpoint", "none" };
	struct sh_zfs *z;
	struct cap c;
	char *buf = NULL;
	size_t len = 0;
	FILE *o;
	int failed;

	/* the top as tests/box/run.sh makes it: no mountpoint to inherit */
	zfake_reset();
	zfake_pool("tank");
	cap_open(&c);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, "tank/zc", &none, 1, &c.c_err) == 0);
	zc_put = zfake_put;
	zc_has = zfake_has;
	CHECK((o = open_memstream(&buf, &len)) != NULL);
	failed = zcells(o, "tank/zc");
	(void) fclose(o);
	if (failed != 0)
		(void) fputs(buf, stdout);
	free(buf);
	cap_close(&c);
	return (failed != 0);
}
