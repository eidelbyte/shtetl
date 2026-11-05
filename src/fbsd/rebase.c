/*
 * zfs_rebase run: see src/core/rebase.h. The program is found by the
 * daemon's PATH, which rc.d/shtetl gives /usr/local/sbin.
 */

#include <stdlib.h>
#include <string.h>

#include "core/job.h"
#include "core/mem.h"
#include "core/rebase.h"

void
sh_rebase(struct sh_job *j, const char *step, const char *const *argv)
{
	const char **w;
	size_t n;

	for (n = 0; argv[n] != NULL; n++)
		;
	w = sh_calloc(n + 2, sizeof (*w));
	w[0] = "zfs_rebase";
	(void) memcpy(w + 1, argv, n * sizeof (*w));
	sh_job_child(j, step, w);
	free(w);
}
