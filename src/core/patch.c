/*
 * A foundation's patches (planning.md 7.1.1): the pieces, each alone.
 *
 * Decided here, beyond the plan (ruled 2026-10-15): a fix has left something
 * while a file the patch touches holds a line beginning with seven of
 * <, | or > followed by a space or the line's end (git's markers;
 * ======= alone is too common in text to tell), or has a .rej beside
 * it. The pause record is UCL, read with shtetl's own parse.
 */

#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/parse.h"
#include "core/patch.h"
#include "core/syntax.h"
#include "core/tree.h"

static int
pt_digit(char c)
{
	return (c >= '0' && c <= '9');
}

static int
pt_nat(const char *a, const char *b)
{
	const char *x, *y;
	size_t lx, ly;
	int c;

	while (*a != '\0' && *b != '\0') {
		if (pt_digit(*a) && pt_digit(*b)) {
			/* leading zeros aside, the longer run is larger */
			for (x = a; *x == '0'; x++)
				;
			for (y = b; *y == '0'; y++)
				;
			for (lx = 0; pt_digit(x[lx]); lx++)
				;
			for (ly = 0; pt_digit(y[ly]); ly++)
				;
			if (lx != ly)
				return (lx < ly ? -1 : 1);
			if ((c = memcmp(x, y, lx)) != 0)
				return (c < 0 ? -1 : 1);
			a = x + lx;
			b = y + ly;
			continue;
		}
		if (*a != *b)
			return ((unsigned char)*a < (unsigned char)*b ? -1 : 1);
		a++;
		b++;
	}
	if (*a == *b)
		return (0);
	return (*a == '\0' ? -1 : 1);
}

int
sh_natcmp(const char *a, const char *b)
{
	int c;

	if ((c = pt_nat(a, b)) != 0)
		return (c);
	c = strcmp(a, b);
	return (c < 0 ? -1 : c > 0);
}

static int
pt_cmp(const void *a, const void *b)
{
	return (sh_natcmp(*(char *const *)a, *(char *const *)b));
}

void
sh_patch_list_free(char **list)
{
	size_t i;

	for (i = 0; list != NULL && list[i] != NULL; i++)
		free(list[i]);
	free(list);
}

char **
sh_patch_list(const char *dir, struct sh_err *e)
{
	char path[SH_PATCH_STR * 2], **list, **nl;
	struct dirent *de;
	struct stat st;
	size_t n, len;
	DIR *d;

	if ((d = opendir(dir)) == NULL) {
		sh_errf(e, "%s: %s", dir, strerror(errno));
		return (NULL);
	}
	if ((list = calloc(1, sizeof (*list))) == NULL)
		goto nomem;
	n = 0;
	while ((de = readdir(d)) != NULL) {
		len = strlen(de->d_name);
		if (de->d_name[0] == '.' || len <= 6 ||
		    strcmp(de->d_name + len - 6, ".patch") != 0)
			continue;
		(void) snprintf(path, sizeof (path), "%s/%s", dir, de->d_name);
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		if ((nl = realloc(list, (n + 2) * sizeof (*nl))) == NULL)
			goto nomem;
		list = nl;
		if ((list[n] = strdup(de->d_name)) == NULL)
			goto nomem;
		list[++n] = NULL;
	}
	(void) closedir(d);
	qsort(list, n, sizeof (*list), pt_cmp);
	return (list);
nomem:
	(void) closedir(d);
	sh_patch_list_free(list);
	sh_err_nomem(e);
	return (NULL);
}

const char *
sh_patch_url(const char *loc, char *buf, size_t len)
{
	enum sh_loc kind;

	if (sh_syn_location(loc, &kind) != NULL || kind == SH_LOC_DIR)
		return (NULL);
	(void) snprintf(buf, len, "%s%s", kind == SH_LOC_HOST ? "https://" :
	    "", loc);
	return (buf);
}

/*
 * git apply --numstat -z: per file "added TAB deleted TAB path NUL",
 * or for a rename "added TAB deleted TAB NUL old NUL new NUL".
 */
char **
sh_patch_files(const char *out, size_t len, struct sh_err *e)
{
	const char *p, *end, *t;
	char **list, **nl;
	size_t n;
	int tabs;

	if ((list = calloc(1, sizeof (*list))) == NULL)
		goto nomem;
	n = 0;
	p = out;
	end = out + len;
	while (p < end) {
		for (t = p, tabs = 0; t < end && tabs < 2; t++) {
			if (*t == '\t')
				tabs++;
			else if (*t == '\0')
				break;
		}
		if (tabs != 2 || t >= end)
			goto bad;
		if (*t == '\0') {
			/* a rename: the old name, then the new */
			if (t + 1 >= end ||
			    memchr(t + 1, '\0', (size_t)(end - t - 1)) == NULL)
				goto bad;
			t += strlen(t + 1) + 2;
			if (t >= end)
				goto bad;
		}
		if (memchr(t, '\0', (size_t)(end - t)) == NULL)
			goto bad;
		if ((nl = realloc(list, (n + 2) * sizeof (*nl))) == NULL)
			goto nomem;
		list = nl;
		if ((list[n] = strdup(t)) == NULL)
			goto nomem;
		list[++n] = NULL;
		p = t + strlen(t) + 1;
	}
	return (list);
bad:
	sh_errf(e, "git apply --numstat: unexpected output");
	sh_patch_list_free(list);
	return (NULL);
nomem:
	sh_patch_list_free(list);
	sh_err_nomem(e);
	return (NULL);
}

/* Whether LINE begins with a conflict marker of git's. */
static int
pt_marker(const char *line)
{
	int i;

	if (line[0] != '<' && line[0] != '|' && line[0] != '>')
		return (0);
	for (i = 1; i < 7; i++) {
		if (line[i] != line[0])
			return (0);
	}
	return (line[7] == ' ' || line[7] == '\n' || line[7] == '\0');
}

int
sh_patch_left(const char *dir, char *const *files, struct sh_err *e)
{
	char path[SH_PATCH_STR * 2], line[256];
	struct stat st;
	size_t i;
	FILE *f;
	int bol, found;

	for (i = 0; files[i] != NULL; i++) {
		(void) snprintf(path, sizeof (path), "%s/%s.rej", dir,
		    files[i]);
		if (stat(path, &st) == 0) {
			if (e != NULL)
				sh_errf(e, "%s.rej left", files[i]);
			return (1);
		}
		(void) snprintf(path, sizeof (path), "%s/%s", dir, files[i]);
		if ((f = fopen(path, "r")) == NULL)
			continue;
		/* only a line's start can be a marker: long lines span reads */
		bol = 1;
		found = 0;
		while (!found && fgets(line, sizeof (line), f) != NULL) {
			found = bol && pt_marker(line);
			bol = strchr(line, '\n') != NULL;
		}
		(void) fclose(f);
		if (found) {
			if (e != NULL)
				sh_errf(e, "%s: conflict marker left",
				    files[i]);
			return (1);
		}
	}
	return (0);
}

const char *const sh_pout_name[SH_PO_N] = { "applied", "already",
	"skipped", "fixed", "merged" };

/* S quoted for UCL, to F. */
static void
pt_quote(FILE *f, const char *s)
{
	const char *esc;
	char buf[8];

	(void) fputc('"', f);
	for (; *s != '\0'; s++) {
		if ((esc = sh_escape((unsigned char)*s, buf)) != NULL)
			(void) fputs(esc, f);
		else
			(void) fputc(*s, f);
	}
	(void) fputc('"', f);
}

int
sh_pause_write(const char *path, const struct sh_pause *pz,
    struct sh_err *e)
{
	static const char *const key[] = { "config", "version", "commit",
	    "message", "base" };
	const char *val[5];
	size_t i;
	FILE *f;

	val[0] = pz->pz_config;
	val[1] = pz->pz_version;
	val[2] = pz->pz_sha;
	val[3] = pz->pz_msg;
	val[4] = pz->pz_base;
	if ((f = fopen(path, "w")) == NULL) {
		sh_errf(e, "%s: %s", path, strerror(errno));
		return (-1);
	}
	(void) fprintf(f, "# a paused build: shtetl build F --continue, "
	    "--skip or --abort reads it\n");
	for (i = 0; i < 5; i++) {
		(void) fprintf(f, "%s = ", key[i]);
		pt_quote(f, val[i]);
		(void) fprintf(f, ";\n");
	}
	(void) fprintf(f, "at = %zu;\noutcomes = [", pz->pz_at);
	for (i = 0; i < pz->pz_at; i++)
		(void) fprintf(f, "%s\"%s\"", i == 0 ? " " : ", ",
		    sh_pout_name[pz->pz_out[i]]);
	(void) fprintf(f, " ];\nfetched = [");
	for (i = 0; i < pz->pz_nsrc; i++) {
		(void) fputs(i == 0 ? " " : ", ", f);
		pt_quote(f, pz->pz_commit[i]);
	}
	(void) fprintf(f, " ];\n");
	if (ferror(f) || fclose(f) != 0) {
		sh_errf(e, "%s: %s", path, strerror(errno));
		return (-1);
	}
	return (0);
}

/* The string KEY of TOP into BUF; 0, or -1 when it is not one. */
static int
pt_str(const struct sh_node *top, const char *key, char *buf, size_t len)
{
	const struct sh_node *v;

	if ((v = sh_obj_get(top, key)) == NULL || v->sn_type != SH_STR ||
	    strlen(v->sn_str) >= len)
		return (-1);
	(void) snprintf(buf, len, "%s", v->sn_str);
	return (0);
}

int
sh_pause_read(const char *path, struct sh_pause *pz, struct sh_err *e)
{
	const struct sh_node *at, *out, *got, *v;
	struct sh_node *top;
	size_t i, j;
	int bad;

	(void) memset(pz, 0, sizeof (*pz));
	if ((top = sh_parse_file(path, "paused.conf", e)) == NULL)
		return (-1);
	bad = pt_str(top, "config", pz->pz_config, sizeof (pz->pz_config)) |
	    pt_str(top, "version", pz->pz_version, sizeof (pz->pz_version)) |
	    pt_str(top, "commit", pz->pz_sha, sizeof (pz->pz_sha)) |
	    pt_str(top, "message", pz->pz_msg, sizeof (pz->pz_msg)) |
	    pt_str(top, "base", pz->pz_base, sizeof (pz->pz_base));
	at = sh_obj_get(top, "at");
	out = sh_obj_get(top, "outcomes");
	got = sh_obj_get(top, "fetched");
	if (bad != 0 || at == NULL || at->sn_type != SH_INT ||
	    at->sn_int < 0 || at->sn_int >= SH_PATCH_MAX || out == NULL ||
	    out->sn_type != SH_ARRAY ||
	    out->sn_arr.ar_n != (size_t)at->sn_int ||
	    got == NULL || got->sn_type != SH_ARRAY ||
	    got->sn_arr.ar_n > SH_PATCH_SRCS)
		goto bad;
	pz->pz_at = (size_t)at->sn_int;
	for (i = 0; i < pz->pz_at; i++) {
		v = out->sn_arr.ar_v[i];
		for (j = 0; j < SH_PO_N; j++) {
			if (v->sn_type == SH_STR &&
			    strcmp(v->sn_str, sh_pout_name[j]) == 0)
				break;
		}
		if (j == SH_PO_N)
			goto bad;
		pz->pz_out[i] = (unsigned char)j;
	}
	pz->pz_nsrc = got->sn_arr.ar_n;
	for (i = 0; i < pz->pz_nsrc; i++) {
		v = got->sn_arr.ar_v[i];
		if (v->sn_type != SH_STR ||
		    strlen(v->sn_str) >= sizeof (pz->pz_commit[i]))
			goto bad;
		(void) snprintf(pz->pz_commit[i], sizeof (pz->pz_commit[i]),
		    "%s", v->sn_str);
	}
	sh_node_free(top);
	return (0);
bad:
	sh_node_free(top);
	sh_errf(e, "%s: not a pause record", path);
	return (-1);
}
