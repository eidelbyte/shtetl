/*
 * A foundation version (planning.md 6.1, 7.1 step 1).
 *
 * Names are for people: shtetl reads shtetl:kind and orders versions by
 * createtxg, never by name (6.1). The names of a foundation's snapshots
 * are read here only so that a new one does not take one already there.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "core/err.h"
#include "core/syntax.h"
#include "core/tree.h"
#include "core/version.h"

#define	REVISION	"REVISION=\""

/* Digits at *P, below 1000, into *N; *P moved past. 1 if read. */
static int
vn_number(const char **p, long *n)
{
	const char *q = *p;

	*n = 0;
	if (*q < '0' || *q > '9')
		return (0);
	while (*q >= '0' && *q <= '9' && *n < 1000)
		*n = *n * 10 + (*q++ - '0');
	if ((*q >= '0' && *q <= '9') || *n >= 1000)
		return (0);
	*p = q;
	return (1);
}

int
sh_version_revision(const char *out, long *major, long *minor,
    struct sh_err *e)
{
	const char *p;

	p = out;
	if (strncmp(p, REVISION, strlen(REVISION)) == 0) {
		p += strlen(REVISION);
		if (vn_number(&p, major) && *p++ == '.' &&
		    vn_number(&p, minor) && *p++ == '"' &&
		    (*p == '\0' || (p[0] == '\n' && p[1] == '\0')))
			return (0);
	}
	sh_errf(e, "newvers.sh: no REVISION=\"N.M\" line");
	return (-1);
}

int
sh_version_name(const struct sh_branch *b, long major, time_t when,
    const char *sha, const char *const *have, size_t nhave, char *out,
    size_t len, struct sh_err *e)
{
	char base[SH_VERSION_MAX], ver[16];
	struct tm tm;
	size_t i, n;
	int k;

	for (i = 0; i < 12; i++) {
		if (!((sha[i] >= '0' && sha[i] <= '9') ||
		    (sha[i] >= 'a' && sha[i] <= 'f')))
			break;
	}
	if (i < 12) {
		sh_errf(e, "invalid commit: %s", sha);
		return (-1);
	}
	switch (b->br_type) {
	case SH_BR_RELENG:
		(void) snprintf(ver, sizeof (ver), "%ld.%ld-releng",
		    b->br_major, b->br_minor);
		break;
	case SH_BR_STABLE:
		(void) snprintf(ver, sizeof (ver), "%ld-stable", b->br_major);
		break;
	default:
		(void) snprintf(ver, sizeof (ver), "%ld-current", major);
		break;
	}
	if (gmtime_r(&when, &tm) == NULL) {
		sh_errf(e, "invalid time: %lld", (long long)when);
		return (-1);
	}
	(void) snprintf(base, sizeof (base), "%s-%04d-%02d-%02d-%.12s", ver,
	    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, sha);
	for (k = 1; ; k++) {
		if (k == 1)
			(void) snprintf(out, len, "%s", base);
		else
			(void) snprintf(out, len, "%s.%d", base, k);
		for (n = 0; n < nhave; n++) {
			if (strcmp(have[n], out) == 0)
				break;
		}
		if (n == nhave)
			return (0);
	}
}

/*
 * Each constraint of VER whose range the versions LO to HI miss
 * entirely, refused as WHAT not satisfying it. -1 if any.
 */
static int
vn_check(long lo, long hi, const char *what, const struct sh_node *ver,
    const char *file, struct sh_err *e)
{
	const struct sh_node *c;
	long clo, chi;
	size_t i;
	int bad;

	bad = 0;
	for (i = 0; ver != NULL && i < ver->sn_arr.ar_n; i++) {
		c = ver->sn_arr.ar_v[i];
		sh_ver_range(c->sn_str, &clo, &chi);
		if (hi < clo || lo > chi) {
			sh_errf(e, "%s: ver: %s does not satisfy %s in %s",
			    file, what, c->sn_str, c->sn_from);
			bad = 1;
		}
	}
	return (bad ? -1 : 0);
}

int
sh_version_check_branch(const struct sh_branch *b,
    const struct sh_node *ver, const char *file, struct sh_err *e)
{
	char what[32];
	long v;

	switch (b->br_type) {
	case SH_BR_RELENG:
		v = b->br_major * SH_VER_MINORS + b->br_minor;
		(void) snprintf(what, sizeof (what), "%ld.%ld", b->br_major,
		    b->br_minor);
		return (vn_check(v, v, what, ver, file, e));
	case SH_BR_STABLE:
		v = b->br_major * SH_VER_MINORS;
		(void) snprintf(what, sizeof (what), "%ld.x", b->br_major);
		return (vn_check(v, v + SH_VER_MINORS - 1, what, ver, file,
		    e));
	default:
		return (0);
	}
}

int
sh_version_check(long major, long minor, const struct sh_node *ver,
    const char *file, struct sh_err *e)
{
	char what[32];
	long v;

	v = major * SH_VER_MINORS + minor;
	(void) snprintf(what, sizeof (what), "%ld.%ld", major, minor);
	return (vn_check(v, v, what, ver, file, e));
}
