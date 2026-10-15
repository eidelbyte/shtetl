/*
 * tests/box/zcheck.c over the fake (tests/zfake.c), on the mac: the
 * box's cells held to what check_zfs.c holds the fake to, before a
 * box runs them against libzfs. The Makefile builds zcheck.c's main
 * as zcheck_main for it; make check runs it.
 */

#include <stddef.h>

#include "zfake.h"

int zcheck_main(int argc, char **argv);
extern int (*zcheck_put)(const char *ds);
extern void (*zcheck_tick)(void);

/* Z23's and Z24's write, the fake's: 64 KiB, not yet committed. */
static int
fake_put(const char *ds)
{
	zfake_write(ds, 65536);
	return (0);
}

int
main(void)
{
	static char prog[] = "box_zcheck", top[] = "tank/zc";
	static char other[] = "fast/zc2";
	char *argv[] = { prog, top, other, NULL };

	zfake_reset();
	zfake_pool("tank");
	zfake_fs("tank/zc");
	zfake_pool("fast");
	zfake_fs("fast/zc2");
	zcheck_put = fake_put;
	zcheck_tick = zfake_txg_sync;
	return (zcheck_main(3, argv));
}
