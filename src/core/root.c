/*
 * Paths in the configuration root.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/parse.h"
#include "core/root.h"
#include "core/tree.h"

char *
sh_root_path(const char *root, const char *rel)
{
	size_t len;
	char *p;

	len = strlen(root) + 1 + strlen(rel) + 1;
	if ((p = malloc(len)) == NULL)
		return (NULL);
	(void) snprintf(p, len, "%s/%s", root, rel);
	return (p);
}

int
sh_root_kind(const char *root, const char *rel)
{
	struct stat st;
	char *p;
	int r;

	if ((p = sh_root_path(root, rel)) == NULL)
		return (0);
	r = stat(p, &st);
	free(p);
	if (r != 0)
		return (0);
	if (S_ISREG(st.st_mode))
		return ('f');
	if (S_ISDIR(st.st_mode))
		return ('d');
	return ('?');
}

struct sh_node *
sh_root_parse(const char *root, const char *name, int *missing,
    struct sh_err *e)
{
	struct sh_node *n;
	struct stat st;
	char *p;

	if (missing != NULL)
		*missing = 0;
	if ((p = sh_root_path(root, name)) == NULL) {
		sh_err_nomem(e);
		return (NULL);
	}
	if (missing != NULL && stat(p, &st) != 0 && errno == ENOENT) {
		*missing = 1;
		free(p);
		return (NULL);
	}
	n = sh_parse_file(p, name, e);
	free(p);
	return (n);
}
