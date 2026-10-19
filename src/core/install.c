/*
 * A foundation's build and install: the configs shtetl writes, the
 * make steps, and what the steps between them do in the daemon, the
 * kernel's sync and installworld's backups.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/cascade.h"
#include "core/err.h"
#include "core/install.h"
#include "core/job.h"
#include "core/mem.h"
#include "core/root.h"
#include "core/tree.h"

#define	INST_WORDS	16	/* more than any make here is given */

/* The directories installkernel makes whole. */
static const char *const inst_kdirs[] = {
	"boot/kernel", "usr/lib/debug/boot/kernel", NULL
};

static int
inst_err(struct sh_err *e, const char *path)
{
	sh_errf(e, "%s: %s", path, strerror(errno));
	return (-1);
}

static int
inst_dot(const char *name)
{
	return (strcmp(name, ".") == 0 || strcmp(name, "..") == 0);
}

/* A and B joined, allocated: "A/B", or the one that is not "". */
static char *
inst_join(const char *a, const char *b)
{
	if (a[0] == '\0' || b[0] == '\0')
		return (sh_strdup(a[0] != '\0' ? a : b));
	return (sh_root_path(a, b));
}

/* A and B, allocated. */
static char *
inst_cat(const char *a, const char *b)
{
	size_t n = strlen(a) + strlen(b) + 1;
	char *s = sh_calloc(1, n);

	(void) snprintf(s, n, "%s%s", a, b);
	return (s);
}

int
sh_install_rmtree(const char *path, struct sh_err *e)
{
	struct dirent *de;
	struct stat st;
	char *sub;
	DIR *d;
	int r = 0;

	if (lstat(path, &st) != 0)
		return (errno == ENOENT ? 0 : inst_err(e, path));
	/* immutable or undeletable first, as installworld leaves some */
	if (st.st_flags != 0 && lchflags(path, 0) != 0)
		return (inst_err(e, path));
	if (!S_ISDIR(st.st_mode))
		return (unlink(path) == 0 ? 0 : inst_err(e, path));
	if ((d = opendir(path)) == NULL)
		return (inst_err(e, path));
	while (r == 0 && (de = readdir(d)) != NULL) {
		if (inst_dot(de->d_name))
			continue;
		sub = sh_root_path(path, de->d_name);
		r = sh_install_rmtree(sub, e);
		free(sub);
	}
	(void) closedir(d);
	if (r == 0 && rmdir(path) != 0)
		r = inst_err(e, path);
	return (r);
}

/* Whether the regular files A and B, SIZE bytes each, hold the same. */
static int
inst_same(const char *a, const char *b, off_t size)
{
	char x[16384], y[16384];
	ssize_t na, nb;
	int fa, fb, same = 1;

	if ((fa = open(a, O_RDONLY | O_CLOEXEC)) < 0)
		return (0);
	if ((fb = open(b, O_RDONLY | O_CLOEXEC)) < 0) {
		(void) close(fa);
		return (0);
	}
	while (same && size > 0) {
		na = read(fa, x, sizeof (x));
		nb = read(fb, y, sizeof (y));
		same = na > 0 && na == nb && memcmp(x, y, (size_t)na) == 0;
		size -= na;
	}
	(void) close(fa);
	(void) close(fb);
	return (same);
}

/* SRC, its lstat SS, copied to DST, which is not there, mode and owner. */
static int
inst_copy(const char *src, const char *dst, const struct stat *ss,
    struct sh_err *e)
{
	char buf[16384];
	ssize_t n;
	int in, out, r = 0;

	if ((in = open(src, O_RDONLY | O_CLOEXEC)) < 0)
		return (inst_err(e, src));
	if ((out = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
	    0600)) < 0) {
		(void) close(in);
		return (inst_err(e, dst));
	}
	while (r == 0 && (n = read(in, buf, sizeof (buf))) > 0)
		r = write(out, buf, (size_t)n) == n ? 0 : -1;
	if (n < 0 || r != 0 || fchown(out, ss->st_uid, ss->st_gid) != 0 ||
	    fchmod(out, ss->st_mode & 07777) != 0)
		r = inst_err(e, dst);
	(void) close(in);
	if (close(out) != 0 && r == 0)
		r = inst_err(e, dst);
	return (r);
}

/*
 * SRC, its lstat SS, put at DST as install -C would: a file alike left
 * as it is, its mode and owner set; anything else replaced by a copy.
 */
static int
inst_put(const char *src, const char *dst, const struct stat *ss,
    struct sh_err *e)
{
	char to[1024], at[1024];
	struct stat ds;
	ssize_t n, m;

	if (S_ISLNK(ss->st_mode)) {
		if ((n = readlink(src, to, sizeof (to) - 1)) < 0)
			return (inst_err(e, src));
		m = readlink(dst, at, sizeof (at) - 1);
		if (m == n && memcmp(to, at, (size_t)n) == 0)
			return (0);
		to[n] = '\0';
		if (sh_install_rmtree(dst, e) != 0)
			return (-1);
		if (symlink(to, dst) != 0)
			return (inst_err(e, dst));
		return (lchown(dst, ss->st_uid, ss->st_gid) == 0 ? 0 :
		    inst_err(e, dst));
	}
	if (!S_ISREG(ss->st_mode))
		return (0);
	if (lstat(dst, &ds) == 0 && S_ISREG(ds.st_mode) &&
	    ds.st_size == ss->st_size && inst_same(src, dst, ss->st_size)) {
		if (((ds.st_mode ^ ss->st_mode) & 07777) != 0 &&
		    chmod(dst, ss->st_mode & 07777) != 0)
			return (inst_err(e, dst));
		if ((ds.st_uid != ss->st_uid || ds.st_gid != ss->st_gid) &&
		    chown(dst, ss->st_uid, ss->st_gid) != 0)
			return (inst_err(e, dst));
		return (0);
	}
	if (sh_install_rmtree(dst, e) != 0)
		return (-1);
	return (inst_copy(src, dst, ss, e));
}

/* Whether REL is one of the kernel's directories, or below one. */
static int
inst_kernel_dir(const char *rel)
{
	size_t i, n;

	for (i = 0; inst_kdirs[i] != NULL; i++) {
		n = strlen(inst_kdirs[i]);
		if (strncmp(rel, inst_kdirs[i], n) == 0 &&
		    (rel[n] == '\0' || rel[n] == '/'))
			return (1);
	}
	return (0);
}

/*
 * FROM's REL put at TO's: each file by inst_put, each directory in
 * turn; in a kernel directory, what FROM's has not removed.
 */
static int
inst_sync(const char *from, const char *to, const char *rel,
    struct sh_err *e)
{
	char *s = inst_join(from, rel), *d = inst_join(to, rel);
	char *sub = NULL, *a = NULL, *z = NULL;
	struct dirent *de;
	struct stat ss, ds;
	DIR *dir = NULL;
	int r = -1;

	if (lstat(s, &ss) != 0) {
		(void) inst_err(e, s);
		goto out;
	}
	if (lstat(d, &ds) == 0 && !S_ISDIR(ds.st_mode) &&
	    sh_install_rmtree(d, e) != 0)
		goto out;
	if (lstat(d, &ds) != 0 && (mkdir(d, ss.st_mode & 07777) != 0 ||
	    chown(d, ss.st_uid, ss.st_gid) != 0)) {
		(void) inst_err(e, d);
		goto out;
	}
	if ((dir = opendir(s)) == NULL) {
		(void) inst_err(e, s);
		goto out;
	}
	r = 0;
	while (r == 0 && (de = readdir(dir)) != NULL) {
		if (inst_dot(de->d_name))
			continue;
		sub = inst_join(rel, de->d_name);
		a = sh_root_path(s, de->d_name);
		z = sh_root_path(d, de->d_name);
		if (lstat(a, &ss) != 0)
			r = inst_err(e, a);
		else if (S_ISDIR(ss.st_mode))
			r = inst_sync(from, to, sub, e);
		else
			r = inst_put(a, z, &ss, e);
		free(sub);
		free(a);
		free(z);
	}
	(void) closedir(dir);
	if (r != 0 || !inst_kernel_dir(rel) || (dir = opendir(d)) == NULL)
		goto out;
	while (r == 0 && (de = readdir(dir)) != NULL) {
		if (inst_dot(de->d_name))
			continue;
		a = sh_root_path(s, de->d_name);
		z = sh_root_path(d, de->d_name);
		if (lstat(a, &ss) != 0)
			r = sh_install_rmtree(z, e);
		free(a);
		free(z);
	}
	(void) closedir(dir);
out:
	free(s);
	free(d);
	return (r);
}

int
sh_install_kernel(const char *from, const char *head, struct sh_err *e)
{
	struct stat st;
	char *path;
	size_t i;
	int r;

	if (inst_sync(from, head, "", e) != 0)
		return (-1);
	/* a kernel directory the new kernel has none of: gone */
	for (i = 0, r = 0; r == 0 && inst_kdirs[i] != NULL; i++) {
		path = sh_root_path(from, inst_kdirs[i]);
		if (lstat(path, &st) != 0) {
			free(path);
			path = sh_root_path(head, inst_kdirs[i]);
			r = sh_install_rmtree(path, e);
		}
		free(path);
	}
	return (r);
}

int
sh_install_backups(const char *head, struct sh_err *e)
{
	static const struct {
		const char	*bk_dir;
		const char	*bk_pre;	/* the name begins so */
		const char	*bk_suf;	/* and ends so, or is PRE */
	} bk[] = {
		{ "boot", "", ".old" },
		{ "libexec", "ld-elf", ".so.1.old" },
		{ "sbin", "init.bak", NULL }
	};
	struct dirent **names;
	const char *s;
	char *dir, *path;
	size_t i, lp, ls, n;
	int k, nn, r = 0;

	for (i = 0; r == 0 && i < sizeof (bk) / sizeof (bk[0]); i++) {
		dir = sh_root_path(head, bk[i].bk_dir);
		/* a directory not there holds no backup */
		if ((nn = scandir(dir, &names, NULL, NULL)) < 0) {
			free(dir);
			continue;
		}
		for (k = 0; k < nn; k++) {
			s = names[k]->d_name;
			n = strlen(s);
			lp = strlen(bk[i].bk_pre);
			ls = bk[i].bk_suf != NULL ? strlen(bk[i].bk_suf) : 0;
			/* .old alone is no backup; ld-elf.so.1.old is one */
			if (r == 0 && !inst_dot(s) && (bk[i].bk_suf == NULL ?
			    strcmp(s, bk[i].bk_pre) == 0 : n > ls &&
			    n >= lp + ls && strncmp(s, bk[i].bk_pre, lp) == 0 &&
			    strcmp(s + n - ls, bk[i].bk_suf) == 0)) {
				path = sh_root_path(dir, s);
				r = sh_install_rmtree(path, e);
				free(path);
			}
			free(names[k]);
		}
		free(names);
		free(dir);
	}
	return (r);
}

/* The kernel installed into the scratch directory synced into the head. */
static int
inst_kernel(struct sh_job *j, void *arg, struct sh_err *e)
{
	const struct sh_install *in = arg;
	char *scratch = sh_root_path(in->in_obj, "kernel-install");
	int r;

	(void) j;
	r = sh_install_kernel(scratch, in->in_head, e);
	if (r == 0)
		r = sh_install_rmtree(scratch, e);
	free(scratch);
	return (r);
}

static int
inst_backups(struct sh_job *j, void *arg, struct sh_err *e)
{
	const struct sh_install *in = arg;

	(void) j;
	return (sh_install_backups(in->in_head, e));
}

/* The pieces of the text T, an object of them, each ending a line. */
static void
inst_text(FILE *out, const struct sh_node *t)
{
	const char *v;
	size_t i;

	for (i = 0; t != NULL && i < t->sn_obj.ob_n; i++) {
		v = t->sn_obj.ob_v[i].pr_val->sn_str;
		(void) fputs(v, out);
		if (v[0] != '\0' && v[strlen(v) - 1] != '\n')
			(void) fputc('\n', out);
	}
}

/* TEXT, LEN bytes, written as the file PATH. */
static int
inst_write(const char *path, const char *text, size_t len,
    struct sh_err *e)
{
	FILE *f;
	int bad;

	if ((f = fopen(path, "w")) == NULL)
		return (inst_err(e, path));
	bad = fwrite(text, 1, len, f) != len;
	if (fclose(f) != 0 || bad)
		return (inst_err(e, path));
	return (0);
}

/* The kernel config IDENT and src.conf, written into DIR. */
static int
inst_configs(const struct sh_install *in, const char *dir,
    const char *ident, struct sh_err *e)
{
	const struct sh_node *kc, *file;
	struct utsname u;
	char *buf = NULL, *inc, *rel, *path;
	size_t len = 0;
	FILE *f;
	int r;

	if (mkdir(dir, 0755) != 0 && errno != EEXIST)
		return (inst_err(e, dir));
	if (uname(&u) != 0)
		return (inst_err(e, "uname"));
	kc = sh_obj_get(in->in_recipe->sr_merged, "kernconf");
	file = sh_obj_get(kc, "file");
	if (file != NULL && strchr(file->sn_str, '/') != NULL) {
		rel = sh_recipe_rel(in->in_recipe, file->sn_from,
		    file->sn_str);
		inc = sh_root_path(in->in_config, rel);
		free(rel);
	} else {
		len = strlen(in->in_src) + strlen(u.machine) + 64 +
		    (file != NULL ? strlen(file->sn_str) : 0);
		inc = sh_calloc(1, len);
		(void) snprintf(inc, len, "%s/sys/%s/conf/%s", in->in_src,
		    u.machine, file != NULL ? file->sn_str : "GENERIC");
	}
	f = sh_memstream(&buf, &len);
	(void) fprintf(f, "# written by shtetl for foundation %s\n"
	    "include \"%s\"\nident %s\n", in->in_name, inc, ident);
	inst_text(f, sh_obj_get(kc, "lines"));
	(void) fclose(f);
	free(inc);
	path = sh_root_path(dir, ident);
	r = inst_write(path, buf, len, e);
	free(path);
	free(buf);
	buf = NULL;
	f = sh_memstream(&buf, &len);
	inst_text(f, sh_obj_get(in->in_recipe->sr_merged, "buildopts"));
	(void) fclose(f);
	path = sh_root_path(dir, "src.conf");
	if (r == 0)
		r = inst_write(path, buf, len, e);
	free(path);
	free(buf);
	return (r);
}

/*
 * A make of J, the step STEP: env(1) giving it MAKEOBJDIRPREFIX
 * (ALL[0]), make -C the checkout, the words after STEP to a NULL, and
 * the rest of ALL, what every make here takes.
 */
static void
inst_make(struct sh_job *j, const struct sh_install *in,
    char *const *all, const char *step, ...)
{
	const char *argv[INST_WORDS];
	va_list ap;
	size_t n = 0;

	argv[n++] = "env";
	argv[n++] = all[0];
	argv[n++] = "make";
	argv[n++] = "-C";
	argv[n++] = in->in_src;
	va_start(ap, step);
	while ((argv[n] = va_arg(ap, const char *)) != NULL)
		n++;
	va_end(ap);
	for (all++; *all != NULL; all++)
		argv[n++] = *all;
	argv[n] = NULL;
	sh_job_child(j, step, argv);
}

int
sh_install_steps(struct sh_job *j, struct sh_install *in, struct sh_err *e)
{
	char *dir, *scratch, *all[5], *dest, *sdest, jobs[32], ident[64];
	size_t i;
	long nj;
	int r;

	for (i = 0; in->in_name[i] != '\0' && i + 1 < sizeof (ident); i++)
		ident[i] = (in->in_name[i] >= 'a' && in->in_name[i] <= 'z') ?
		    in->in_name[i] - 'a' + 'A' : in->in_name[i];
	ident[i] = '\0';
	dir = sh_root_path(in->in_obj, "shtetl");
	scratch = sh_root_path(in->in_obj, "kernel-install");
	/* the scratch directory empty, for installkernel to fill */
	if ((r = inst_configs(in, dir, ident, e)) == 0)
		r = sh_install_rmtree(scratch, e);
	if (r == 0 && mkdir(scratch, 0755) != 0)
		r = inst_err(e, scratch);
	if (r != 0) {
		free(dir);
		free(scratch);
		return (-1);
	}
	nj = in->in_jobs > 0 ? in->in_jobs : sysconf(_SC_NPROCESSORS_ONLN);
	(void) snprintf(jobs, sizeof (jobs), "-j%ld", nj > 0 ? nj : 1);
	all[0] = inst_cat("MAKEOBJDIRPREFIX=", in->in_obj);
	dest = sh_root_path(dir, "src.conf");
	all[1] = inst_cat("SRCCONF=", dest);
	free(dest);
	all[2] = inst_cat("KERNCONFDIR=", dir);
	all[3] = inst_cat("KERNCONF=", ident);
	all[4] = NULL;
	dest = inst_cat("DESTDIR=", in->in_head);
	sdest = inst_cat("DESTDIR=", scratch);
	inst_make(j, in, all, "build", jobs, "buildworld", "buildkernel",
	    NULL);
	if (in->in_update)
		inst_make(j, in, all, "delete-old", "delete-old",
		    "delete-old-libs", dest, "BATCH_DELETE_OLD_FILES=yes",
		    NULL);
	inst_make(j, in, all, "installworld", "installworld", dest,
	    "INSTALL_CMD=install -C", NULL);
	inst_make(j, in, all, "installkernel", "installkernel", sdest, NULL);
	sh_job_call(j, "kernel", inst_kernel, in);
	inst_make(j, in, all, "distribution", "distribution", dest, NULL);
	if (in->in_update)
		sh_job_call(j, "backups", inst_backups, in);
	for (i = 0; i < 4; i++)
		free(all[i]);
	free(dest);
	free(sdest);
	free(dir);
	free(scratch);
	return (0);
}
