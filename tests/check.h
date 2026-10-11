/*
 * What every unit test shares: CHECK, an error sink caught in memory
 * so a test reads exactly what a person would, and a scratch
 * directory for files.
 */

#ifndef	SH_CHECK_H
#define	SH_CHECK_H

#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/err.h"

static int checks;

#define	CHECK(x)							\
	do {								\
		checks++;						\
		if (!(x)) {						\
			printf("%s:%d: check failed: %s\n", __FILE__,	\
			    __LINE__, #x);				\
			exit(1);					\
		}							\
	} while (0)

/* An error sink whose messages land in memory. */
struct cap {
	struct sh_err	c_err;
	FILE		*c_f;
	char		*c_buf;
	size_t		c_len;
};

static inline void
cap_open(struct cap *c)
{
	c->c_buf = NULL;
	c->c_len = 0;
	c->c_f = open_memstream(&c->c_buf, &c->c_len);
	if (c->c_f == NULL) {
		printf("open_memstream failed\n");
		exit(1);
	}
	sh_err_init(&c->c_err, c->c_f, "");
}

/* Everything reported so far. */
static inline const char *
cap_text(struct cap *c)
{
	(void) fflush(c->c_f);
	return (c->c_buf != NULL ? c->c_buf : "");
}

static inline void
cap_close(struct cap *c)
{
	(void) fclose(c->c_f);
	free(c->c_buf);
}

/* Forget what was reported and start counting again. */
static inline void
cap_reset(struct cap *c)
{
	cap_close(c);
	cap_open(c);
}

/* Whether the messages so far hold S. */
static inline int
cap_has(struct cap *c, const char *s)
{
	return (strstr(cap_text(c), s) != NULL);
}

/*
 * A scratch directory under TMPDIR, made on first use; scratch_path
 * names a file in it (one static buffer, so use it at once).
 */
static char scratch_dir[1024];

static inline const char *
scratch_path(const char *name)
{
	static char buf[2048];
	const char *t;

	if (scratch_dir[0] == '\0') {
		t = getenv("TMPDIR");
		(void) snprintf(scratch_dir, sizeof (scratch_dir),
		    "%s/shtetl-check.XXXXXX", t != NULL ? t : "/tmp");
		if (mkdtemp(scratch_dir) == NULL) {
			printf("mkdtemp %s failed\n", scratch_dir);
			exit(1);
		}
	}
	(void) snprintf(buf, sizeof (buf), "%s/%s", scratch_dir, name);
	return (buf);
}

/* Writes TEXT to the scratch file NAME, making its directories. */
static inline const char *
scratch_write(const char *name, const char *text)
{
	char dir[2048], *slash;
	const char *path;
	FILE *f;

	path = scratch_path(name);
	(void) snprintf(dir, sizeof (dir), "%s", path);
	for (slash = dir + strlen(scratch_dir) + 1;
	    (slash = strchr(slash, '/')) != NULL; slash++) {
		*slash = '\0';
		(void) mkdir(dir, 0755);
		*slash = '/';
	}
	if ((f = fopen(path, "w")) == NULL ||
	    fwrite(text, 1, strlen(text), f) != strlen(text) ||
	    fclose(f) != 0) {
		printf("cannot write %s\n", path);
		exit(1);
	}
	return (path);
}

/* Removes the scratch directory and everything in it. */
static inline void
scratch_clean(void)
{
	char cmd[1200];

	if (scratch_dir[0] == '\0')
		return;
	(void) snprintf(cmd, sizeof (cmd), "rm -rf '%s'", scratch_dir);
	(void) system(cmd);
}

#endif	/* SH_CHECK_H */
