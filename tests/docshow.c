/*
 * docs/configuration.md's examples, as tools/doccheck.py runs them:
 * shtetl show as the daemon runs it (sh_show), over an example's
 * configuration root, a directory, since the mac has no daemon and no
 * ZFS to find zconfig through.
 *
 * usage: docshow ROOT [RECIPE]
 */

#include <stdio.h>

#include "core/show.h"

int
main(int argc, char **argv)
{
	if (argc < 2 || argc > 3) {
		(void) fputs("usage: docshow ROOT [RECIPE]\n", stderr);
		return (2);
	}
	return (sh_show(argv[1], argc == 3 ? argv[2] : NULL, stdout,
	    stderr));
}
