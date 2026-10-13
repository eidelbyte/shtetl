/*
 * tests/box/zcheck.c over the fake (tests/zfake.c), on the mac: the
 * box's cells held to what check_zfs.c holds the fake to, before a
 * box runs them against libzfs. The Makefile builds zcheck.c's main
 * as zcheck_main for it; make check runs it.
 */

#include <stddef.h>

#include "zfake.h"

int zcheck_main(int argc, char **argv);

int
main(void)
{
	static char prog[] = "box_zcheck", top[] = "tank/zc";
	char *argv[] = { prog, top, NULL };

	zfake_reset();
	zfake_pool("tank");
	zfake_fs("tank/zc");
	return (zcheck_main(2, argv));
}
